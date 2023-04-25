/*
 * Copyright (C) 2023 Aiya <mail@aiya.moe>
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

#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>

#include <pthread.h>

#include <check.h>

#include "bit.h"
#include "pos.h"
#include "move.h"
#include "movegen.h"
#include "tt.h"
#include "eval.h"
#include "rng.h"
#include "search.h"

#define MAX_DEPTH 128
#define MAX_PLY 256
#define REPETITION_TABLE_LEN 8191
#define MAX_KILLER_MOVES 2

struct search_data {
	int ply;
	long long nodes;
	Position *pos;
	Move killers[MAX_PLY + 1][MAX_KILLER_MOVES];
	Move move_made[MAX_PLY + 1];
	i8 repetitions[REPETITION_TABLE_LEN];
};

struct result {
	Move best;
	bool found_mate;
	long long nodes;
};

struct parameters {
	int depth;
	int mate;
	int movestogo;
	long long nodes;
	struct timespec stop_time;
	Position *pos;
	Position **old_positions;
	size_t num_old_positions;
	bool *stop;
	pthread_mutex_t *stop_mtx;
	void (*output)(const struct info *);
};

static const int INFINITE = SHRT_MAX;

static void inc_repetition(i8 *repetitions, const Position *pos)
{
	const u64 hash = tt_hash(pos);
	const size_t key = hash % REPETITION_TABLE_LEN;
	++repetitions[key];
}

static void dec_repetition(i8 *repetitions, const Position *pos)
{
	const u64 hash = tt_hash(pos);
	const size_t key = hash % REPETITION_TABLE_LEN;
	--repetitions[key];
}

static bool repeated(struct search_data *data, const struct parameters *params)
{
	const u64 hash = tt_hash(data->pos);
	const size_t key = hash % REPETITION_TABLE_LEN;

	if (!data->repetitions[key])
		return false;

	Position *prev_pos = pos_copy(data->pos);

	/* One position is skipped because it's impossible that it's the same as
	 * the current one. */
	int prev_ply = data->ply - 1;
	for (; prev_ply >= 0; --prev_ply) {
		move_undo(prev_pos, data->move_made[prev_ply]);
		--prev_ply;
		if (prev_ply < 0)
			break;
		move_undo(prev_pos, data->move_made[prev_ply]);

		if (pos_equal(data->pos, prev_pos))
			return true;
	}
	pos_destroy(prev_pos);

	/* We also have to check the positions from previous searches. */
	for (size_t i = 0; i < params->num_old_positions; ++i) {
		if (pos_equal(data->pos, params->old_positions[i]))
			return true;
	}

	return false;
}

/*
 * This function stores a new killer move by shifting all the killer moves for
 * a certain depth, discarding the move in the last slot, the oldest one, and
 * then places the new move in the first slot. It is important that all the
 * slots contain different moves, otherwise we waste computation time in move
 * ordering looking for the same killer move again.
 */
static void store_killer(Move *killers, Move move)
{
	for (int i = 0; i < MAX_KILLER_MOVES; ++i) {
		if (move == killers[i])
			return;
	}
	for (int i = MAX_KILLER_MOVES - 1; i > 0; --i)
		killers[i] = killers[i - 1];
	killers[0] = move;
}

static bool is_killer(Move move, const Move *killers)
{
	for (int i = 0; i < MAX_KILLER_MOVES; ++i) {
		Move killer = killers[i];
		if (killer && move == killer)
			return true;
	}
	return false;
}

/*
 * Returns the index of what seems to be the most promising by evaluating moves.
 *
 * The best move of PV nodes are stored in the transposition table so and since
 * all the moves of PV nodes moves have been searched we know for sure that
 * that move is the best for that position. So the best move of PV nodes have
 * higher priority than any other moves.
 *
 * The killer moves are searched next because they caused a beta cutoff and are
 * likely to cause a beta cutoff again in the rest of the moves. However, some
 * captures have the potential to make the killer move not a good choice (for
 * example, if white captures black's queen in the other branch before black
 * makes the killer move then the killer move might no longer be an option for
 * black). So very good captures have priority.
 * 
 * To simulate this priority order we have some offsets that act as the starting
 * point for the score of a move which is then added to the offset. Captures
 * have a smaller offset than killer moves, but they are still close enough for
 * a capture to surpass a killer move if it is good enough.
 * 
 * There is no offset for the best move of PV nodes because they are always
 * searched first, so if the position is a PV node we just return the move we
 * have in the transposition table. And other moves have offset 0 because they
 * have lower priority than captures.
 */
