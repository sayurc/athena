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

/*
 * [Color][PIECE_TYPE][Square]
 */
static i8 sq_tables[2][6][64];

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

/*
 * The square tables for black pieces has the same values as the ones for white
 * pieces but the board is flipped, so this function uses the square tables of
 * white pieces to initialize the square tables of black pieces.
 * The square tables are indexed by the square number so even though the code
 * looks like a chess board the top row is actually the rank 1.
 */
static void init_square_tables(void)
{
	const i8 white_pawn[64] = {
		 0,  0,   0,   0,   0,   0,  0,  0,
		 5, 10,  10, -20, -20,  10, 10,  5,
		 5, -5, -10,   0,   0, -10, -5,  5,
		 0,  0,   0,  20,  20,   0,  0,  0,
		 5,  5,  10,  25,  25,  10,  5,  5,
		10, 10,  20,  30,  30,  20, 10, 10,
		50, 50,  50,  50,  50,  50, 50, 50,
		 0,  0,   0,   0,   0,   0,  0,  0,
	};
	const i8 white_knight[64] = {
		-50, -40, -30, -30, -30, -30, -40, -50,
		-40, -20,   0,   5,   5,   0, -20, -40,
		-30,   5,  10,  15,  15,  10,   5, -30,
		-30,   0,  15,  20,  20,  15,   0, -30,
		-30,   5,  15,  20,  20,  15,   5, -30,
		-30,   0,  10,  15,  15,  10,   0, -30,
		-40, -20,   0,   0,   0,   0, -20, -40,
		-50, -40, -30, -30, -30, -30, -40, -50,
	};
	const i8 white_bishop[64] = {
		-20, -10, -10, -10, -10, -10, -10, -20,
		-10,   5,   0,   0,   0,   0,   5, -10,
		-10,  10,  10,  10,  10,  10,  10, -10,
		-10,   0,  10,  10,  10,  10,   0, -10,
		-10,   5,   5,  10,  10,   5,   5, -10,
		-10,   0,   5,  10,  10,   5,   0, -10,
		-10,   0,   0,   0,   0,   0,   0, -10,
		-20, -10, -10, -10, -10, -10, -10, -20,
	};
	const i8 white_rook[64] = {
		 0,  0,  0,  5,  5,  0,  0,  0,
		-5,  0,  0,  0,  0,  0,  0, -5,
		-5,  0,  0,  0,  0,  0,  0, -5,
		-5,  0,  0,  0,  0,  0,  0, -5,
		-5,  0,  0,  0,  0,  0,  0, -5,
		-5,  0,  0,  0,  0,  0,  0, -5,
		 5, 10, 10, 10, 10, 10, 10,  5,
		 0,  0,  0,  0,  0,  0,  0,  0,
	};
	const i8 white_queen[64] = {
		-20, -10, -10, -5, -5, -10, -10, -20,
		-10,   0,   5,  0,  0,   0,   0, -10,
		-10,   5,   5,  5,  5,   5,   0, -10,
		  0,   0,   5,  5,  5,   5,   0,  -5,
		 -5,   0,   5,  5,  5,   5,   0,  -5,
		-10,   0,   5,  5,  5,   5,   0, -10,
		-10,   0,   0,  0,  0,   0,   0, -10,
		-20, -10, -10, -5, -5, -10, -10, -20,
	};
	const i8 white_king[64] = {
		 20,  30,  10,   0,   0,  10,  30,  20,
		 20,  20,   0,   0,   0,   0,  20,  20,
		-10, -20, -20, -20, -20, -20, -20, -10,
		-20, -30, -30, -40, -40, -30, -30, -20,
		-30, -40, -40, -50, -50, -40, -40, -30,
		-30, -40, -40, -50, -50, -40, -40, -30,
		-30, -40, -40, -50, -50, -40, -40, -30,
		-30, -40, -40, -50, -50, -40, -40, -30,
	};

	size_t size = sizeof(white_pawn) / sizeof(white_pawn[0]);
	memcpy(sq_tables[COLOR_WHITE][PIECE_TYPE_PAWN], white_pawn, size);
	memcpy(sq_tables[COLOR_WHITE][PIECE_TYPE_KNIGHT], white_knight, size);
	memcpy(sq_tables[COLOR_WHITE][PIECE_TYPE_BISHOP], white_bishop, size);
	memcpy(sq_tables[COLOR_WHITE][PIECE_TYPE_ROOK], white_rook, size);
	memcpy(sq_tables[COLOR_WHITE][PIECE_TYPE_QUEEN], white_queen, size);
	memcpy(sq_tables[COLOR_WHITE][PIECE_TYPE_KING], white_king, size);

	for (PieceType pt = PIECE_TYPE_PAWN; pt <= PIECE_TYPE_KING; ++pt)
		flip_table(sq_tables[COLOR_BLACK][pt], sq_tables[COLOR_WHITE][pt]);
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

	score += sq_tables[piece_color][piece_type][target];
	score -= sq_tables[piece_color][piece_type][origin];

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
			score += sq_tables[color][piece_type][sq];
		}

		piece = pos_make_piece(piece_type, !color);
		bb = pos_get_piece_bitboard(pos, piece);
		while (bb) {
			const Square sq = unset_ls1b(&bb);
			score -= sq_tables[!color][piece_type][sq];
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
	       sq_tables[attacker_color][attacker_type][target];
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

static int score_king(const Position *pos, Color color)
{
	const Square sq = pos_get_king_square(pos, color);

	return sq_tables[color][PIECE_TYPE_KING][sq];
}

/*
 * Computes the score of the attacks on the square sq.
 */
static int score_attacks(const Position *pos, u64 attackers, Square sq)
{
	const Piece piece = pos_get_piece_at(pos, sq);

	int score = 0;
	while (attackers) {
		const Square attacker_sq = unset_ls1b(&attackers);
		const Piece attacker = pos_get_piece_at(pos, attacker_sq);
		const Color attacker_color = pos_get_piece_color(piece);
		const PieceType attacker_type = pos_get_piece_type(attacker);
		if (piece == PIECE_NONE) {
			score += sq_tables[attacker_color][attacker_type][sq] - sq_tables[attacker_color][attacker_type][attacker_sq];
		} if (attacker_type == PIECE_TYPE_KING) {
			score += get_captured_piece_value(piece) + score_king(pos, attacker_color);
		} else {
			const Color color = pos_get_piece_color(piece);
			const PieceType pt = pos_get_piece_type(piece);

			//score += get_captured_piece_value(piece) + get_attacker_value(attacker);
			score += sq_tables[color][pt][sq];
			score += sq_tables[attacker_color][attacker_type][sq];
			score -= sq_tables[attacker_color][attacker_type][attacker_sq];
		}
	}

	return score;
}

/*
 * Computes the score of the squares controlled by the piece at square sq. We
 * don't handle the case of a king at an attacked square because that means the
 * king is in check and this is handled elsewhere. 
 */
static int score_controlled_squares(const Position *pos, u64 controlled, Square sq)
{
	const int center_bonus[] = {
		[A8] = 0, [B8] = 0, [C8] = 0,   [D8] = 0,   [E8] = 0,   [F8] = 0,   [G8] = 0, [H8] = 0,
		[A7] = 0, [B7] = 0, [C7] = 0,   [D7] = 0,   [E7] = 0,   [F7] = 0,   [G7] = 0, [H7] = 0,
		[A6] = 0, [B6] = 0, [C6] = 50,  [D6] = 50,  [E6] = 50,  [F6] = 50,  [G6] = 0, [H6] = 0,
		[A5] = 0, [B5] = 0, [C5] = 50,  [D5] = 150, [E5] = 150, [F5] = 50,  [G5] = 0, [H5] = 0,
		[A4] = 0, [B4] = 0, [C4] = 50,  [D4] = 150, [E4] = 150, [F4] = 50,  [G4] = 0, [H4] = 0,
		[A3] = 0, [B3] = 0, [C3] = 50,  [D3] = 50,  [E3] = 50,  [F3] = 50,  [G3] = 0, [H3] = 0,
		[A2] = 0, [B2] = 0, [C2] = 0,   [D2] = 0,   [E2] = 0,   [F2] = 0,   [G2] = 0, [H2] = 0,
		[A1] = 0, [B1] = 0, [C1] = 0,   [D1] = 0,   [E1] = 0,   [F1] = 0,   [G1] = 0, [H1] = 0,
	};
	const Piece piece = pos_get_piece_at(pos, sq);
	const PieceType pt = pos_get_piece_type(piece);
	const Color color = pos_get_piece_color(piece);

	int score = 0;
	while (controlled) {
		const Square attacked_sq = unset_ls1b(&controlled);
		const Piece attacked = pos_get_piece_at(pos, attacked_sq);
		if (attacked != PIECE_NONE) {
			const Color attacked_color = pos_get_piece_color(attacked);
			const PieceType attacked_type = pos_get_piece_type(attacked);
			//score += get_captured_piece_value(attacked) + get_attacker_value(piece);
			score += sq_tables[attacked_color][attacked_type][attacked_sq];
		}
		score += sq_tables[color][pt][attacked_sq];
		score -= sq_tables[color][pt][sq];

		score += center_bonus[attacked_sq];
	}

	return score;
}

/*
 * Score all pieces of a color on the board based on their positioning,
 * controlled squares, etc. Although the king is not evaluated directly in this
 * function, its safety is taken into account when the king is defending the
 * piece being evaluated.
 */
static int score_pieces(const Position *pos, Color color)
{
	int score = 0;

	u64 bb = pos_get_color_bitboard(pos, color);

	bool found_light_bishop = false, found_dark_bishop = false;
	while (bb) {
		const Square sq = unset_ls1b(&bb);
		const Piece piece = pos_get_piece_at(pos, sq);
		const PieceType pt = pos_get_piece_type(piece);
		if (pt == PIECE_TYPE_KING)
			continue;

		if (pt == PIECE_TYPE_BISHOP) {
			SquareColor sc = pos_get_square_color(sq);
			if (sc == SQUARE_COLOR_LIGHT)
				found_light_bishop = true;
			if (sc == SQUARE_COLOR_DARK)
				found_dark_bishop = true;

			/* Bonus for the bishop pair. */
			if (found_light_bishop && found_dark_bishop == true) {
				score += point_value[PIECE_TYPE_ROOK] / 2;
				/* Reset these for the next bishop pair. */
				found_light_bishop = false;
				found_dark_bishop = false;
			}
		}

		score += sq_tables[color][pt][sq];

		const u64 attackers = movegen_get_attackers(sq, pos);
		const u64 friend_attackers = attackers & pos_get_color_bitboard(pos, color);
		const u64 enemy_attackers = attackers & pos_get_color_bitboard(pos, !color);
		score += score_attacks(pos, friend_attackers, sq);
		score -= score_attacks(pos, enemy_attackers, sq);

		const u64 controlled = movegen_get_attacked_squares(sq, pos);
		score += score_controlled_squares(pos, controlled, sq);
	}

	return score;
}

int eval_evaluate(const Position *pos)
{
	Color color = pos_get_side_to_move(pos);

	int score = score_pieces(pos, color);
	score -= score_pieces(pos, !color);
	score += 3 * compute_material(pos) / 2;

	return score;
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
		const Piece cap_piece = pos_get_captured_piece(pos);
		score += get_captured_piece_value(cap_piece) - evaluate_exchange(target, pos);
		move_undo(pos, move);
	}

	return score;
}

void eval_init(void)
{
	init_possible_moves_table();
	init_square_tables();
}
