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

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#include "bit.h"
#include "pos.h"
#include "move.h"
#include "movegen.h"
#include "eval.h"
#include "rng.h"

enum factor {
	MATERIAL,
	MOBILITY,
	POSITIONING,
};

struct score {
	int mg;
	int eg;
};

static const int mg_pawn_sq_table[64] = {
	  0,   0,   0,   0,   0,   0,  0,   0,
	 98, 134,  61,  95,  68, 126, 34, -11,
	 -6,   7,  26,  31,  65,  56, 25, -20,
	-14,  13,   6,  21,  23,  12, 17, -23,
	-27,  -2,  -5,  12,  17,   6, 10, -25,
	-26,  -4,  -4, -10,   3,   3, 33, -12,
	-35,  -1, -20, -23, -15,  24, 38, -22,
	  0,   0,   0,   0,   0,   0,  0,   0,
};

static const int eg_pawn_sq_table[64] = {
	  0,   0,   0,   0,   0,   0,   0,   0,
	178, 173, 158, 134, 147, 132, 165, 187,
	 94, 100,  85,  67,  56,  53,  82,  84,
	 32,  24,  13,   5,  -2,   4,  17,  17,
	 13,   9,  -3,  -7,  -7,  -8,   3,  -1,
	  4,   7,  -6,   1,   0,  -5,  -1,  -8,
	 13,   8,   8,  10,  13,   0,   2,  -7,
	  0,   0,   0,   0,   0,   0,   0,   0,
};

static const int mg_knight_sq_table[64] = {
	-167, -89, -34, -49,  61, -97, -15, -107,
	 -73, -41,  72,  36,  23,  62,   7,  -17,
	 -47,  60,  37,  65,  84, 129,  73,   44,
	  -9,  17,  19,  53,  37,  69,  18,   22,
	 -13,   4,  16,  13,  28,  19,  21,   -8,
	 -23,  -9,  12,  10,  19,  17,  25,  -16,
	 -29, -53, -12,  -3,  -1,  18, -14,  -19,
	-105, -21, -58, -33, -17, -28, -19,  -23,
};

static const int eg_knight_sq_table[64] = {
	-58, -38, -13, -28, -31, -27, -63, -99,
	-25,  -8, -25,  -2,  -9, -25, -24, -52,
	-24, -20,  10,   9,  -1,  -9, -19, -41,
	-17,   3,  22,  22,  22,  11,   8, -18,
	-18,  -6,  16,  25,  16,  17,   4, -18,
	-23,  -3,  -1,  15,  10,  -3, -20, -22,
	-42, -20, -10,  -5,  -2, -20, -23, -44,
	-29, -51, -23, -15, -22, -18, -50, -64,
};

static const int mg_bishop_sq_table[64] = {
	-29,   4, -82, -37, -25, -42,   7,  -8,
	-26,  16, -18, -13,  30,  59,  18, -47,
	-16,  37,  43,  40,  35,  50,  37,  -2,
	 -4,   5,  19,  50,  37,  37,   7,  -2,
	 -6,  13,  13,  26,  34,  12,  10,   4,
	  0,  15,  15,  15,  14,  27,  18,  10,
	  4,  15,  16,   0,   7,  21,  33,   1,
	-33,  -3, -14, -21, -13, -12, -39, -21,
};

static const int eg_bishop_sq_table[64] = {
	-14, -21, -11,  -8, -7,  -9, -17, -24,
	 -8,  -4,   7, -12, -3, -13,  -4, -14,
	  2,  -8,   0,  -1, -2,   6,   0,   4,
	 -3,   9,  12,   9, 14,  10,   3,   2,
	 -6,   3,  13,  19,  7,  10,  -3,  -9,
	-12,  -3,   8,  10, 13,   3,  -7, -15,
	-14, -18,  -7,  -1,  4,  -9, -15, -27,
	-23,  -9, -23,  -5, -9, -16,  -5, -17,
};

