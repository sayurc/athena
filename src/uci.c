/*
 * Copyright (C) 2023 Sayu <mail@sayurc.moe>
 *
 * This file is part of Athena.
 *
 * Athena is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 * 
 * Athena is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <https://www.gnu.org/licenses/>. 
 */

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#include <bit.h>
#include <str.h>
#include <pos.h>
#include <move.h>
#include <movegen.h>
#include <tt.h>
#include <search.h>
#include <uci.h>

#define OPTION_UCI_ANALYSISMODE_TYPE boolean
#define OPTION_HASH_TYPE integer
#define OPTION_PONDER_TYPE boolean
#define OPTION_VALUE_TYPE(name) OPTION_##name##_TYPE

static struct search_argument search_arg;
static pthread_t search_thread;
static bool search_thread_created = false;
static atomic_bool stop_search = false;
static bool newgame_sent = false;
static bool initialized_transposition_table = false;

enum option_type {
	OPTION_TYPE_BOOLEAN,
	OPTION_TYPE_INTEGER,
	OPTION_TYPE_STRING,
	OPTION_TYPE_BUTTON,
};

union option_value {
	bool boolean;
	int integer;
	char *string;
};

static struct option {
	const char *name;
	const enum option_type type;
	void (*func)(void); /* Only used for button type. */
	const union option_value default_value;
	union option_value value;
	const int min;
	const int max;
} options[] = {
	{ .name = "Hash",
	  .type = OPTION_TYPE_INTEGER,
	  .default_value.integer = 1,
	  .value.integer = 1,
	  .min = 1,
	  .max = 32768 },

	/*
	{ .name = "Clear Hash",
	  .type = OPTION_TYPE_BUTTON,
	  .func = clear_hash_table },
	 */
};

static char *uci_receive(bool *eof);
static void uci(void);
static void setoption(void);
static char *read_words_until_equal(const char *str, bool *found);
static void isready(void);
static void position(void);
static int parse_moves(Move *moves, int capacity, Position *pos, int *len);
static void ucinewgame(void);
static void init_search_arg(struct search_argument *arg);
static void go(void);
static void stop(void);
static void quit(void);
static void info(const struct info *info);
static void id(void);
static void option(void);
static void uciok(void);
static void readyok(void);
static void bestmove(Move move);
static void uci_send(const char *fmt, ...);
static int str_to_option_value(union option_value *value, const char *name,
			       const char *str);
static struct option *get_option(const char *name);

void uci_loop(void)
{
	movegen_init();

	bool quit = false;
	while (!quit) {
		bool eof = false;
		char *str = uci_receive(&eof);
		if (eof) {
			quit = true;
		} else if (str) {
			quit = !uci_interpret(str);
			free(str);
		}
	}
}

/*
 * Reads a UCI message from stdin and return it, or return NULL if the message
 * is invalid or an error occurred. In both cases EOF is set to false. eof is
 * set to true and returns NULL if EOF is read.
 */
static char *uci_receive(bool *eof)
{
	size_t max_len = BUFSIZ;
	char *str = malloc(max_len + 1);
	if (!str) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}

	*eof = false;
	size_t i = 0;
	int ch = '\0';
	for (ch = fgetc(stdin); ch != '\n'; ch = fgetc(stdin)) {
		if (ch == EOF) {
			*eof = true;
			free(str);
			return NULL;
		}
		if (i == max_len) {
			max_len += BUFSIZ;
			char *const tmp = realloc(str, max_len + 1);
			if (!tmp) {
				fprintf(stderr, "Out of memory.\n");
				exit(1);
			}
			str = tmp;
		}
		str[i] = (char)ch;
		++i;
	}
	str[i] = '\0';
	if (!strlen(str)) {
		free(str);
		return NULL;
	}

	return str;
}

/*
 * Returns true normally and false when the "quit" command is used.
 */
bool uci_interpret(const char *str)
{
	bool ret = true;
	const size_t len = strlen(str);
	char *const split_str = malloc(len + 1);
	if (!split_str) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}

	strcpy(split_str, str);
	char *const cmd = strtok(split_str, " ");

	if (!cmd) {
		free(split_str);
		return ret;
	}

	if (search_thread_created && !stop_search && strcmp(cmd, "stop") &&
	    strcmp(cmd, "quit")) {
		free(split_str);
		return ret;
	}

	if (!strcmp(cmd, "uci")) {
		uci();
	} else if (!strcmp(cmd, "isready")) {
		isready();
	} else if (!strcmp(cmd, "setoption")) {
		setoption();
	} else if (!strcmp(cmd, "ucinewgame")) {
		ucinewgame();
	} else if (!strcmp(cmd, "position")) {
		position();
	} else if (!strcmp(cmd, "go")) {
		go();
	} else if (!strcmp(cmd, "stop")) {
		stop();
	} else if (!strcmp(cmd, "quit")) {
		quit();
		ret = false;
	}

	free(split_str);
	return ret;
}

