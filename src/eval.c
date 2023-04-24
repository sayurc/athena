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

#include <check.h>

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

static struct score sq_tables[2][6][64];

/*
 * These tables store the number of possible moves for a piece when the board
 * contains only that piece, so no occupancy for sliding pieces or pawns.
 */
static i8 white_pawn_number_of_possible_moves[64];
static i8 black_pawn_number_of_possible_moves[64];
static i8 knight_number_of_possible_moves[64];
static i8 rook_number_of_possible_moves[64];
static i8 bishop_number_of_possible_moves[64];
static i8 queen_number_of_possible_moves[64];
static i8 king_number_of_possible_moves[64];

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

static void init_possible_moves_table(void)
{
	for (Square sq = A1; sq <= H8; ++sq) {
		white_pawn_number_of_possible_moves[sq] = movegen_get_number_of_moves_empty_board(PIECE_WHITE_PAWN, sq);
		black_pawn_number_of_possible_moves[sq] = movegen_get_number_of_moves_empty_board(PIECE_BLACK_PAWN, sq);
		knight_number_of_possible_moves[sq] = movegen_get_number_of_moves_empty_board(PIECE_WHITE_KNIGHT, sq);
		rook_number_of_possible_moves[sq] = movegen_get_number_of_moves_empty_board(PIECE_WHITE_ROOK, sq);
		bishop_number_of_possible_moves[sq] = movegen_get_number_of_moves_empty_board(PIECE_WHITE_BISHOP, sq);
		queen_number_of_possible_moves[sq] = movegen_get_number_of_moves_empty_board(PIECE_WHITE_QUEEN, sq);
		king_number_of_possible_moves[sq] = movegen_get_number_of_moves_empty_board(PIECE_WHITE_KING, sq);
	}
}

static void flip_table(i8 *restrict dst, const i8 *restrict src)
{
	for (size_t r = RANK_1; r <= RANK_8; ++r) {
		for (size_t f = FILE_A; f <= FILE_H; ++f) {
			const Square sq = pos_file_rank_to_square(f, r);
			const Rank dst_r = RANK_8 - r;
			const Square dst_sq = pos_file_rank_to_square(f, dst_r);
			dst[dst_sq] = src[sq];
		}
	}
}

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
	const PieceType pt = pos_get_piece_type(piece);

	return point_value[(len - 1) - pt];
}

static int get_captured_piece_value(Piece piece)
{
	const PieceType pt = pos_get_piece_type(piece);
	return point_value[pt];
}

/*
 * Returns the bitboard of the least valuable pieces of the current side to move
 * attacking a square. If there are no pieces attacking the square then it
 * returns 0.
 */