static Move get_next_move(const Move *moves, size_t len,
struct search_data *data)
{
	static const int capture_offset = 300;
	static const int killer_offset = 600;
	int best_score = -INFINITE;
	size_t best_idx = 0;

	for (size_t i = 0; i < len; ++i) {
		const Move move = moves[i];
		NodeData pos_data;
		if (tt_get(&pos_data, data->pos) && pos_data.type == NODE_TYPE_PV) {
			if (move == pos_data.best_move)
				return i;
		}
	}

	for (size_t i = 0; i < len; ++i) {
		const Move move = moves[i];
		int score = 0;
		if (is_killer(move, data->killers[data->ply]))
			score = killer_offset + eval_evaluate_move(move, data->pos);
		else if (move_is_capture(move))
			score = capture_offset + eval_evaluate_move(move, data->pos);
		else
			score = eval_evaluate_move(move, data->pos);

		if (score > best_score) {
			best_idx = i;
			best_score = score;
		}
	}

	return best_idx;
}

/*
 * Returns the index of what seems to be the most promising for the quiescence
 * search. Non capture moves will be ignored. If there are no more capturing
 * moves then *ended is set to true.
 */
static size_t get_next_qmove(const Move *moves, size_t len,
struct search_data *data, bool *ended)
{
	int best_score = -INFINITE;
	size_t best_idx = 0;

	for (size_t i = 0; i < len; ++i) {
		const Move move = moves[i];
		if (!move_is_capture(move))
			continue;

		NodeData pos_data;
		if (tt_get(&pos_data, data->pos) && pos_data.type == NODE_TYPE_PV) {
			if (move == pos_data.best_move)
				return i;
		}

		int score = eval_evaluate_qmove(move, data->pos);
		if (score > best_score) {
			best_idx = i;
			best_score = score;
		}
	}

	if (best_score == -INFINITE)
		*ended = true;

	return best_idx;
}

static bool is_in_check(const Position *pos)
{
	const Color c = pos_get_side_to_move(pos);
	const Square king_sq = pos_get_king_square(pos, c);
	return movegen_is_square_attacked(king_sq, !c, pos);
}

static bool has_legal_moves(Move *moves, size_t len, Position *pos)
{
	for (size_t i = 0; i < len; ++i) {
		if (move_is_legal(pos, moves[i]))
			return true;
	}
	return false;
}

/*
 * The quiescence search is performed at the leaf nodes of the main search. It
 * searches for the best quiet position after a sequence of captures, so only
 * capturing moves are made. This way we can avoid the horizon effect where the
 * main search might stop at what seems to be a good position, but then a
 * valuable piece is captured right in the next move. Thus, the quiescence
 * search must go through all the possible captures until there are only quiet
 * moves left as leaf nodes, which is possible because the tree of captures is
 * not as wide as the tree of all moves. 
 */
static int qsearch(int depth, int alpha, int beta, struct search_data *data,
struct info *info, const struct parameters *params)
{
	pthread_mutex_lock(params->stop_mtx);
	if (data->nodes >= params->nodes || data->ply > MAX_PLY)
		*params->stop = true;
	if (*params->stop || data->nodes >= params->nodes) {
		pthread_mutex_unlock(params->stop_mtx);
		return alpha;
	}
	pthread_mutex_unlock(params->stop_mtx);

	++data->nodes;
	++info->nodes;

	if (repeated(data, params))
		return 0;
	inc_repetition(data->repetitions, data->pos);

	NodeData pos_data;
	if (tt_get(&pos_data, data->pos) && pos_data.depth >= depth)
		return pos_data.score;

	int stand_pat = eval_evaluate(data->pos);
	if (stand_pat >= beta)
		return stand_pat;
	if (alpha < stand_pat)
		alpha = stand_pat;