static void uci(void)
{
	id();
	option();
	uciok();
}

static void setoption(void)
{
	char *token = strtok(NULL, " ");
	if (!token || strcmp(token, "name"))
		return;

	bool value_keyword;
	char *name = read_words_until_equal("value", &value_keyword);
	if (!name)
		return;

	struct option *const op = get_option(name);
	if (!op) {
		free(name);
		return;
	}

	if (!value_keyword && op->type != OPTION_TYPE_BUTTON) {
		free(name);
		return;
	} else if (op->type == OPTION_TYPE_BUTTON) {
		op->func();
		free(name);
		return;
	}

	char *const value_str = read_words_until_equal(NULL, NULL);
	if (!value_str) {
		free(name);
		return;
	}

	union option_value value;
	if (str_to_option_value(&value, name, value_str)) {
		free(name);
		free(value_str);
		return;
	}

	if (op->type == OPTION_TYPE_STRING)
		free(op->value.string);
	op->value = value;
	free(name);
	free(value_str);
}

/*
 * Read all the words until str is found or the end of the string has been
 * reached, and return the full sentence or NULL if there is nothing before str.
 * If str is NULL then the function will read until the end and the extra
 * argument is ignored.
 */
static char *read_words_until_equal(const char *str, bool *found)
{
	if (str)
		*found = false;
	size_t name_len = 0;
	char *joined = NULL, *word = NULL;
	for (word = strtok(NULL, " "); word && (!str || strcmp(word, str));
	     word = strtok(NULL, " ")) {
		const size_t word_len = strlen(word);
		name_len += word_len + 1;
		char *tmp = realloc(joined, name_len);
		if (!tmp) {
			fprintf(stderr, "Out of memory.\n");
			exit(1);
		}
		joined = tmp;
		strcpy(joined + name_len - word_len - 1, word);
		joined[name_len - 1] = ' ';
	}
	if (!joined)
		return NULL;
	--name_len;
	joined[name_len] = '\0';
	if (str && word)
		*found = true;
	return joined;
}

static void isready(void)
{
	readyok();
}

static void position(void)
{
	if (!newgame_sent)
		ucinewgame();
	const char *startpos = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w "
			       "KQkq - 0 1";
	Position pos;

	const char *token = strtok(NULL, " ");
	if (token && !strcmp(token, "startpos")) {
		init_position(&pos, startpos);
	} else if (token && !strcmp(token, "fen")) {
		char *fen = NULL;
		const size_t num_parts = 6;
		char *parts[6];
		for (size_t i = 0; i < num_parts; ++i) {
			parts[i] = strtok(NULL, " ");
			if (!parts[i])
				return;
		}
		size_t fen_len = 0;
		for (size_t i = 0; i < num_parts; ++i) {
			size_t part_len = strlen(parts[i]);
			fen_len += part_len + 1; /* + 1 for space or '\0'. */
			char *tmp = realloc(fen, fen_len);
			if (!tmp) {
				fprintf(stderr, "Out of memory.\n");
				exit(1);
			}
			fen = tmp;
			char *part_ptr = fen + fen_len - part_len - 1;
			strcpy(part_ptr, parts[i]);
			if (i < num_parts - 1) {
				fen[fen_len - 1] = ' ';
			} else {
				fen[fen_len - 1] = '\0';
				/* Remove the extra length that was added when
				 * it is reserved for '\0'. */
				--fen_len;
			}
		}
		const int error = init_position(&pos, fen);
		free(fen);
		if (error)
			return;
	} else {
		return;
	}

	token = strtok(NULL, " ");
	if (!token) {
		search_arg.pos = pos;
		return;
	}
	if (strcmp(token, "moves"))
		return;

	int moves_len;
	int error = parse_moves(
		search_arg.moves,
		(int)(sizeof(search_arg.moves) / sizeof(search_arg.moves[0])),
		&pos, &moves_len);
	if (error)
		return;
	search_arg.pos = pos;
	search_arg.moves_nb = moves_len;
}