static const int mg_rook_sq_table[64] = {
	 32,  42,  32,  51, 63,  9,  31,  43,
	 27,  32,  58,  62, 80, 67,  26,  44,
	 -5,  19,  26,  36, 17, 45,  61,  16,
	-24, -11,   7,  26, 24, 35,  -8, -20,
	-36, -26, -12,  -1,  9, -7,   6, -23,
	-45, -25, -16, -17,  3,  0,  -5, -33,
	-44, -16, -20,  -9, -1, 11,  -6, -71,
	-19, -13,   1,  17, 16,  7, -37, -26,
};

static const int eg_rook_sq_table[64] = {
	13, 10, 18, 15, 12,  12,   8,   5,
	11, 13, 13, 11, -3,   3,   8,   3,
	 7,  7,  7,  5,  4,  -3,  -5,  -3,
	 4,  3, 13,  1,  2,   1,  -1,   2,
	 3,  5,  8,  4, -5,  -6,  -8, -11,
	-4,  0, -5, -1, -7, -12,  -8, -16,
	-6, -6,  0,  2, -9,  -9, -11,  -3,
	-9,  2,  3, -1, -5, -13,   4, -20,
};

static const int mg_queen_sq_table[64] = {
	-28,   0,  29,  12,  59,  44,  43,  45,
	-24, -39,  -5,   1, -16,  57,  28,  54,
	-13, -17,   7,   8,  29,  56,  47,  57,
	-27, -27, -16, -16,  -1,  17,  -2,   1,
	 -9, -26,  -9, -10,  -2,  -4,   3,  -3,
	-14,   2, -11,  -2,  -5,   2,  14,   5,
	-35,  -8,  11,   2,   8,  15,  -3,   1,
	 -1, -18,  -9,  10, -15, -25, -31, -50,
};

static const int eg_queen_sq_table[64] = {
	 -9,  22,  22,  27,  27,  19,  10,  20,
	-17,  20,  32,  41,  58,  25,  30,   0,
	-20,   6,   9,  49,  47,  35,  19,   9,
	  3,  22,  24,  45,  57,  40,  57,  36,
	-18,  28,  19,  47,  31,  34,  39,  23,
	-16, -27,  15,   6,   9,  17,  10,   5,
	-22, -23, -30, -16, -16, -23, -36, -32,
	-33, -28, -22, -43,  -5, -32, -20, -41,
};

static const int mg_king_sq_table[64] = {
	-65,  23,  16, -15, -56, -34,   2,  13,
	 29,  -1, -20,  -7,  -8,  -4, -38, -29,
	 -9,  24,   2, -16, -20,   6,  22, -22,
	-17, -20, -12, -27, -30, -25, -14, -36,
	-49,  -1, -27, -39, -46, -44, -33, -51,
	-14, -14, -22, -46, -44, -30, -15, -27,
	  1,   7,  -8, -64, -43, -16,   9,   8,
	-15,  36,  12, -54,   8, -28,  24,  14,
};

static const int eg_king_sq_table[64] = {
	-74, -35, -18, -18, -11,  15,   4, -17,
	-12,  17,  14,  17,  17,  38,  23,  11,
	 10,  17,  23,  15,  20,  45,  44,  13,
	 -8,  22,  24,  27,  26,  33,  26,   3,
	-18,  -4,  21,  24,  27,  23,   9, -11,
	-19,  -3,  11,  21,  23,  16,   7,  -9,
	-27, -11,   4,  13,  14,   4,  -5, -17,
	-53, -34, -21, -11, -28, -14, -24, -43
};

/*
 * Indexed by color, piece type and square.
 */
static struct score sq_tables[2][6][64];

/*
 * These are the intrinsic point value of each piece in the centipawn scale, in
 * increasing sorted order.
 */
static const int point_value[] = {
	[PIECE_TYPE_PAWN] = 100,
	[PIECE_TYPE_KNIGHT] = 325,
	[PIECE_TYPE_BISHOP] = 350,
	[PIECE_TYPE_ROOK] = 500,
	[PIECE_TYPE_QUEEN] = 1000,
	[PIECE_TYPE_KING] = 10000,
};

