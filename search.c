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

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bit.h"
#include "threads.h"
#include "pos.h"
#include "move.h"
#include "movegen.h"
#include "tt.h"
#include "eval.h"
#include "rng.h"
#include "search.h"

#define MAX_DEPTH 128
#define MAX_PLY (2 * MAX_DEPTH)
#define POS_CNT_TABLE_LEN 8191
#define MAX_KILLER_MOVES 2
#define AVERAGE_GAME_LENGTH 40
#define NULL_MOVE_REDUCTION 4
#define LMR_THRESHOLD 5

struct search_data {
	int ply;
	long long nodes;
	Position *pos;
	Move killers[MAX_DEPTH + 1][MAX_KILLER_MOVES];
	/* move_made needs one extra index because ply starts at 1. */
	Move move_made[MAX_PLY + 2];
	i8 pos_cnt[POS_CNT_TABLE_LEN];
};

struct result {
	Move best;
	bool found_mate;
	long long nodes;
};

/*
 * These are the search parameters passed to the main search function by the
 * iterative deepening function, including moves played previously for the
 * threefold repetition rule.
 */
struct parameters {
	int depth;
	int mate;
	int movestogo;
	long long nodes;
	bool limited_time;
	struct timespec stop_time;
	Position *pos;
	Move *moves;
	int num_moves;
	bool *running;
	mtx_t *running_mtx;
	void (*output)(const struct info *);
};

static const int INF = SHRT_MAX;

static void inc_pos_cnt(i8 *cnt, const Position *pos)
{
	const u64 hash = hash_pos(pos);
	const size_t key = hash % POS_CNT_TABLE_LEN;
	++cnt[key];
}

static void dec_pos_cnt(i8 *cnt, const Position *pos)
{
	const u64 hash = hash_pos(pos);
	const size_t key = hash % POS_CNT_TABLE_LEN;
	--cnt[key];
}

void init_pos_cnt_table(struct search_data *data,
                        const struct parameters *params)
{
	if (!params->num_moves)
		return;
	memset(data->pos_cnt, 0, sizeof(data->pos_cnt));

	Position *prev_pos = copy_pos(data->pos);

	for (int i = params->num_moves - 1; i >= 0; --i) {
		undo_move(prev_pos, params->moves[i]);
		inc_pos_cnt(data->pos_cnt, prev_pos);
	}

	destroy_pos(prev_pos);
}

/*
 * Returns the move that was played at a ply. Negative plies index the moves
 * that were played before the search, if no moves have been played at the ply
 * then it will return 0. The ply should never be 0.
 */
static Move get_ply_move(int ply, struct search_data *data,
                         const struct parameters *params)
{
	if (ply < 0) {
		int idx = ply + params->num_moves;
		if (idx >= 0 && idx < params->num_moves)
			return params->moves[idx];
	} else {
		return data->move_made[ply];
	}

	return 0;
}

/*
 * The threefold repetition rule is enforced by comparing the current position
 * with the previous positions in the current line of the search tree and also
 * with the positions of before the search. As an optimization trick we use a
 * hash table that stores a counter of how many times each position has been
 * reached along the current line, so the position's counter should be
 * incremented each time the search function enters the node and decremented
 * after exiting. Of course, different positions may hash to the same index so
 * we still have to compare the previous positions to make sure the counter
 * counted the right position.
 *
 * Note that the first repetition is already considered a draw because the
 * opponent can usually force the second.
 */