	NodeType type = NODE_TYPE_ALL;
	Move best_move;
	bool has_legal = false;
	size_t len = 0;
	Move *const moves_ptr = movegen_get_pseudo_legal_moves(data->pos, &len);
	for (Move *moves = moves_ptr; len; --len, ++moves) {
		if (len > 1) {
			Move first = moves[0];
			bool ended = false;
			size_t i = get_next_qmove(moves, len, data, &ended);
			if (ended && !has_legal) {
				has_legal = has_legal_moves(moves, len, data->pos);
				break;
			} else if (ended) {
				break;
			}
			Move most_promising = moves[i];
			moves[0] = most_promising;
			moves[i] = first;
		}

		Move move = *moves;
		if (move_is_legal(data->pos, move))
			has_legal = true;
		if (!move_is_legal(data->pos, move) || !move_is_capture(move))
			continue;

		move_do(data->pos, move);
		data->move_made[data->ply] = move;
		++data->ply;
		tt_prefetch();
		int score = -qsearch(depth - 1, -beta, -alpha, data, info, params);
		dec_repetition(data->repetitions, data->pos);
		move_undo(data->pos, move);
		--data->ply;

		if (score > alpha) {
			alpha = score;
			best_move = move;
			type = NODE_TYPE_PV;
		}
		if (alpha >= beta) {
			type = NODE_TYPE_CUT;
			break;
		}
	}
	free(moves_ptr);

	if (!has_legal) {
		if (is_in_check(data->pos)) {
			info->mate = (data->ply + 1) / 2;
			return -INFINITE + data->ply;
		} else {
			return 0;
		}
	}

	tt_entry_init(&pos_data, alpha, depth, type, best_move, data->pos);
	tt_store(&pos_data);

	return alpha;
}

/*
 * This is the main negamax function with alpha beta pruning, it returns the
 * best score achievable for a position. It returns -INFINITE or 0 if the best
 * outcome calculated is a checkmate or stalemate, respectively, which means
 * that such outcome is unavoidable. In the case of checkmate it will
 * additionally set info->mate to the number of moves (full moves, not plies)
 * for checkmate.
 */
static int negamax(int depth, int alpha, int beta, struct search_data *data,
struct info *info, const struct parameters *params)
{
	pthread_mutex_lock(params->stop_mtx);
	if (data->nodes >= params->nodes || data->ply > MAX_PLY)
		*params->stop = true;
	if (*params->stop || data->nodes >= params->nodes) {
		pthread_mutex_unlock(params->stop_mtx);
		return alpha;
	}
	pthread_mutex_unlock(params->stop_mtx);

	++data->nodes;
	++info->nodes;

	if (repeated(data, params))
		return 0;
	inc_repetition(data->repetitions, data->pos);

	NodeData pos_data;
	if (tt_get(&pos_data, data->pos) && pos_data.depth >= depth)
		return pos_data.score;
	if (!depth) {
		/* The quiescence search will count this node so we decrement
		 * the current count to avoid counting it twice. */
		--data->nodes;
		--info->nodes;
		return qsearch(depth, alpha, beta, data, info, params);
	}

	NodeType type = NODE_TYPE_ALL;

	bool in_check = is_in_check(data->pos);

	Move best_move;
	bool has_legal = 0;
	size_t len = 0;
	Move *const moves_ptr = movegen_get_pseudo_legal_moves(data->pos, &len);

	int eval = eval_evaluate(data->pos);

	for (Move *moves = moves_ptr; len; --len, ++moves) {
		/* Lazily sort moves instead of doing it all at once, this way
		 * we avoid wasting time sorting moves of branches that are
		 * pruned. */
		if (len > 1) {
			Move first = moves[0];
			size_t i = get_next_move(moves, len, data);
			Move most_promising = moves[i];
			moves[0] = most_promising;
			moves[i] = first;
		}
		Move move = *moves;
		if (!move_is_legal(data->pos, move))
			continue;
		has_legal = true;

		/* Futility pruning. If the position score plus some safety
		 * margin is not enough to raise alpha, then skip all the next
		 * moves. The safety margin is proportional to the depth with
		 * proportionality constant equal to 1.5 centipawns so that
		 * upper nodes are less likely to be pruned. */
		if (move_is_quiet(move) && !in_check) {
			if (eval + 150 * depth <= alpha)
				break;
		}

		move_do(data->pos, move);
		data->move_made[data->ply] = move;
		++data->ply;
		tt_prefetch();
		int score = -negamax(depth - 1, -beta, -alpha, data, info, params);
		dec_repetition(data->repetitions, data->pos);
		move_undo(data->pos, move);
		--data->ply;

		if (score > alpha) {
			alpha = score;
			best_move = move;
			type = NODE_TYPE_PV;
		}
		if (alpha >= beta) {
			if (!move_is_capture(move))
				store_killer(data->killers[data->ply], move);
			type = NODE_TYPE_CUT;
			break;
		}
	}
	free(moves_ptr);

