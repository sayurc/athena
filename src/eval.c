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

#include <stdbool.h>
#include <stdint.h>

#include <bit.h>
#include <pos.h>
#include <move.h>
#include <eval.h>

struct score {
	int mg;
	int eg;
};

/* clang-format off */
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
/* clang-format on */

static int evaluate_move(Move move, const Position *pos);
static int get_square_value(Piece piece, Square sq, bool middle_game);
static int mvv_lva(Move move, const Position *pos);

/*
 * These are the intrinsic point value of each piece in the centipawn scale.
 */
static const int point_value[] = {
	[PIECE_TYPE_PAWN] = 100,   [PIECE_TYPE_KNIGHT] = 325,
	[PIECE_TYPE_BISHOP] = 350, [PIECE_TYPE_ROOK] = 500,
	[PIECE_TYPE_QUEEN] = 1000, [PIECE_TYPE_KING] = 10000,
};

int pick_next_move(const Move *moves, int len, const Position *pos)
{
	int best_score = -INF;
	int best = 0;

	for (int i = 0; i < len; ++i) {
		const int score = evaluate_move(moves[i], pos);
		if (score > best_score) {
			best_score = score;
			best = i;
		}
	}

	return best;
}

int evaluate(const Position *pos)
{
	const Color color = get_side_to_move(pos);
	const int phase = get_phase(pos);

	struct score score;
	score.mg = 0;
	score.eg = 0;

	/* Material */
	for (PieceType pt = PIECE_TYPE_PAWN; pt <= PIECE_TYPE_QUEEN; ++pt) {
		const Piece p1 = create_piece(pt, color),
			    p2 = create_piece(pt, !color);
		const int nb1 = get_number_of_pieces(pos, p1);
		const int nb2 = get_number_of_pieces(pos, p2);
		const int tmp = point_value[pt] * (nb1 - nb2);
		/* The material score for the middle-game and end-game are the
		 * same. */
		score.mg += tmp;
		score.eg += tmp;
	}

	/* PST */
	for (Square sq = A1; sq <= H8; ++sq) {
		const Piece piece = get_piece_at(pos, sq);
		if (piece == PIECE_NONE)
			continue;
		if (get_piece_color(piece) == color) {
			score.mg += get_square_value(piece, sq, true);
			score.eg += get_square_value(piece, sq, false);
		} else {
			score.mg -= get_square_value(piece, sq, true);
			score.eg += get_square_value(piece, sq, false);
		}
	}

	/* Linear interpolation of (INITIAL_PHASE, score.mg) and
	 * (FINAL_PHASE, score.eg). */
	return ((score.mg * (FINAL_PHASE - phase)) +
		score.eg * (phase - INITIAL_PHASE)) /
	       FINAL_PHASE;
}

/*
 * This function tries to guess how good a move is without actually searching
 * the position, the better the guess the more nodes will be pruned in the
 * alpha-beta pruning search. Of course, since it is the position evaluation
 * function that decides how good a move actually is during the search, this
 * function has to be adjusted accordingly to it.
 */
static int evaluate_move(Move move, const Position *pos)
{
	const int phase = get_phase(pos);

	struct score score;
	score.mg = 0;
	score.eg = 0;

	const Square from = get_move_origin(move);
	const Square to = get_move_target(move);
	const Piece piece = get_piece_at(pos, from);

	if (move_is_capture(move)) {
		const int tmp = mvv_lva(move, pos);
		score.mg += tmp;
		score.eg += tmp;
	}

	score.mg += get_square_value(piece, to, true);
	score.eg += get_square_value(piece, to, false);
	score.mg -= get_square_value(piece, from, true);
	score.eg -= get_square_value(piece, from, false);

	return ((score.mg * (FINAL_PHASE - phase)) +
		score.eg * (phase - INITIAL_PHASE)) /
	       FINAL_PHASE;
}

static int get_square_value(Piece piece, Square sq, bool middle_game)
{
	const int *mg_table[] = {
		[PIECE_TYPE_PAWN] = mg_pawn_sq_table,
		[PIECE_TYPE_KNIGHT] = mg_knight_sq_table,
		[PIECE_TYPE_BISHOP] = mg_bishop_sq_table,
		[PIECE_TYPE_ROOK] = mg_rook_sq_table,
		[PIECE_TYPE_QUEEN] = mg_queen_sq_table,
		[PIECE_TYPE_KING] = mg_king_sq_table,
	};
	const int *eg_table[] = {
		[PIECE_TYPE_PAWN] = eg_pawn_sq_table,
		[PIECE_TYPE_KNIGHT] = eg_knight_sq_table,
		[PIECE_TYPE_BISHOP] = eg_bishop_sq_table,
		[PIECE_TYPE_ROOK] = eg_rook_sq_table,
		[PIECE_TYPE_QUEEN] = eg_queen_sq_table,
		[PIECE_TYPE_KING] = eg_king_sq_table,
	};

	const PieceType pt = get_piece_type(piece);
	/* The tables are from the point of view of black so we flip the square
	 * with sq ^ 56 when it is white. */
	if (get_piece_color(piece) == COLOR_WHITE) {
		return middle_game ? mg_table[pt][sq ^ 56] :
				     eg_table[pt][sq ^ 56];
	} else {
		return middle_game ? mg_table[pt][sq] : eg_table[pt][sq];
	}
}

/*
 * Most Valuable Victim - Least Valuable Aggressor.
 *
 * A heuristic that scores a capturing move based on the fact that capturing
 * valuable pieces with less valuable pieces is generally good. Capturing a
 * queen with a knight is probably worth it even if we lose the knight on the
 * next move.
 */
static int mvv_lva(Move move, const Position *pos)
{
	const Piece piece1 = get_piece_at(pos, get_move_origin(move));
	const PieceType attacker = get_piece_type(piece1);
	const Color c = get_piece_color(piece1);
	const MoveType move_type = get_move_type(move);
	Piece piece2;
	if (move_type == MOVE_EP_CAPTURE)
		piece2 = c == COLOR_WHITE ? PIECE_BLACK_PAWN : PIECE_WHITE_PAWN;
	else
		piece2 = get_piece_at(pos, get_move_target(move));
	const PieceType victim = get_piece_type(piece2);

	/* Here we assume that the point_value table is sorted from least
	 * valuable to most valuable */
	const int len = sizeof(point_value) / sizeof(point_value[0]);
	return point_value[len - 1 - (int)attacker] + point_value[victim];
}