static u64 get_least_valuable_attackers(Square sq, const Position *pos)
{
	const u64 attackers = movegen_get_attackers(sq, pos);

	u64 ret = 0;
	Piece least_piece = PIECE_NONE;
	for (PieceType pt = PIECE_TYPE_PAWN; pt <= PIECE_TYPE_KING; ++pt) {
		const Piece piece = pos_make_piece(pt, pos_get_side_to_move(pos));
		const u64 bb = pos_get_piece_bitboard(pos, piece);
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

/*
 * This function should be used during the opening only. It returns a positive
 * penalty value if the queen or rook aren't at their initial squares and 0
 * otherwise. It can only be called with rook or queen as argument because
 * there is no development penalty for other pieces during the opening.
 */
static int get_dev_penalty(Piece piece, Square sq)
{
	const PieceType pt = pos_get_piece_type(piece);
	const Color c = pos_get_piece_color(piece);

	if (pt == PIECE_TYPE_ROOK) {
		if (c == COLOR_WHITE)
			return (sq != A1 && sq != H1) * 20;
		else
			return (sq != A8 && sq != H8) * 20;
	} else {
		if (c == COLOR_WHITE)
			return (sq != D1) * 25;
		else
			return (sq != D8) * 25;
	}
}

static int estimate_positioning_gain(Move move, Position *pos)
{
	const Square target = move_get_target(move);
	const Square origin = move_get_origin(move);
	const Piece piece = pos_get_piece_at(pos, origin);
	const PieceType piece_type = pos_get_piece_type(piece);
	const Color piece_color = pos_get_side_to_move(pos);

	int score = 0;

	if (movegen_is_square_attacked(target, !piece_color, pos))
		score -= get_captured_piece_value(piece);
	else
		score += 1;
	if (movegen_is_square_attacked(origin, !piece_color, pos))
		score += get_captured_piece_value(piece);

	if (piece_type == PIECE_TYPE_PAWN) {
		score += piece_color == COLOR_WHITE ?
		         pos_get_rank_of_square(target) :
		         (RANK_7 - pos_get_rank_of_square(target));
	}

	score += sq_tables[piece_color][piece_type][target].mg;
	score -= sq_tables[piece_color][piece_type][origin].mg;

	return score;
}

static int compute_positioning(const Position *pos)
{
	const Color color = pos_get_side_to_move(pos);

	int score = 0;

	for (PieceType piece_type = PIECE_TYPE_PAWN; piece_type <= PIECE_TYPE_KING; ++piece_type) {
		Piece piece = pos_make_piece(piece_type, color);
		u64 bb = pos_get_piece_bitboard(pos, piece);
		while (bb) {
			const Square sq = unset_ls1b(&bb);
			score += sq_tables[color][piece_type][sq].mg;
		}

		piece = pos_make_piece(piece_type, !color);
		bb = pos_get_piece_bitboard(pos, piece);
		while (bb) {
			const Square sq = unset_ls1b(&bb);
			score -= sq_tables[!color][piece_type][sq].mg;
		}
	}

	return score;
}

static int estimate_mobility_gain(Move move, Position *pos)
{
	i8 *number_of_possible_moves[] = {
		[PIECE_TYPE_PAWN  ] = white_pawn_number_of_possible_moves,
		[PIECE_TYPE_KNIGHT] = knight_number_of_possible_moves,
		[PIECE_TYPE_ROOK  ] = rook_number_of_possible_moves,
		[PIECE_TYPE_BISHOP] = bishop_number_of_possible_moves,
		[PIECE_TYPE_QUEEN ] = queen_number_of_possible_moves,
		[PIECE_TYPE_KING  ] = king_number_of_possible_moves,
	};
	const Square target = move_get_target(move);
	const Square origin = move_get_origin(move);
	const Piece piece = pos_get_piece_at(pos, origin);
	const PieceType piece_type = pos_get_piece_type(piece);
	const Color piece_color = pos_get_side_to_move(pos);

	if (piece_color == COLOR_BLACK)
		number_of_possible_moves[PIECE_TYPE_PAWN] = black_pawn_number_of_possible_moves;

	int mobility = 0;
	/* In captures the oponent loses mobility, and as a consequence we gain
	 * mobility points. */
	if (move_is_capture(move)) {
		const MoveType cap_type = move_get_type(move);
		/* The captured piece is not at the target square in en passant
		 * captures so we have to treat it separately. */
		if (cap_type != MOVE_EP_CAPTURE) {
			const Piece cap_piece = pos_get_piece_at(pos, target);
			const PieceType cap_piece_type = pos_get_piece_type(cap_piece);
			mobility += number_of_possible_moves[cap_piece_type][target];
		}
		
		/* If it's a promotion then use the piece the pawn was promoted
		 * to. */
		if (move_is_promotion(move)) {
			const PieceType promoted_to = move_get_promotion_piece_type(move);
			mobility += number_of_possible_moves[promoted_to][target];
		} else if (cap_type == MOVE_EP_CAPTURE) {
			mobility += number_of_possible_moves[PIECE_TYPE_PAWN][target];
		} else {
			mobility += number_of_possible_moves[piece_type][target];
		}
	} else {
		mobility += number_of_possible_moves[piece_type][target];
	}
	mobility -= number_of_possible_moves[piece_type][origin];

	const Rank rank = pos_get_rank_of_square(target);
	const File file = pos_get_file_of_square(target);
	/* Moving to the center of the board means more squares to attack. */
	if (piece_type != PIECE_TYPE_PAWN) {
		if ((rank == RANK_5 || rank == RANK_4) && (file == FILE_D || file == FILE_E))
			mobility += 10;
		else if ((rank == RANK_6 || rank == RANK_3) && (file == FILE_C || file == FILE_F))
			mobility += 5;
	} else {
		if (file >= FILE_B && file <= FILE_G)
			mobility += 2;
	}

	return mobility;
}

static int compute_mobility(const Position *pos)
{
	const int c = pos_get_side_to_move(pos);
	int mobility = 0;
	
	for (PieceType pt = PIECE_TYPE_PAWN; pt <= PIECE_TYPE_KING; ++pt) {
		mobility += movegen_get_number_of_pseudo_legal_moves(pt, c, pos);
	}
	for (PieceType pt = PIECE_TYPE_PAWN; pt <= PIECE_TYPE_KING; ++pt) {
		mobility -= movegen_get_number_of_pseudo_legal_moves(pt, !c, pos);	
	}

	return mobility;
}

static int compute_mvv_lva(Move move, const Position *pos)
{
	const Square target = move_get_target(move);
	const Square origin = move_get_origin(move);
	const Piece attacked = pos_get_piece_at(pos, target);
	const Piece attacker = pos_get_piece_at(pos, origin);
	const PieceType attacker_type = pos_get_piece_type(attacker);
	const Color attacker_color = pos_get_piece_color(attacker);

	return get_captured_piece_value(attacked) +
	       get_attacker_value(attacker) +
	       sq_tables[attacker_color][attacker_type][target].mg;
}

/*
 * Returns the total score in piece value of a series of exchanges on a square
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
		const Piece piece = pos_get_piece_at(pos, from);
		MoveType move_type = MOVE_CAPTURE;
		if ((piece == PIECE_WHITE_PAWN && pos_get_rank_of_square(target) == RANK_8) ||
		    (piece == PIECE_BLACK_PAWN && pos_get_rank_of_square(target) == RANK_1))
			move_type = MOVE_QUEEN_PROMOTION_CAPTURE;
		const Move move = move_new(from, target, move_type);
		move_do(pos, move);
		const Piece cap_piece = pos_get_captured_piece(pos);
		const int score = get_captured_piece_value(cap_piece) - evaluate_exchange(target, pos);
		if (score > 0)
			value = score;
		move_undo(pos, move);
	}
	return value;
}

static int estimate_material_gain(Move move, Position *pos)
{
	if (move_get_type(move) == MOVE_CAPTURE) {
		move_do(pos, move);
		const Piece cap_piece = pos_get_captured_piece(pos);
		const Square target = move_get_target(move);
		int score = get_captured_piece_value(cap_piece) - evaluate_exchange(target, pos);
		move_undo(pos, move);

		return score + compute_mvv_lva(move, pos);
	}
	return 0;
}

static int compute_material(const Position *pos)
{
	const Color c = pos_get_side_to_move(pos);
	const int P = pos_make_piece(PIECE_TYPE_PAWN  , c),
	          N = pos_make_piece(PIECE_TYPE_KNIGHT, c),
	          R = pos_make_piece(PIECE_TYPE_ROOK  , c),
	          B = pos_make_piece(PIECE_TYPE_BISHOP, c),
	          Q = pos_make_piece(PIECE_TYPE_QUEEN , c),
	          K = pos_make_piece(PIECE_TYPE_KING  , c);
	const int p = pos_make_piece(PIECE_TYPE_PAWN  , !c),
	          n = pos_make_piece(PIECE_TYPE_KNIGHT, !c),
	          r = pos_make_piece(PIECE_TYPE_ROOK  , !c),
	          b = pos_make_piece(PIECE_TYPE_BISHOP, !c),
	          q = pos_make_piece(PIECE_TYPE_QUEEN , !c),
	          k = pos_make_piece(PIECE_TYPE_KING  , !c);
	const int num_P = pos_get_number_of_pieces(pos, P),
	          num_N = pos_get_number_of_pieces(pos, N),
	          num_R = pos_get_number_of_pieces(pos, R),
	          num_B = pos_get_number_of_pieces(pos, B),
	          num_Q = pos_get_number_of_pieces(pos, Q),
	          num_K = pos_get_number_of_pieces(pos, K);

	const int num_p = pos_get_number_of_pieces(pos, p),
	          num_n = pos_get_number_of_pieces(pos, n),
	          num_r = pos_get_number_of_pieces(pos, r),
	          num_b = pos_get_number_of_pieces(pos, b),
	          num_q = pos_get_number_of_pieces(pos, q),
	          num_k = pos_get_number_of_pieces(pos, k);
	const int material = point_value[PIECE_TYPE_PAWN]   * (num_P - num_p)
	                   + point_value[PIECE_TYPE_KNIGHT] * (num_N - num_n)
	                   + point_value[PIECE_TYPE_ROOK]   * (num_R - num_r)
	                   + point_value[PIECE_TYPE_BISHOP] * (num_B - num_b)
                           + point_value[PIECE_TYPE_QUEEN]  * (num_Q - num_q)
	                   + point_value[PIECE_TYPE_KING]   * (num_K - num_k);

	return material;
}

/*
 * This function returns a number between 0 and 256 representing the game phase
 * where 0 is the initial phase and 256 is the final phase. This approach of
 * representing the game with several phases prevents evaluation discontinuity.
 */
int eval_get_phase(const Position *pos)
{
	const int weights[] = {
		[PIECE_TYPE_PAWN] = 0, [PIECE_TYPE_KNIGHT] = 1,
		[PIECE_TYPE_BISHOP] = 1, [PIECE_TYPE_ROOK] = 2,
		[PIECE_TYPE_QUEEN] = 4
	};
	const int neutral = 16 * weights[PIECE_TYPE_PAWN  ] +
	              4  * weights[PIECE_TYPE_KNIGHT] +
	              4  * weights[PIECE_TYPE_BISHOP] +
	              4  * weights[PIECE_TYPE_ROOK  ] +
	              2  * weights[PIECE_TYPE_QUEEN ];

	int phase = neutral;

	for (Color c = COLOR_WHITE; c <= COLOR_BLACK; ++c) {
		for (PieceType pt = PIECE_TYPE_PAWN; pt <= PIECE_TYPE_QUEEN; ++pt) {
			Piece piece = pos_make_piece(pt, c);
			int num = pos_get_number_of_pieces(pos, piece);
			phase -= num * weights[pt];
		}
	}
	phase = (256 * phase + (neutral / 2)) / neutral;

	return phase;
}

int eval_evaluate(const Position *pos)
{
	const Color color = pos_get_side_to_move(pos);
	const int phase = eval_get_phase(pos);

	struct score score = {0, 0};
	for (Square sq = A1; sq <= H8; ++sq) {
		const Piece piece = pos_get_piece_at(pos, sq);
		if (piece == PIECE_NONE)
			continue;
		const PieceType pt = pos_get_piece_type(piece);
		if (pt == PIECE_TYPE_KING)
			continue;

		if (pos_get_piece_color(piece) == color) {
			score.mg += sq_tables[color][pt][sq].mg;
			score.eg += sq_tables[color][pt][sq].eg;
		} else {
			score.mg -= sq_tables[!color][pt][sq].mg;
			score.eg -= sq_tables[!color][pt][sq].eg;
		}
	}

	score.mg += compute_material(pos);
	score.eg += compute_material(pos);

	return ((score.mg * (256 - phase)) + (score.eg * phase)) / 256;
}

int eval_evaluate_move(Move move, Position *pos)
{
	const int material = estimate_material_gain(move, pos);
	const int mobility = estimate_mobility_gain(move, pos);
	const int positioning = estimate_positioning_gain(move, pos);

	return material +
	       mobility +
	       positioning;
}

/*
 * This function is used to evaluate moves in the quiescence search only, so it
 * works only for captures.
 */
int eval_evaluate_qmove(Move move, Position *pos)
{
	const Square origin = move_get_origin(move);
	const Square target = move_get_target(move);
	const Piece piece = pos_get_piece_at(pos, origin);
	const Color color = pos_get_piece_color(piece);
	const PieceType pt = pos_get_piece_type(piece);
	const MoveType move_type = move_get_type(move);

	Piece cap_piece;
	PieceType cap_piece_type;
	if (move_type == MOVE_EP_CAPTURE) {
		cap_piece_type = PIECE_TYPE_PAWN;
		cap_piece = pos_make_piece(cap_piece_type, !color);
	} else {
		cap_piece = pos_get_piece_at(pos, target);
		cap_piece_type = pos_get_piece_type(cap_piece);
	}

	int score = 0;

	score += compute_mvv_lva(move, pos);
	if (point_value[pt] < point_value[PIECE_TYPE_ROOK] &&
	    point_value[cap_piece_type] >= point_value[PIECE_TYPE_ROOK]) {
		score += point_value[cap_piece_type];
		if (move_is_promotion(move))
			score += point_value[PIECE_TYPE_QUEEN];
	} else {
		move_do(pos, move);
		cap_piece = pos_get_captured_piece(pos);
		score += get_captured_piece_value(cap_piece) - evaluate_exchange(target, pos);
		move_undo(pos, move);
	}

	return score;
}

void eval_init(void)
{
	init_possible_moves_table();
	init_sq_tables();
}
