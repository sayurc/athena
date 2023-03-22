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

struct internal_data {
	long long total_nodes;
	long ply;
	bool found_mate;
};

static const int INFINITE = SHRT_MAX;

#define MAX_DEPTH 128
#define MAX_KILLER_MOVES 2

Move killer_moves[MAX_DEPTH][MAX_KILLER_MOVES];

/*
 * This function stores a new killer move by shifting all the killer moves for
 * a certain depth, discarding the move in the last slot, the oldest one, and
 * then places the new move in the first slot. It is important that all the
 * slots contain different moves, otherwise we waste computation time in move
 * ordering looking for the same killer move again.
 */
static void store_killer(Move move, int depth)
{
	const size_t depth_idx = depth - 1;;

	for (int i = 0; i < MAX_KILLER_MOVES; ++i) {
		if (move == killer_moves[depth_idx][i])
			return;
	}
	for (int i = MAX_KILLER_MOVES - 1; i > 0; --i)
		killer_moves[depth_idx][i] = killer_moves[depth_idx][i - 1];
	killer_moves[depth_idx][0] = move;
}

static bool is_killer(Move move, int depth)
{
	for (int i = 0; i < MAX_KILLER_MOVES; ++i) {
		Move killer = killer_moves[depth - 1][i];
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
 * captures to surpass the killer move if it is good enough and/or the killer
 * move is bad enough.
 * 
 * There is no offset for the best move of PV nodes because they are always
 * searched first, so if the position is a PV node we just return the move we
 * have in the transposition table. And other moves have offset 0 because they
 * have lower priority than captures.
 */
static size_t get_most_promising_move(const Move *restrict moves, size_t len, Position *restrict pos, int depth)
{
	static const int capture_offset = INFINITE / 64;
	static const int killer_offset = INFINITE / 32;
	int best_score = -INFINITE;
	size_t best_idx = 0;

	for (size_t i = 0; i < len; ++i) {
		const Move move = moves[i];
		NodeData pos_data;
		if (tt_get(&pos_data, pos) && pos_data.type == NODE_TYPE_PV) {
			if (move == pos_data.best_move)
				return i;
		}
	}

	for (size_t i = 0; i < len; ++i) {
		const Move move = moves[i];
		int score = 0;
		if (is_killer(move, depth))
			score = killer_offset + eval_evaluate_move(move, pos);
		else if (move_is_capture(move))
			score = capture_offset + eval_evaluate_move(move, pos);
		else
			score = eval_evaluate_move(move, pos);

		if (score > best_score) {
			best_idx = i;
			best_score = score;
		}
	}

	return best_idx;
}

static bool is_in_check(const Position *pos)
{
	const Color c = pos_get_side_to_move(pos);
	const Square king_sq = pos_get_king_square(pos, c);
	return movegen_is_square_attacked(king_sq, !c, pos);
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
static int qsearch(int alpha, int beta, struct thread_data *restrict thread_data, struct search_info *restrict info)
{
	pthread_mutex_lock(&thread_data->stop_mtx);
	if (thread_data->stop) {
		pthread_mutex_unlock(&thread_data->stop_mtx);
		return alpha;
	}
	pthread_mutex_unlock(&thread_data->stop_mtx);

	int score = eval_evaluate(thread_data->settings.position);
	alpha = score > alpha ? score : alpha;

	size_t len, legal_moves_cnt = 0;
	Move *moves = movegen_get_pseudo_legal_moves(thread_data->settings.position, &len);
	for (size_t i = 0; i < len && thread_data->internal->total_nodes < thread_data->settings.nodes; ++i) {
		Move move = moves[i];
		if (move_is_legal(thread_data->settings.position, move))
			++legal_moves_cnt;
		if (!move_is_legal(thread_data->settings.position, move) || !move_is_capture(move))
			continue;

		move_do(thread_data->settings.position, move);
		++thread_data->internal->ply;
		score = -qsearch(-beta, -alpha, thread_data, info);
		move_undo(thread_data->settings.position, move);
		--thread_data->internal->ply;

		++thread_data->internal->total_nodes;
		++info->nodes;

		if (score > alpha)
			alpha = score;
		if (alpha >= beta)
			break;
	}
	free(moves);

	if (!legal_moves_cnt) {
		if (is_in_check(thread_data->settings.position)) {
			info->mate = (thread_data->internal->ply + 1) / 2;
			return -INFINITE;
		} else {
			return 0;
		}
	}

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
static int negamax(int depth, int alpha, int beta, struct thread_data *restrict thread_data, struct search_info *restrict info)
{
	pthread_mutex_lock(&thread_data->stop_mtx);
	if (thread_data->stop) {
		pthread_mutex_unlock(&thread_data->stop_mtx);
		return alpha;
	}
	pthread_mutex_unlock(&thread_data->stop_mtx);

	NodeData pos_data;
	if (tt_get(&pos_data, thread_data->settings.position) && pos_data.depth >= depth)
		return pos_data.score;
	if (!depth)
		return qsearch(alpha, beta, thread_data, info);


	NodeType type = NODE_TYPE_ALL;
	Move best_move;
	size_t legal_moves_cnt = 0, len = 0;
	Move *moves = movegen_get_pseudo_legal_moves(thread_data->settings.position, &len);
	Move *const moves_ptr = moves;
	while (len && thread_data->internal->total_nodes < thread_data->settings.nodes) {
		/* Lazily sort moves instead of doing it all at once, this way
		 * we avoid wasting time sorting moves of branches that are
		 * pruned. */
		if (len > 1) {
			Move first = moves[0];
			size_t i = get_most_promising_move(moves, len, thread_data->settings.position, depth);
			Move most_promising = moves[i];
			moves[0] = most_promising;
			moves[i] = first;
		}

		Move move = *moves;
		if (!move_is_legal(thread_data->settings.position, move)) {
			--len;
			++moves;
			continue;
		}
		++legal_moves_cnt;

		move_do(thread_data->settings.position, move);
		++thread_data->internal->ply;
		int score = -negamax(depth - 1, -beta, -alpha, thread_data, info);
		move_undo(thread_data->settings.position, move);
		--thread_data->internal->ply;

		++thread_data->internal->total_nodes;
		++info->nodes;

		if (score > alpha) {
			alpha = score;
			best_move = move;
			type = NODE_TYPE_PV;
		}
		if (alpha >= beta) {
			if (!move_is_capture(move))
				store_killer(move, depth);
			type = NODE_TYPE_CUT;
			break;
		}

		--len;
		++moves;
	}
	free(moves_ptr);

	if (!legal_moves_cnt) {
		if (is_in_check(thread_data->settings.position)) {
			info->mate = (thread_data->internal->ply + 1) / 2;
			return -INFINITE;
		} else {
			return 0;
		}
	}

	tt_entry_init(&pos_data, alpha, depth, type, best_move, thread_data->settings.position);
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
	for (size_t i = 0; i < MAX_DEPTH; ++i) {
		for (size_t j = 0; j < MAX_KILLER_MOVES; ++j)
			killer_moves[i][j] = 0;
	}
	tt_init();
	eval_init();
}

void search_finish(void)
{
	tt_finish();
}

/*
 * Returns the elapsed time between two timestamps with nanosecond precision.
 */
static double get_elapsed_time(const struct timespec *restrict ts1, const struct timespec *restrict ts2)
{
	const double t1 = ts1->tv_sec + ts1->tv_nsec / 10e9;
	const double t2 = ts2->tv_sec + ts2->tv_nsec / 10e9;
	return t2 - t1;
}

/*
 * This is the root search function that calls the main negamax function, which
 * is used to score the root moves and the best one is returned. If the position
 * is a checkmate or stalemate for the side to move, 0 is returned. If all moves
 * lead to checkmate or stalemate for the side to move, then any of these moves
 * is returned.
 * 
 * The values set in the struct info doesn't matter since it is set to default
 * values when this function is called, and it is also constantly updated by
 * this and the main nexgamax function as the search runs.
 * 
 * The main negamax function will return -INFINITE if a checkmate for the
 * opposite side is unavoidable, and since it will also set the number of plies
 * to the mate in info, if the search type is SEARCH_TYPE_FIND_MATE we just have
 * to return the move that leads to this mate as the best move and set
 * thread_data->found_mate to true. Note that this member must not be set to
 * true by the main negamax function when a mate is found, because the mate can
 * only be considered reachable after the full search.
 * 
 * Notice that this function, along with the search functions called from it,
 * may stop at any moment if requested. If that happens while the search is
 * running it will simply return anything, so the calling function must check
 * if a stop request has been received and if so ignore whatever this function
 * returned.
 */
static Move search(int depth, struct thread_data *restrict thread_data, struct search_info *restrict info)
{
	info->depth = depth;
	info->nodes = 0;
	info->nps = 0;
	info->mate = 0;

	int alpha = -INFINITE, beta = INFINITE;
	Move best_move = 0;
	size_t len;
	Move *const moves = movegen_get_pseudo_legal_moves(thread_data->settings.position, &len);
	if (!len) {
		if (is_in_check(thread_data->settings.position))
			return -INFINITE;
		return 0;
	}

	struct timespec ts1, ts2;
	long long old_nodes;
	old_nodes = info->nodes;
	timespec_get(&ts1, TIME_UTC);

	for (size_t i = 0; i < len && thread_data->internal->total_nodes < thread_data->settings.nodes; ++i) {
		pthread_mutex_lock(&thread_data->stop_mtx);
		if (thread_data->stop) {
			pthread_mutex_unlock(&thread_data->stop_mtx);
			break;
		}
		pthread_mutex_unlock(&thread_data->stop_mtx);

		Move move = moves[i];
		if (!move_is_legal(thread_data->settings.position, move))
			continue;

		move_do(thread_data->settings.position, move);
		++thread_data->internal->ply;
		int score = -negamax(depth - 1, -beta, -alpha, thread_data, info);
		move_undo(thread_data->settings.position, move);
		--thread_data->internal->ply;

		++thread_data->internal->total_nodes;
		++info->nodes;

		if (score > alpha) {
			alpha = score;
			best_move = move;
		}
		if (thread_data->settings.type == SEARCH_TYPE_FIND_MATE && alpha == INFINITE && info->mate == thread_data->settings.moves_to_mate) {
			thread_data->internal->found_mate = true;
			best_move = move;
			break;
		}
		if (alpha >= beta)
			break;
	}

	timespec_get(&ts2, TIME_UTC);
	double dt = get_elapsed_time(&ts1, &ts2);
	const double nps = (double)(info->nodes - old_nodes) / dt;
	info->nps = (long long)round(nps);
	info->flags = INFO_FLAG_DEPTH | INFO_FLAG_NODES | INFO_FLAG_NPS;
	if (alpha == INFINITE)
		info->flags |= INFO_FLAG_MATE;
	thread_data->info_sender(info);

	if (!best_move && len) {
		for (size_t i = 0; i < len; ++i) {
			if (move_is_legal(thread_data->settings.position, moves[i]))
				best_move = moves[i];
		}
	}
	free(moves);

	return best_move;
}

/*
 * The only element of the thread_data struct that the calling thread may modify
 * is stop, to signal that the search should stop. When stop is true we return
 * the last best root move we calculated, ignoring everything that was
 * calculated after, because the search probably didn't finish.
 * After signaling stop the calling thread must not change the stop information
 * until the search thread terminates, otherwise the search functions might
 * stop while this function might not and it will continue searching.
 */
void *search_get_best_move(void *data)
{
	struct thread_data *const thread_data = data;
	pthread_mutex_lock(&thread_data->stop_mtx);
	pthread_mutex_unlock(&thread_data->stop_mtx);

	if (thread_data->settings.type == SEARCH_TYPE_INFINITE || thread_data->settings.type == SEARCH_TYPE_FIND_MATE) {
		thread_data->settings.depth = MAX_DEPTH;
		thread_data->settings.nodes = LLONG_MAX;
	} else {
		if (thread_data->settings.depth > MAX_DEPTH)
			thread_data->settings.depth = MAX_DEPTH;
	}

	struct search_info info;
	struct internal_data internal_data;
	internal_data.ply = 0;
	internal_data.total_nodes = 0;
	internal_data.found_mate = false;
	thread_data->internal = &internal_data;
	Move best_move = 0;
	for (int depth = 1; depth <= thread_data->settings.depth && internal_data.total_nodes < thread_data->settings.nodes; ++depth) {
		Move best_root_move = search(depth, thread_data, &info);
		pthread_mutex_lock(&thread_data->stop_mtx);
		if (thread_data->stop) {
			pthread_mutex_unlock(&thread_data->stop_mtx);
			break;
		}
		pthread_mutex_unlock(&thread_data->stop_mtx);
		best_move = best_root_move;
		if (thread_data->settings.type == SEARCH_TYPE_FIND_MATE && thread_data->internal->found_mate)
			break;
	}
	pthread_mutex_lock(&thread_data->stop_mtx);
	thread_data->best_move_sender(best_move);
	pthread_mutex_unlock(&thread_data->stop_mtx);

	return NULL;
}