	if (!has_legal) {
		if (is_in_check(data->pos)) {
			info->mate = (data->ply + 1) / 2;
			return -INFINITE + data->ply;
		} else {
			return 0;
		}
	}

	tt_entry_init(&pos_data, alpha, depth, type, best_move, data->pos);
	tt_store(&pos_data);
	return alpha;
}

/*
 * This function must be called before any searches are performed, it
 * initializes all the tables and call other initialization functions. It does
 * not need to be called between every search, only once (unless it's desirable
 * to start a new search with a clean state.)
 */
void search_init(void)
{
	tt_init();
	eval_init();
}

void search_finish(void)
{
	tt_finish();
}

/*
 * Returns the elapsed time between two timestamps in milliseconds.
 */
static double get_elapsed_time(const struct timespec *ts1,
const struct timespec *ts2)
{
	const double t1 = ts1->tv_sec * 1e3 + ts1->tv_nsec / 10e6;
	const double t2 = ts2->tv_sec * 1e3 + ts2->tv_nsec / 10e6;
	return ceil(t2 - t1);
}

/*
 * This is the root search function that calls the main negamax function, which
 * is used to score the root moves and the best one is returned. If the position
 * is a checkmate or stalemate for the side to move, 0 is returned. If all moves
 * lead to checkmate or stalemate for the side to move, then any of these moves
 * is returned.
 * 
 * The main negamax function will return -INFINITE if a checkmate for the
 * opposite side is unavoidable, and since it will also set the number of plies
 * to the mate in info, if we are searching for a mate we just have to return
 * the move that leads to this mate as the best move and set
 * thread_data->found_mate to true. Note that this member must not be set to
 * true by the main negamax function when a mate is found, because the mate can
 * only be considered reachable after the search finishes.
 * 
 * Notice that this function, along with the search functions called from it,
 * may stop at any moment if requested. If that happens while the search is
 * running it will simply return anything, so the calling function must check
 * if a stop request has been received and if so ignore whatever this function
 * returned.
 */
static struct result search(const struct parameters *params)
{
	struct search_data data;
	data.ply = 0;
	data.nodes = 0;
	data.pos = pos_copy(params->pos);
	memset(data.move_made, 0, sizeof(data.move_made));
	memset(data.killers, 0, sizeof(data.killers));
	memset(data.repetitions, 0, sizeof(data.repetitions));
	for (size_t i = 0; i < params->num_old_positions; ++i)
		inc_repetition(data.repetitions, params->old_positions[i]);

	inc_repetition(data.repetitions, data.pos);

	struct info info;
	info.depth = params->depth;
	info.nodes = info.nps = info.mate = 0;

	struct result result;
	result.found_mate = false;
	result.best = result.nodes = 0;

	int alpha = -INFINITE, beta = INFINITE;

	size_t len;
	Move *const moves = movegen_get_pseudo_legal_moves(params->pos, &len);

	struct timespec ts1, ts2;
	long long old_nodes = info.nodes;