static void init_sq_tables(void)
{
	const int *const mg_tables[6] = {
		[PIECE_TYPE_PAWN  ] = mg_pawn_sq_table,
		[PIECE_TYPE_KNIGHT] = mg_knight_sq_table,
		[PIECE_TYPE_BISHOP] = mg_bishop_sq_table,
		[PIECE_TYPE_ROOK  ] = mg_rook_sq_table,
		[PIECE_TYPE_QUEEN ] = mg_queen_sq_table,
		[PIECE_TYPE_KING  ] = mg_king_sq_table,
	};
	const int *const eg_tables[6] = {
		[PIECE_TYPE_PAWN  ] = eg_pawn_sq_table,
		[PIECE_TYPE_KNIGHT] = eg_knight_sq_table,
		[PIECE_TYPE_BISHOP] = eg_bishop_sq_table,
		[PIECE_TYPE_ROOK  ] = eg_rook_sq_table,
		[PIECE_TYPE_QUEEN ] = eg_queen_sq_table,
		[PIECE_TYPE_KING  ] = eg_king_sq_table,
	};

	for (PieceType pt = PIECE_TYPE_PAWN; pt <= PIECE_TYPE_KING; ++pt) {
		for (Square sq = A1; sq <= H8; ++sq) {
			sq_tables[COLOR_WHITE][pt][sq ^ 56].mg = mg_tables[pt][sq];
			sq_tables[COLOR_WHITE][pt][sq ^ 56].eg = eg_tables[pt][sq];
			sq_tables[COLOR_BLACK][pt][sq].mg = mg_tables[pt][sq];
			sq_tables[COLOR_BLACK][pt][sq].eg = eg_tables[pt][sq];
		}
	}
}

/*
 * In the MVV-LVA heuristic the most valuable attacker in captures is the least
 * valuable piece, so this function returns the value of an attacker in
 * centipawns following this principle.
 */
static int get_attacker_value(Piece piece)
{
	const size_t len = sizeof(point_value) / sizeof(point_value[0]);
	const PieceType pt = get_piece_type(piece);

	return point_value[(len - 1) - pt];
}

static int get_captured_piece_value(Piece piece)
{
	const PieceType pt = get_piece_type(piece);
	return point_value[pt];
}

/*
 * Returns the bitboard of the least valuable pieces of the current side to move
 * attacking a square. If there are no pieces attacking the square then it
 * returns 0.
 */
static u64 get_least_valuable_attackers(Square sq, const Position *pos)
{
	const u64 attackers = get_attackers(sq, pos);

	u64 ret = 0;
	Piece least_piece = PIECE_NONE;
	for (PieceType pt = PIECE_TYPE_PAWN; pt <= PIECE_TYPE_KING; ++pt) {
		const Piece piece = create_piece(pt, get_side_to_move(pos));
		const u64 bb = get_piece_bitboard(pos, piece);
		if (bb & attackers) {
			if (!ret || get_attacker_value(piece) >
			    get_captured_piece_value(least_piece)) {
				least_piece = piece;
				ret = bb & attackers;
			}
		}
	}
	return ret;
}

static struct score compute_mvv_lva(Move move, const Position *pos)
{
	const MoveType move_type = get_move_type(move);
	const Square target = get_target_square(move);
	const Square origin = get_origin_square(move);
	const Piece attacker = get_piece_at(pos, origin);
	const Color attacker_color = get_piece_color(attacker);
	Piece attacked;
	if (move_type == MOVE_EP_CAPTURE)
		attacked = attacker_color == COLOR_WHITE ? PIECE_BLACK_PAWN :
		                                           PIECE_WHITE_PAWN;
	else
		attacked = get_piece_at(pos, target);
	const int tmp = get_captured_piece_value(attacked) +
	                get_attacker_value(attacker);
	struct score score = {
		.mg = tmp,
		.eg = tmp,
	};

	return score;
}

/*
 * Returns the total score in centipawns of a series of exchanges on a square
 * starting with the least valued pieces. If there are many of the same pieces
 * then any of them is used.
 * The score starts at 0 and is incremented by the value of captured enemy
 * pieces and decremented by the captured friendly pieces.
 * So if after a capture this function returns a value smaller than the captured
 * piece's value the exchange was favorable for the side that captured, if it is
 * bigger then it was favorable for the opposide side, and if it is equal then
 * the exchange is fair. If there are no attacking pieces then the result is
 * just 0.
 * Note that since this function is supposed to be called after a capture, it
 * does not need to handle en passant since en passant is not possible after a
 * capture. Only call this function if the previous move was a capture because
 * it expects an enemy piece at the target square.
 */