static int parse_moves(Move *moves, int capacity, Position *pos, int *len)
{
	int num = 0;
	for (char *move = strtok(NULL, " "); move; move = strtok(NULL, " ")) {
		const size_t move_len = strlen(move);
		if (move_len > MAX_LAN_LEN)
			return 1;
		++num;
		if (num > capacity)
			return 1;
		bool success;
		moves[num - 1] = lan_to_move(move, pos, &success);
		if (!success)
			return 1;

		do_move(pos, moves[num - 1]);
	}
	*len = num;

	for (int i = *len - 1; i >= 0; --i)
		undo_move(pos, moves[i]);

	return 0;
}

static void ucinewgame(void)
{
	const struct option *const hash = get_option("Hash");
	if (!hash) {
		fprintf(stderr, "Internal error.\n");
		exit(1);
	}
	if (initialized_transposition_table)
		tt_free();
	tt_init(512);
	initialized_transposition_table = true;

	init_search_arg(&search_arg);

	newgame_sent = true;
}

static void init_search_arg(struct search_argument *arg)
{
	arg->moves_nb = 0;
	arg->stop = &stop_search;
	arg->info_sender = info;
	arg->best_move_sender = bestmove;
	arg->depth = INT_MAX;
	arg->nodes = LLONG_MAX;
	arg->time[COLOR_WHITE] = arg->time[COLOR_BLACK] = 0;
	arg->inc[COLOR_WHITE] = arg->inc[COLOR_BLACK] = 0;
	arg->movetime = 0;
	arg->mate = 0;
	init_search_context(&arg->ctx);
#ifdef SEARCH_STATISTICS
	arg->log_file = fopen("search.log", "w");
	if (!arg->log_file) {
		fprintf(stderr, "Could not create log file.\n");
		exit(1);
	}
#endif
}

/*
 * Infinite searches are done by maxing out the search limits.
 */
static void go(void)
{
	char *str = strtok(NULL, " ");
	while (str) {
		if (!strcmp(str, "infinite")) {
			search_arg.depth = 100;
		} else {
			const char *const value = strtok(NULL, " ");
			if (!value)
				return;
			char *endptr = NULL;
			errno = 0;
			int x = (int)strtol(value, &endptr, 10);
			if (errno == ERANGE || endptr == value)
				return;

			if (!strcmp(str, "depth")) {
				search_arg.depth = x;
			} else if (!strcmp(str, "nodes")) {
				search_arg.nodes = x;
			} else if (!strcmp(str, "mate")) {
				search_arg.mate = x;
			} else if (!strcmp(str, "wtime")) {
				search_arg.time[COLOR_WHITE] = x;
			} else if (!strcmp(str, "btime")) {
				search_arg.time[COLOR_BLACK] = x;
			} else if (!strcmp(str, "winc")) {
				search_arg.inc[COLOR_WHITE] = x;
			} else if (!strcmp(str, "binc")) {
				search_arg.inc[COLOR_BLACK] = x;
			} else if (!strcmp(str, "movestogo")) {
				search_arg.movestogo = x;
			} else if (!strcmp(str, "movetime")) {
				search_arg.movetime = x;
			} else if (!strcmp(str, "perft")) {
				/* TODO. */
			} else {
				break;
			}
		}
		str = strtok(NULL, " ");
	}

	if (search_thread_created) {
		search_thread_created = false;
		if (pthread_join(search_thread, NULL)) {
			fprintf(stderr, "Internal error.\n");
			exit(1);
		}
	}

	stop_search = false;
	if (pthread_create(&search_thread, NULL, search, &search_arg)) {
		search_thread_created = false;
		stop_search = true;
		perror("Athena");
	} else {
		search_thread_created = true;
	}
}

static void stop(void)
{
	if (search_thread_created) {
		search_thread_created = false;
		stop_search = true;
		if (pthread_join(search_thread, NULL)) {
			fprintf(stderr, "Internal error.\n");
			exit(1);
		}
	}
}

static void quit(void)
{
	if (search_thread_created) {
		search_thread_created = false;
		stop_search = true;
		if (pthread_join(search_thread, NULL)) {
			fprintf(stderr, "Internal error.\n");
			exit(1);
		}
	}
	for (size_t i = 0; i < sizeof(options) / sizeof(options[0]); ++i) {
		struct option *const op = &options[i];
		if (op->type == OPTION_TYPE_STRING)
			free(op->value.string);
	}
	if (initialized_transposition_table) {
		tt_free();
		initialized_transposition_table = false;
	}
}