	timespec_get(&ts1, TIME_UTC);
	for (size_t i = 0; i < len; ++i) {
		pthread_mutex_lock(params->stop_mtx);
		if (data.nodes >= params->nodes)
			*params->stop = true;
		if (*params->stop) {
			pthread_mutex_unlock(params->stop_mtx);
			break;
		}
		pthread_mutex_unlock(params->stop_mtx);

		Move move = moves[i];
		if (!move_is_legal(data.pos, move))
			continue;

		move_do(data.pos, move);
		data.move_made[data.ply] = move;
		++data.ply;
		tt_prefetch();
		int score = -negamax(params->depth - 1, -beta, -alpha, &data, &info, params);
		dec_repetition(data.repetitions, data.pos);
		move_undo(data.pos, move);
		--data.ply;

		if (score > alpha) {
			alpha = score;
			result.best = move;
		}
		if (params->mate && alpha == INFINITE && info.mate == params->mate) {
			result.found_mate = true;
			result.best = move;
			break;
		}
		if (alpha >= beta)
			break;
	}
	timespec_get(&ts2, TIME_UTC);

	result.nodes = data.nodes;

	double dt = get_elapsed_time(&ts1, &ts2);
	const double nps = (double)(info.nodes - old_nodes) * 1000 / dt;
	info.nps = (long long)round(nps);
	info.time = (long long)round(dt);
	info.flags = INFO_FLAG_DEPTH | INFO_FLAG_NODES | INFO_FLAG_NPS | INFO_FLAG_TIME;
	if (alpha == INFINITE)
		info.flags |= INFO_FLAG_MATE;
	params->output(&info);

	if (!result.best && len) {
		for (size_t i = 0; i < len; ++i) {
			if (move_is_legal(data.pos, moves[i]))
				result.best = moves[i];
		}
	}
	free(moves);

	dec_repetition(data.repetitions, data.pos);
	pos_destroy(data.pos);

	return result;
}

static void perft(const struct parameters *params)
{
	struct info info;
	struct timespec ts1, ts2;

	Position *const pos = pos_copy(params->pos);

	timespec_get(&ts1, TIME_UTC);
	info.nodes = movegen_perft(pos, params->depth);
	timespec_get(&ts2, TIME_UTC);

	double dt = get_elapsed_time(&ts1, &ts2);
	const double nps = (double)info.nodes / dt;
	info.nps = round(nps);

	info.flags = INFO_FLAG_NODES | INFO_FLAG_NPS;
	params->output(&info);
}

/*
 * When stop is true or the maximum number of nodes is reached we return the
 * last best root move we calculated, ignoring everything that was calculated
 * after, because this iteration's search probably didn't finish.
 * After signaling stop the calling thread must not change the stop information
 * until the search thread terminates, otherwise the search functions might
 * stop while this function might not and it will continue searching.
 */
void *search_run(void *data)
{
	struct search_argument *const arg = data;

	int max_depth = arg->settings.depth;
	long long nodes = arg->settings.nodes;
	bool *const stop = arg->stop;
	pthread_mutex_t *const mtx = arg->stop_mtx;

	if (arg->settings.infinite || arg->settings.mate) {
		max_depth = MAX_DEPTH;
		nodes = LLONG_MAX;
	} else {
		if (arg->settings.depth > MAX_DEPTH)
			max_depth = MAX_DEPTH;
	}

	if (arg->settings.perft) {
		struct parameters params;
		params.depth = arg->settings.perft;
		params.pos = arg->settings.positions[arg->settings.num_positions - 1];
		params.output = arg->settings.info_sender;
		perft(&params);
		return NULL;
	}

	Move best_move = 0;
	for (int depth = 1; depth <= max_depth && nodes > 0; ++depth) {
		struct parameters params;
		params.depth = depth;
		params.mate = arg->settings.mate;
		params.movestogo = arg->settings.movestogo;
		params.nodes = nodes;
		params.pos = arg->settings.positions[arg->settings.num_positions - 1];
		params.old_positions = arg->settings.positions;
		params.num_old_positions = arg->settings.num_positions - 1;
		params.stop = arg->stop;
		params.stop_mtx = arg->stop_mtx;
		params.output = arg->settings.info_sender;

		struct result result = search(&params);

		nodes -= result.nodes;

		pthread_mutex_lock(mtx);
		if (*stop) {
			pthread_mutex_unlock(mtx);
			break;
		}
		pthread_mutex_unlock(mtx);

		best_move = result.best;
		if (arg->settings.mate && result.found_mate)
			break;
	}
	arg->settings.best_move_sender(best_move);

	free(arg);
	return NULL;
}