static bool repeated(struct search_data *data, const struct parameters *params)
{
	const u64 hash = hash_pos(data->pos);
	const size_t key = hash % POS_CNT_TABLE_LEN;

	if (data->pos_cnt[key] <= 1)
		return false;

	Position *prev_pos = copy_pos(data->pos);

	for (int ply = data->ply;; --ply) {
		/* One position is skipped because it's impossible that it's the
		 * same as the current one. */
		Move move = get_ply_move(ply, data, params);
		if (!move)
			break;
		undo_move(prev_pos, move);
		--ply;

		move = get_ply_move(ply, data, params);
		if (!move)
			break;

		const Square from = get_move_origin(move);
		const Piece piece = get_piece_at(prev_pos, from);
		const PieceType pt = get_piece_type(piece);
		if (!move_is_quiet(move) || move_is_castling(move) ||
		    pt == PIECE_TYPE_PAWN)
			break;

		undo_move(prev_pos, move);
		if (pos_equal(data->pos, prev_pos)) {
			destroy_pos(prev_pos);
			return true;
		}
	}

	destroy_pos(prev_pos);

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
static Move get_next_move(const Move *restrict moves, size_t len,
                          const Move *restrict killers, Position *restrict pos)
{
	static const int capture_offset = 300;
	static const int killer_offset = 600;
	int best_score = -INF;
	size_t best_idx = 0;

	for (size_t i = 0; i < len; ++i) {
		const Move move = moves[i];
		NodeData pos_data;
		if (get_tt_entry(&pos_data, pos) &&
		    pos_data.type == NODE_TYPE_EXACT) {
			if (move == pos_data.best_move)
				return i;
		}
	}

	for (size_t i = 0; i < len; ++i) {
		const Move move = moves[i];
		int score = 0;
		if (is_killer(move, killers))
			score = killer_offset +
			        evaluate_move(move, pos);
		else if (move_is_capture(move))
			score = capture_offset +
			        evaluate_move(move, pos);
		else
			score = evaluate_move(move, pos);

		if (score > best_score) {
			best_idx = i;
			best_score = score;
		}
	}

	return best_idx;
}

/*
 * Returns the index of what seems to be the most move promising for the
 * quiescence search. Non capture moves will be ignored. If there are no more
 * capturing moves then *ended is set to true.
 */
static size_t get_next_qmove(const Move *moves, size_t len,
                             struct search_data *data, bool *ended)
{
	int best_score = -INF;
	size_t best_idx = 0;

	for (size_t i = 0; i < len; ++i) {
		const Move move = moves[i];
		if (!move_is_capture(move))
			continue;

		NodeData pos_data;
		if (get_tt_entry(&pos_data, data->pos) &&
		    pos_data.type == NODE_TYPE_EXACT) {
			if (move == pos_data.best_move)
				return i;
		}

		int score = evaluate_move(move, data->pos);
		if (score > best_score) {
			best_idx = i;
			best_score = score;
		}
	}

	if (best_score == -INF)
		*ended = true;

	return best_idx;
}

static bool is_in_check(const Position *pos)
{
	const Color c = get_side_to_move(pos);
	const Square king_sq = get_king_square(pos, c);
	return is_square_attacked(king_sq, !c, pos);
}

static bool has_legal_moves(const Move *moves, size_t len, Position *pos)
{
	for (size_t i = 0; i < len; ++i) {
		if (move_is_legal(pos, moves[i]))
			return true;
	}
	return false;
}

/*
 * We can't store mate scores as-is in the TT directly because the same position
 * can be found in different plies from the root, which means that if we use the
 * mate score in a transposition with larger ply than the node that stored the
 * entry we will get a larger score even though the mate takes longer. This
 * would make the engine choose longer mates and if it keeps choosing longer
 * mates it might never deliver mate.
 *
 * So instead we store the mate score based on the number of plies from the
 * current node. This function adjusts the mate score for the TT and if the
 * score is not mate it is left unchanged.
 */
static int score_to_ttscore(int score, int ply)
{
	if (score >= INF - MAX_PLY)
		return score + ply;
	else if (score <= -INF + MAX_PLY)
		return score - ply;
	else
		return score;
}

/*
 * This is the inverse of score_to_ttscore.
 */
static int ttscore_to_score(int score, int ply)
{
	if (score >= INF - MAX_PLY)
		return score - ply;
	else if (score <= INF + MAX_PLY)
		return score + ply;
	else
		return score;
}

/*
 * This function tries to guess if a zugzwang position is likely for the side
 * to move by using the fact that zigzwang positions usually happen when the
 * side to move has only the king to protect the pawns.
 */
static bool is_zugzwang_likely(const Position *pos)
{
	const Color color = get_side_to_move(pos);
	const u64 color_bb = get_color_bitboard(pos, color);
	const Piece pawn = create_piece(PIECE_TYPE_PAWN, color);
	const u64 pawn_bb = get_piece_bitboard(pos, pawn);
	const Piece king = create_piece(PIECE_TYPE_KING, color);
	const u64 king_bb = get_piece_bitboard(pos, king);
	return (color_bb & (pawn_bb | king_bb)) == color_bb;
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
	mtx_lock(params->running_mtx);
	if (data->nodes >= params->nodes || data->ply > MAX_PLY)
		*params->running = false;
	if (!*params->running || data->nodes >= params->nodes) {
		mtx_unlock(params->running_mtx);
		return alpha;
	}
	mtx_unlock(params->running_mtx);

	++data->nodes;
	++info->nodes;

	if (repeated(data, params))
		return 0;

	NodeData pos_data;
	if (get_tt_entry(&pos_data, data->pos) && pos_data.depth >= depth) {
		const NodeType type = pos_data.type;
		const int score = ttscore_to_score(pos_data.score, data->ply);
		if (type == NODE_TYPE_EXACT ||
		    (type == NODE_TYPE_CUT && score >= beta) ||
		    (type == NODE_TYPE_ALPHA_UNCHANGED && score <= alpha)) {
			return score;
		}
	}

	NodeType type = NODE_TYPE_ALPHA_UNCHANGED;
	int best_score = evaluate(data->pos);;
	Move best_move = 0;

	/* Only return early if not in check otherwise checkmates won't be
	 * detected and the stand-pat value would be returned instead. */
	if (best_score >= beta && !is_in_check(data->pos))
		return best_score;
	if (best_score > alpha)
		alpha = best_score;

	bool has_legal = false;
	size_t len = 0;
	Move *const moves = get_pseudo_legal_moves(data->pos, &len);
	for (size_t i = 0; i < len; ++i) {
		bool ended = false;
		const size_t j = i + get_next_qmove(moves + i, len - i, data,
		                                    &ended);
		/* Because we skip quiet moves, when no legal moves are
		 * found we have to check if the moves skipped are legal
		 * otherwise the node will be considered a checkmate or
		 * stalemate even if it isn't. */
		if (ended && !has_legal) {
			has_legal = has_legal_moves(moves + i, len - i,
			                            data->pos);
			break;
		} else if (ended) {
			break;
		}
		const Move most_promising = moves[j];
		const Move tmp = moves[i];
		moves[i] = most_promising;
		moves[j] = tmp;
		const Move move = most_promising;

		if (move_is_legal(data->pos, move))
			has_legal = true;
		if (!move_is_legal(data->pos, move) || !move_is_capture(move))
			continue;

		do_move(data->pos, move);
		inc_pos_cnt(data->pos_cnt, data->pos);
		++data->ply;
		data->move_made[data->ply] = move;
		prefetch_tt();
		int score = -qsearch(depth - 1, -beta, -alpha, data, info,
		                     params);
		dec_pos_cnt(data->pos_cnt, data->pos);
		undo_move(data->pos, move);
		--data->ply;

		if (score > best_score) {
			best_score = score;
			best_move = move;
			if (score > alpha) {
				alpha = best_score;
				type = NODE_TYPE_EXACT;
			}
		}
		if (alpha >= beta) {
			type = NODE_TYPE_CUT;
			break;
		}
	}
	if (!best_move && has_legal)
		best_move = moves[0];

	free(moves);

	if (!has_legal) {
		if (is_in_check(data->pos)) {
			best_score = -INF + data->ply;
			alpha = best_score;
		} else {
			best_score = 0;
			alpha = best_score;
		}
	}

	if (*params->running) {
		init_tt_entry(&pos_data,
		              score_to_ttscore(best_score, data->ply), depth,
		              type, best_move, data->pos);
		store_tt_entry(&pos_data);
	}

	return best_score;
}

/*
 * This is the main negamax function with alpha beta pruning, it returns the
 * best score achievable for a position. It returns a value less than or equal
 * to -INF or 0 if the best outcome calculated is a checkmate or stalemate,
 * respectively, which means that such outcome is unavoidable. In the case of
 * checkmate it will additionally set info->mate to the number of moves
 * (full moves, not plies) for checkmate.
 */
static int negamax(int depth, int alpha, int beta, struct search_data *data,
                   struct info *info, const struct parameters *params)
{
	mtx_lock(params->running_mtx);
	/* Only check time each 8192 nodes to avoid making system calls and
	 * slowing down the search. */
	if (data->nodes % 8192 == 0) {
		struct timespec now;
		timespec_get(&now, TIME_UTC);
		if (params->limited_time) {
			if (now.tv_sec > params->stop_time.tv_sec ||
			    (now.tv_sec == params->stop_time.tv_sec &&
			     now.tv_nsec >= params->stop_time.tv_nsec))
				*params->running = false;
		}
	}
	if (data->nodes >= params->nodes || data->ply > MAX_PLY)
		*params->running = false;
	if (!*params->running) {
		mtx_unlock(params->running_mtx);
		return alpha;
	}
	mtx_unlock(params->running_mtx);

	++data->nodes;
	++info->nodes;

	if (repeated(data, params))
		return 0;

	NodeData pos_data;
	if (get_tt_entry(&pos_data, data->pos) && pos_data.depth >= depth) {
		const NodeType type = pos_data.type;
		const int score = ttscore_to_score(pos_data.score, data->ply);
		if (type == NODE_TYPE_EXACT ||
		    (type == NODE_TYPE_CUT && score >= beta) ||
		    (type == NODE_TYPE_ALPHA_UNCHANGED && score <= alpha)) {
			return score;
		}
	}
	if (!depth) {
		/* The quiescence search will count this node so we decrement
		 * the current count to avoid counting it twice. */
		--data->nodes;
		--info->nodes;
		return qsearch(depth, alpha, beta, data, info, params);
	}

	NodeType type = NODE_TYPE_ALPHA_UNCHANGED;

	bool in_check = is_in_check(data->pos);

	/* Null move pruning */
	if (!in_check && !is_zugzwang_likely(data->pos) &&
	    depth > NULL_MOVE_REDUCTION) {
		const int new_depth = depth - NULL_MOVE_REDUCTION;

		do_null_move(data->pos);
		const int score = -negamax(new_depth, -beta, -alpha, data, info,
		                           params);
		undo_null_move(data->pos);

		if (score >= beta)
			return beta;
	}

	int best_score = -INF;
	Move best_move = 0;
	bool has_legal = 0;
	size_t len = 0;
	Move *const moves = get_pseudo_legal_moves(data->pos, &len);

	int eval = evaluate(data->pos);

	for (size_t i = 0; i < len; ++i) {
		/* Lazily sort moves instead of doing it all at once, this way
		 * we avoid wasting time sorting moves of branches that are
		 * pruned. */
		const size_t j = i + get_next_move(moves + i, len - i,
		                                   data->killers[depth],
		                                   data->pos);
		const Move most_promising = moves[j];
		const Move tmp = moves[i];
		moves[i] = most_promising;
		moves[j] = tmp;
		const Move move = most_promising;

		if (!move_is_legal(data->pos, move))
			continue;
		has_legal = true;

		/* Futility pruning. If the static position score plus some
		 * safety margin is not enough to raise alpha, then skip all the
		 * next moves. The safety margin is proportional to the depth
		 * with proportionality constant equal to 1.5 centipawns so that
		 * upper nodes are less likely to be pruned. */
		if (move_is_quiet(move) && !in_check &&
		    abs(alpha) < INF - MAX_PLY && abs(beta) < INF - MAX_PLY) {
			if (eval + 175 * depth <= alpha) {
				free(moves);
				return eval;
			}
		}

		/* Reverse futility pruning. It works similarly to the regular
		 * futility pruning, but it's based on beta instead of alpha.
		 * If the static position score minus some margin can beat beta,
		 * then skip all next moves because the full evaluation will
		 * most likely beat beta. */
		if (move_is_quiet(move) && !in_check &&
		    abs(alpha) < INF - MAX_PLY && abs(beta) < INF - MAX_PLY) {
			if (eval - 175 * depth >= beta) {
				free(moves);
				return eval - 175 * depth;
			}
		}

		do_move(data->pos, move);
		inc_pos_cnt(data->pos_cnt, data->pos);
		++data->ply;
		data->move_made[data->ply] = move;
		prefetch_tt();
		int score = -negamax(depth - 1, -beta, -alpha, data, info,
		                     params);
		dec_pos_cnt(data->pos_cnt, data->pos);
		undo_move(data->pos, move);
		--data->ply;

		if (score > best_score) {
			best_score = score;
			best_move = move;
			if (score > alpha) {
				alpha = best_score;
				type = NODE_TYPE_EXACT;
			}
		}
		if (alpha >= beta) {
			if (!move_is_capture(move))
				store_killer(data->killers[depth], move);
			type = NODE_TYPE_CUT;
			break;
		}
	}
	if (!best_move && has_legal)
		best_move = moves[0];

	free(moves);

	if (!has_legal) {
		if (is_in_check(data->pos)) {
			best_score = -INF + data->ply;
			alpha = best_score;
		} else {
			best_score = 0;
			alpha = best_score;
		}
	}

	if (*params->running) {
		init_tt_entry(&pos_data,
		              score_to_ttscore(best_score, data->ply), depth,
		              type, best_move, data->pos);
		store_tt_entry(&pos_data);
	}

	return best_score;
}

/*
 * This function must be called before any searches are performed, it
 * initializes all the tables and call other initialization functions. It does
 * not need to be called between every search, only once (unless it's desirable
 * to start a new search with a clean state.)
 *
 * tt_size is the transposition table size in mebibytes.
 */
void search_init(int tt_size)
{
	movegen_init();
	tt_init(tt_size);
	eval_init();
}

void clear_hash_table(void)
{
	clear_tt();
}

void resize_hash_table(int tt_size)
{
	resize_tt(tt_size);
}

void search_free(void)
{
	tt_free();
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
 * The main negamax function will return -INF if a checkmate for the
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
	data.pos = copy_pos(params->pos);
	memset(data.move_made, 0, sizeof(data.move_made));
	memset(data.killers, 0, sizeof(data.killers));
	init_pos_cnt_table(&data, params);

	inc_pos_cnt(data.pos_cnt, data.pos);

	struct info info;
	info.depth = params->depth;
	info.nodes = info.nps = info.mate = 0;

	struct result result;
	result.found_mate = false;
	result.best = result.nodes = 0;

	int alpha = -INF, beta = INF;

	size_t len;
	Move *const moves = get_pseudo_legal_moves(params->pos, &len);

	struct timespec ts1, ts2;
	long long old_nodes = info.nodes;

	timespec_get(&ts1, TIME_UTC);
	for (size_t i = 0; i < len; ++i) {
		mtx_lock(params->running_mtx);
		if (data.nodes >= params->nodes)
			*params->running = false;
		if (!*params->running) {
			mtx_unlock(params->running_mtx);
			break;
		}
		mtx_unlock(params->running_mtx);

		Move move = moves[i];
		if (!move_is_legal(data.pos, move))
			continue;

		do_move(data.pos, move);
		++data.ply;
		inc_pos_cnt(data.pos_cnt, data.pos);
		data.move_made[data.ply] = move;
		prefetch_tt();
		int score = -negamax(params->depth - 1, -beta, -alpha, &data,
		                     &info, params);
		dec_pos_cnt(data.pos_cnt, data.pos);
		--data.ply;
		undo_move(data.pos, move);

		if (score > alpha) {
			alpha = score;
			result.best = move;
		}
		if (params->mate && alpha >= INF - MAX_PLY) {
			result.found_mate = true;
			result.best = move;
			break;
		}
	}
	timespec_get(&ts2, TIME_UTC);

	result.nodes = data.nodes;

	double dt = get_elapsed_time(&ts1, &ts2);
	const double nps = (double)(info.nodes - old_nodes) * 1000 / dt;
	info.nps = (long long)round(nps);
	info.time = (long long)round(dt);
	info.cp = alpha;
	info.flags = INFO_FLAG_DEPTH | INFO_FLAG_NODES | INFO_FLAG_NPS |
	             INFO_FLAG_TIME;
	if (alpha >= INF - MAX_PLY) {
		info.mate = (INF - alpha + 1) / 2;
		info.flags |= INFO_FLAG_MATE;
	} else if (alpha <= -INF + MAX_PLY) {
		info.mate = -(INF + alpha + 1) / 2;
		info.flags |= INFO_FLAG_MATE;
	} else {
		info.flags |= INFO_FLAG_CP;
	}
	if (!*params->running)
		info.flags |= INFO_FLAG_LBOUND;
	params->output(&info);

	if (!result.best && len) {
		for (size_t i = 0; i < len; ++i) {
			if (move_is_legal(data.pos, moves[i]))
				result.best = moves[i];
		}
	}
	free(moves);

	dec_pos_cnt(data.pos_cnt, data.pos);
	destroy_pos(data.pos);

	return result;
}

static void perft(int depth, const Position *pos,
                  void (*output)(const struct info *))
{
	struct info info;
	struct timespec ts1, ts2;

	Position *const pos_copy = copy_pos(pos);

	timespec_get(&ts1, TIME_UTC);
	info.nodes = movegen_perft(pos_copy, depth);
	timespec_get(&ts2, TIME_UTC);

	double dt = get_elapsed_time(&ts1, &ts2);
	const double nps = (double)info.nodes / dt;
	info.nps = round(nps);

	info.flags = INFO_FLAG_NODES | INFO_FLAG_NPS;
	output(&info);

	destroy_pos(pos_copy);
}

/*
 * Receives a position and the total time left in milliseconds and returns the
 * amount of time the search can use, also in milliseconds.
 * 
 * We need to divide the time we have available among the moves that will be
 * played throughout the game, but the number of future moves depends on how
 * many moves have already been played. Using the current game phase we can use
 * a linear interpolation between the average number of moves for a full chess
 * game and a safe minimum number of moves we always want to have available,
 * then we can divide the time we have left by the interpolation value at the
 * current phase to obtain the time we should spend on searching the next move.
 *
 * If movestogo is set then we just use it instead of the average number of
 * moves. In particular, if movestogo is 1 then we can use all the remaining
 * time since the next move will start the next time control. Note, however,
 * that it is not safe to actually use all the remaining time since the engine
 * may take longer to finish calculating, therefore we can only use a portion of
 * it.
 * 
 * It is clear that the more time we have the more time it is safe to use. For
 * example, if we only have 1 second left using 99% of it is not safe since the
 * engine would only have a buffer of less than 1 millisecond between detecting
 * time is up and sending the move. The following mathematical function maps a
 * time in milliseconds to a value between 0 and 1.
 * 
 * f(x) = (x / 1000)^1.1 / (x / 1000 + 1)^1.1
 * 
 * The division by 1000 effectively converts the time to seconds, scaling down
 * the function. Obviously the final search time calculated might be 0, which
 * means we're screwed and there's no time for search.
 */
static long long compute_search_time(const Position *pos, long long time,
                                     int movestogo)
{
	if (movestogo == 1) {
		double factor = pow(time / 1000., 1.1);
		factor /= pow(time / 1000. + 1., 1.1);
		return time * factor;
	}
	const int phase = get_phase(pos);
	const size_t max = movestogo && movestogo < AVERAGE_GAME_LENGTH ?
	                   movestogo : AVERAGE_GAME_LENGTH;
	const double divisor = (max * (256 - phase) + 8 * phase) / 256;
	const double search_time = time / divisor;
	return (long long)search_time;
}

/*
 * Adds time milliseconds to ts.
 */
static void add_time(struct timespec *ts, long long time)
{
	time_t sec = (time_t)floor(time / 1e3);
	long nsec = (long)floor((time / 1e3 - sec) * 1e9);
	ts->tv_sec += sec;
	ts->tv_nsec += nsec;
	if (ts->tv_nsec >= 1000000000L) {
		++ts->tv_sec;
		ts->tv_nsec %= 1000000000L;
	}
}

static void init_params(struct parameters *params,
                        const struct search_argument *arg)
{
	params->pos = arg->pos;
	params->mate = arg->mate;
	params->movestogo = arg->movestogo;
	params->moves = arg->moves;
	params->num_moves = arg->num_moves;
	params->running_mtx = arg->running_mtx;
	params->running = arg->running;
	params->output = arg->info_sender;
	params->depth = 1;

	if (arg->infinite || arg->mate)
		params->nodes = LLONG_MAX;
	else
		params->nodes = arg->nodes;

	const Color color = get_side_to_move(arg->pos);

	if (arg->movetime) {
		params->limited_time = true;

		const long long movetime = arg->movetime;

		timespec_get(&params->stop_time, TIME_UTC);
		add_time(&params->stop_time, movetime);
	} else if (arg->time[color]) {
		params->limited_time = true;

		const long long total_time = arg->time[color] + arg->inc[color];

		const long long search_time = compute_search_time(params->pos,
			total_time, arg->movestogo);
		timespec_get(&params->stop_time, TIME_UTC);
		add_time(&params->stop_time, search_time);
	} else {
		params->limited_time = false;
	}
}

/*
 * When stop is true or the maximum number of nodes is reached we return the
 * last best root move we calculated, ignoring everything that was calculated
 * after, because this iteration's search probably didn't finish.
 * After signaling stop the calling thread must not change the stop information
 * until the search thread terminates, otherwise the search functions might
 * stop while this function might not and it will continue searching.
 *
 * If the position is already checkmate, this function will just return 0
 * without searching.
 * 
 * This function prints the best move for the last position in the position list
 * in the search_settings struct and the other positions are used to enforce the
 * threefold repetition rule so they should be in the order they happened in the
 * game.
 */
int run_search(void *data)
{
	const struct search_argument *const arg = data;

	Move best_move;
	size_t len;
	Move *const moves = get_pseudo_legal_moves(arg->pos, &len);
	if (!moves) {
		free(moves);
		return 0;
	}

	bool has_legal = false;
	for (size_t i = 0; i < len; ++i) {
		if (move_is_legal(arg->pos, moves[i])) {
			has_legal = true;
			best_move = moves[i];
		}
	}
	if (!has_legal && is_in_check(arg->pos)) {
		free(moves);
		return 0;
	}

	if (arg->perft) {
		perft(arg->perft, arg->pos, arg->info_sender);
		mtx_lock(arg->running_mtx);
		*arg->running = false;
		mtx_unlock(arg->running_mtx);
		return 0;
	}

	struct parameters params;
	init_params(&params, arg);

	long long nodes = params.nodes;
	int max_depth = arg->depth;
	if (arg->infinite || arg->mate) {
		max_depth = MAX_DEPTH;
	} else {
		if (arg->depth > MAX_DEPTH)
			max_depth = MAX_DEPTH;
	}

	for (int depth = 1; depth <= max_depth && nodes > 0; ++depth) {
		params.depth = depth;
		params.nodes = nodes;

		struct result result = search(&params);

		nodes -= result.nodes;

		mtx_lock(arg->running_mtx);
		if (!*arg->running) {
			mtx_unlock(arg->running_mtx);
			break;
		}
		mtx_unlock(arg->running_mtx);

		best_move = result.best;
		if (params.mate && result.found_mate)
			break;
	}
	arg->best_move_sender(best_move);

	mtx_lock(arg->running_mtx);
	*arg->running = false;
	mtx_unlock(arg->running_mtx);

	return 0;
}