static void info(const struct info *info)
{
	char *str = malloc(6), *tmp;
	if (!str) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}

	if (!info->flags) {
		free(str);
		return;
	}

	strcpy(str, "info ");

	if (info->flags & INFO_FLAG_DEPTH) {
		SAFE_ASPRINTF(&tmp, "%sdepth %d ", str, info->depth);
		free(str);
		str = tmp;
	}
	if (info->flags & INFO_FLAG_NODES) {
		SAFE_ASPRINTF(&tmp, "%snodes %lld ", str, info->nodes);
		free(str);
		str = tmp;
	}
	if (info->flags & INFO_FLAG_CP) {
		SAFE_ASPRINTF(&tmp, "%sscore cp %d ", str, info->cp);
		free(str);
		str = tmp;
	} else if (info->flags & INFO_FLAG_MATE) {
		SAFE_ASPRINTF(&tmp, "%sscore mate %d ", str, info->mate);
		free(str);
		str = tmp;
	}
	if (info->flags & INFO_FLAG_LBOUND) {
		SAFE_ASPRINTF(&tmp, "%slowerbound ", str);
		free(str);
		str = tmp;
	}
	if (info->flags & INFO_FLAG_NPS) {
		SAFE_ASPRINTF(&tmp, "%snps %lld ", str, info->nps);
		free(str);
		str = tmp;
	}
	if (info->flags & INFO_FLAG_TIME) {
		SAFE_ASPRINTF(&tmp, "%stime %lld", str, info->time);
		free(str);
		str = tmp;
	}

	str[strlen(str)] = 0;
	uci_send(str);
	free(str);
}

static void id(void)
{
	uci_send("id name Athena");
	uci_send("id author sayurc");
}

static void option(void)
{
	for (size_t i = 0; i < sizeof(options) / sizeof(options[0]); ++i) {
		const struct option *const op = &options[i];
		switch (op->type) {
		case OPTION_TYPE_BOOLEAN:
			if (op->default_value.boolean == false)
				uci_send("option name %s type check default %s",
					 op->name, "false");
			else
				uci_send("option name %s type check default %s",
					 op->name, "true");
			break;
		case OPTION_TYPE_INTEGER:
			uci_send("option name %s type spin default %d min %d "
				 "max %d",
				 op->name, op->default_value.integer, op->min,
				 op->max);
			break;
		case OPTION_TYPE_STRING:
			uci_send("option name %s type string default %s",
				 op->name, op->default_value.string);
			break;
		case OPTION_TYPE_BUTTON:
			uci_send("option name %s type button", op->name);
			break;
		}
	}
}

static void uciok(void)
{
	uci_send("uciok");
}

static void readyok(void)
{
	uci_send("readyok");
}

static void bestmove(Move move)
{
	char lan[MAX_LAN_LEN + 1];

	move_to_lan(lan, move);
	uci_send("bestmove %s", lan);
}

static void uci_send(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	putchar('\n');
	fflush(stdout);
}

/*
 * Converts a string to a value for an option and returns 0 on success and 1
 * otherwise.
 */
static int str_to_option_value(union option_value *value, const char *name,
			       const char *str)
{
	const struct option *op;

	for (size_t i = 0; i < sizeof(options) / sizeof(options[0]); ++i) {
		op = &options[i];
		if (!strcmp(name, op->name)) {
			switch (op->type) {
			case OPTION_TYPE_BOOLEAN:
				goto type_boolean;
			case OPTION_TYPE_INTEGER:
				goto type_integer;
			case OPTION_TYPE_STRING:
				goto type_string;
			default:
				exit(1);
			}
		}
	}
	return 1;

type_boolean:
	if (!strcmp(str, "true"))
		value->boolean = true;
	else if (!strcmp(str, "false"))
		value->boolean = false;
	else
		return 1;
	return 0;
type_integer:
	errno = 0;
	char *endptr;
	int n = (int)strtol(str, &endptr, 10);
	if (errno == ERANGE || endptr == str || *endptr != '\0' ||
	    n < op->min || n > op->max)
		return 1;
	value->integer = n;
	return 0;
type_string:
	value->string = malloc(strlen(str) + 1);
	if (!value->string) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	strcpy(value->string, str);
	return 0;
}

static struct option *get_option(const char *name)
{
	const size_t num = sizeof(options) / sizeof(struct option);
	for (size_t i = 0; i < num; ++i) {
		if (!strcmp(options[i].name, name))
			return &options[i];
	}

	return NULL;
}