static int evaluate_exchange(Square target, Position *pos)
{
	int value = 0;
	const u64 attackers = get_least_valuable_attackers(target, pos);
	if (attackers) {
		const Square from = get_ls1b(attackers);
		const Piece piece = get_piece_at(pos, from);
		MoveType move_type = MOVE_CAPTURE;
		if ((piece == PIECE_WHITE_PAWN && get_rank(target) == RANK_8) ||
		    (piece == PIECE_BLACK_PAWN && get_rank(target) == RANK_1))
			move_type = MOVE_QUEEN_PROMOTION_CAPTURE;
		const Move move = create_move(from, target, move_type);
		do_move(pos, move);
		const Piece cap_piece = get_captured_piece(pos);
		const int score = get_captured_piece_value(cap_piece) - evaluate_exchange(target, pos);
		if (score > 0)
			value = score;
		undo_move(pos, move);
	}
	return value;
}

static int compute_material(const Position *pos)
{
	const Color c = get_side_to_move(pos);
	const int P = create_piece(PIECE_TYPE_PAWN  , c),
	          N = create_piece(PIECE_TYPE_KNIGHT, c),
	          R = create_piece(PIECE_TYPE_ROOK  , c),
	          B = create_piece(PIECE_TYPE_BISHOP, c),
	          Q = create_piece(PIECE_TYPE_QUEEN , c),
	          K = create_piece(PIECE_TYPE_KING  , c);
	const int p = create_piece(PIECE_TYPE_PAWN  , !c),
	          n = create_piece(PIECE_TYPE_KNIGHT, !c),
	          r = create_piece(PIECE_TYPE_ROOK  , !c),
	          b = create_piece(PIECE_TYPE_BISHOP, !c),
	          q = create_piece(PIECE_TYPE_QUEEN , !c),
	          k = create_piece(PIECE_TYPE_KING  , !c);
	const int num_P = get_number_of_pieces(pos, P),
	          num_N = get_number_of_pieces(pos, N),
	          num_R = get_number_of_pieces(pos, R),
	          num_B = get_number_of_pieces(pos, B),
	          num_Q = get_number_of_pieces(pos, Q),
	          num_K = get_number_of_pieces(pos, K);

	const int num_p = get_number_of_pieces(pos, p),
	          num_n = get_number_of_pieces(pos, n),
	          num_r = get_number_of_pieces(pos, r),
	          num_b = get_number_of_pieces(pos, b),
	          num_q = get_number_of_pieces(pos, q),
	          num_k = get_number_of_pieces(pos, k);
	const int material = point_value[PIECE_TYPE_PAWN]   * (num_P - num_p)
	                   + point_value[PIECE_TYPE_KNIGHT] * (num_N - num_n)
	                   + point_value[PIECE_TYPE_ROOK]   * (num_R - num_r)
	                   + point_value[PIECE_TYPE_BISHOP] * (num_B - num_b)
                           + point_value[PIECE_TYPE_QUEEN]  * (num_Q - num_q)
	                   + point_value[PIECE_TYPE_KING]   * (num_K - num_k);

	return material;
}
static struct score evaluate_capture(Move move, Position *pos)
{
	const Square origin = get_origin_square(move);
	const Square target = get_target_square(move);
	const Piece piece = get_piece_at(pos, origin);
	const PieceType pt = get_piece_type(piece);
	const MoveType move_type = get_move_type(move);

	PieceType cap_piece_type;
	if (move_type == MOVE_EP_CAPTURE) {
		cap_piece_type = PIECE_TYPE_PAWN;
	} else {
		Piece cap_piece = get_piece_at(pos, target);
		cap_piece_type = get_piece_type(cap_piece);
	}

	struct score score = compute_mvv_lva(move, pos);
	if (point_value[pt] < point_value[PIECE_TYPE_ROOK] &&
	    point_value[cap_piece_type] >= point_value[PIECE_TYPE_ROOK]) {
		score.mg += point_value[cap_piece_type];
		score.eg += point_value[cap_piece_type];
		if (move_is_promotion(move)) {
			score.mg += point_value[PIECE_TYPE_QUEEN];
			score.eg += point_value[PIECE_TYPE_QUEEN];
		}
	} else {
		do_move(pos, move);
		Piece cap_piece = get_captured_piece(pos);
		const int tmp = get_captured_piece_value(cap_piece) -
		                evaluate_exchange(target, pos);
		score.mg += tmp;
		score.eg += tmp;
		undo_move(pos, move);
	}

	return score;
}

/*
 * Calculates the smallest Chebyshev distance from the king to a pawn. This is
 * useful in the end game where rook attacks are a major threat. The distance
 * ranges from 0 to 5 where 0 is closest to the king and 5 farthest.
 */
static int get_smallest_pawn_distance(const Position *pos, Color color)
{
	const Square king_sq = get_king_square(pos, color);
	const File king_file = get_file(king_sq);
	const Rank king_rank = get_rank(king_sq);
	const Piece pawn = create_piece(PIECE_TYPE_PAWN, color);

	u64 pawn_bb = get_piece_bitboard(pos, pawn) &
	              get_color_bitboard(pos, color);
	int dist = 6;
	while (pawn_bb) {
		const Square sq = unset_ls1b(&pawn_bb);
		const File file = get_file(sq);
		const Rank rank = get_rank(sq);
		const int file_dist = abs((int)king_file - (int)file);
		const int rank_dist = abs((int)king_rank - (int)rank);
		const int tmp = file_dist > rank_dist ? file_dist : rank_dist;
		if (tmp < dist)
			dist = tmp;
	}
	return dist - 1;
}

int evaluate(const Position *pos)
{
	const Color color = get_side_to_move(pos);
	const int phase = get_phase(pos);

	struct score score = {0, 0};
	for (Square sq = A1; sq <= H8; ++sq) {
		const Piece piece = get_piece_at(pos, sq);
		if (piece == PIECE_NONE)
			continue;
		const PieceType pt = get_piece_type(piece);

		if (get_piece_color(piece) == color) {
			score.mg += sq_tables[color][pt][sq].mg;
			score.eg += sq_tables[color][pt][sq].eg;
		} else {
			score.mg -= sq_tables[!color][pt][sq].mg;
			score.eg -= sq_tables[!color][pt][sq].eg;
		}
	}

	const int material = compute_material(pos);
	score.mg += material;
	score.eg += material;

	score.eg += 16 * get_smallest_pawn_distance(pos, !color);
	score.eg -= 16 * get_smallest_pawn_distance(pos, color);

	return ((score.mg * (256 - phase)) + (score.eg * phase)) / 256;
}

/*
 * This function estimates the expected score gain after a move.
 */
int evaluate_move(Move move, Position *pos)
{
	const int phase = get_phase(pos);
	const Square origin = get_origin_square(move);
	const Square target = get_target_square(move);
	const Piece piece = get_piece_at(pos, origin);
	const Color color = get_piece_color(piece);
	const PieceType pt = get_piece_type(piece);

	struct score score = {0, 0};

	if (move_is_promotion(move)) {
		/* Promotions with captures are already handled by SEE so we
		 * shouldn't do anything about them here. */
		if (!move_is_capture(move)) {
			const int tmp = point_value[PIECE_TYPE_QUEEN] -
			                point_value[PIECE_TYPE_PAWN];
			score.mg += tmp;
			score.eg += tmp;
		}
		/* A queen is so much more valuable than a pawn that we just
		 * don't care about where the pawn was. */
		score.mg += sq_tables[color][PIECE_TYPE_QUEEN][target].mg;
		score.eg += sq_tables[color][PIECE_TYPE_QUEEN][target].eg;
	} else {
		score.mg += sq_tables[color][pt][target].mg;
		score.mg -= sq_tables[color][pt][origin].mg;
		score.eg += sq_tables[color][pt][target].eg;
		score.eg -= sq_tables[color][pt][origin].eg;
	}

	if (move_is_capture(move)) {
		const struct score cap_score = evaluate_capture(move, pos);
		score.mg += cap_score.mg;
		score.eg += cap_score.eg;
	}

	return ((score.mg * (256 - phase)) + (score.eg * phase)) / 256;
}

void eval_init(void)
{
	init_sq_tables();
}
