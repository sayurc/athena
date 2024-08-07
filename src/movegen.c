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

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <bit.h>
#include <str.h>
#include <pos.h>
#include <move.h>
#include <movegen.h>
#include <rng.h>

typedef struct magic {
	u64 *ptr;
	u64 mask;
	u64 num;
	int shift;
} Magic;

typedef struct move_list {
	struct move_with_score *ptr;
	int capacity;
	int len;
} MoveList;

static void gen_moves(MoveList *restrict list, PieceType piece_type,
		      enum move_gen_type type, const Position *restrict pos);
static void gen_king_moves(MoveList *restrict list, enum move_gen_type type,
			   const Position *restrict pos);
static void gen_queen_castling(MoveList *restrict list,
			       const Position *restrict pos);
static void gen_king_castling(MoveList *restrict list,
			      const Position *restrict pos);
static void gen_pawn_moves(MoveList *restrict list, enum move_gen_type type,
			   const Position *restrict pos);
static void gen_pawn_attacks(MoveList *restrict list, Square from,
			     u64 enemy_pieces, Color color);
static void gen_double_pushes(MoveList *restrict list, Square from, u64 occ,
			      Color color);
static void gen_pushes(MoveList *restrict list, Square from, u64 occ,
		       Color color);
static void add_move(MoveList *restrict list, Move move);
static u64 get_king_attacks(Square sq);
static u64 get_queen_attacks(Square sq, u64 occ);
static u64 get_rook_attacks(Square sq, u64 occ);
static u64 get_bishop_attacks(Square sq, u64 occ);
static u64 get_knight_attacks(Square sq);
static u64 get_double_push(Square sq, u64 occ, Color c);
static u64 get_single_push(Square sq, u64 occ, Color c);
static void init_king_attacks(void);
static void init_rook_attacks(void);
static void init_bishop_attacks(void);
static void init_knight_attacks(void);
static void init_sliding_atacks(PieceType pt);
static u64 slow_get_rook_attacks(Square sq, u64 occ);
static u64 slow_get_bishop_attacks(Square sq, u64 occ);
static u64 gen_ray_attacks(u64 occ, Direction dir, Square sq);
static void init_rays(void);

/*
 * The bitboards for each rank and file contain all the squares of a rank or
 * file, and they are used to generate the ray bitboards. A ray bitboard
 * represents all the squares to a specific direction from a square.
 * For example, the following two bitboards are the ray bitboard for the north
 * and southeast directions from square C3.
 *
 * 00000001	00000000
 * 00000010	00000000
 * 00000100	00000000
 * 00001000	00000000
 * 00010000	00000000
 * 00000000	00000000
 * 00000000	00010000
 * 00000000	00001000
 *
 * The ray bitboards can be used to generate sliding piece attacks, but this
 * is very slow so it is only done to initialize the attack tables for magic
 * bitboards.
 */
static const u64 rank_bitboards[] = {
	U64(0x00000000000000ff), U64(0x000000000000ff00),
	U64(0x0000000000ff0000), U64(0x00000000ff000000),
	U64(0x000000ff00000000), U64(0x0000ff0000000000),
	U64(0x00ff000000000000), U64(0xff00000000000000),
};
static const u64 file_bitboards[] = {
	U64(0x0101010101010101), U64(0x0202020202020202),
	U64(0x0404040404040404), U64(0x0808080808080808),
	U64(0x1010101010101010), U64(0x2020202020202020),
	U64(0x4040404040404040), U64(0x8080808080808080),
};
static u64 ray_bitboards[8][64];
static Magic rook_magics[64];
static Magic bishop_magics[64];
static u64 king_attack_table[64];
static u64 rook_attack_table[0x19000];
static u64 bishop_attack_table[0x1480];
static u64 knight_attack_table[64];

void movegen_init(void)
{
	seed_rng(2718281828459045235);
	init_rays();
	init_knight_attacks();
	init_bishop_attacks();
	init_rook_attacks();
	init_king_attacks();
}

u64 get_file_bitboard(File file)
{
	return file_bitboards[file];
}

/*
 * This function tests if a move is pseudo-legal, meaning a move that is
 * possible for the current position without considering king checks. This is
 * useful to validate transposition table moves (since multiple different
 * positions may hash to the same entry) and to test if a killer move is
 * possible in the current position. It is also useful to make sure a move in
 * the transposition table hasn't been corrupted because of race conditions.
 *
 * We perform the tests in increasing order of computational cost, ideally
 * exiting earlier through the cheaper tests.
 */
bool move_is_pseudo_legal(Move move, const Position *pos)
{
	const MoveType move_type = get_move_type(move);
	const Color side = get_side_to_move(pos);
	const Square from = get_move_origin(move);
	const Square to = get_move_target(move);

	const Piece piece = get_piece_at(pos, from);

	/* These are uncommon moves so we test them the slower, but simpler,
	 * way. */
	if (move_type == MOVE_EP_CAPTURE || move_is_castling(move)) {
		struct move_with_score moves[256];
		MoveList list;
		list.capacity = 256;
		list.ptr = &moves[0];
		list.len = 0;

		if (move_type == MOVE_EP_CAPTURE) {
			gen_pawn_moves(&list, MOVE_GEN_TYPE_CAPTURE, pos);
		} else if (move_type == MOVE_KING_CASTLE) {
			gen_king_castling(&list, pos);
		} else if (move_type == MOVE_QUEEN_CASTLE) {
			gen_queen_castling(&list, pos);
		}
		for (int i = 0; i < list.len; ++i) {
			if (move == moves[i].move)
				return true;
		}
		return false;
	}

	/* The origin square needs to contain the piece we are moving. */
	if (piece == PIECE_NONE)
		return false;
	if (get_piece_color(piece) != side)
		return false;
	const Piece target_piece = get_piece_at(pos, to);
	if (target_piece != PIECE_NONE && get_piece_color(target_piece) == side)
		return false;

	if (move_is_capture(move)) {
		/* All captures need a piece at the target square. We don't have
		 * to care about en passant here because it was handled at the
		 * beginning. */
		if (target_piece == PIECE_NONE)
			return false;
	} else {
		if (target_piece != PIECE_NONE)
			return false;
	}

	const u64 occ = get_color_bitboard(pos, COLOR_WHITE) |
			get_color_bitboard(pos, COLOR_BLACK);
	const u64 enemy_bb = get_color_bitboard(pos, !side);
	const u64 target_bb = U64(0x1) << to;

	if (get_piece_type(piece) == PIECE_TYPE_PAWN) {
		/* We don't have to test if en passant captures are valid here
		 * because they are handled at the start of the function. */
		const Rank from_rank = get_rank(from);
		const Rank to_rank = get_rank(to);

		if (move_is_promotion(move)) {
			if (side == COLOR_WHITE && to_rank != RANK_8)
				return false;
			else if (side == COLOR_BLACK && to_rank != RANK_1)
				return false;
		} else {
			if (side == COLOR_WHITE && to_rank == RANK_8)
				return false;
			else if (side == COLOR_BLACK && to_rank == RANK_1)
				return false;
		}

		if (abs((int)to_rank - (int)from_rank) == 2 &&
		    move_type != MOVE_DOUBLE_PAWN_PUSH)
			return false;
		if (has_en_passant_square(pos)) {
			const Square ep_sq = get_en_passant_square(pos);
			if (to == ep_sq && move_type != MOVE_EP_CAPTURE)
				return false;
		}

		/* We don't need to check if there is an enemy piece at the
		 * target square for pawn attacks because we already tested this
		 * for every piece earlier. */
		if (!(get_single_push(from, occ, side) & target_bb) &&
		    !(get_double_push(from, occ, side) & target_bb) &&
		    !(get_pawn_attacks(from, side) & target_bb & enemy_bb))
			return false;
		else if (get_double_push(from, occ, side) & target_bb) {
			/* Here we make sure the double push is from the
			 * pawn's starting square. */
			if (side == COLOR_WHITE && from_rank != RANK_2)
				return false;
			else if (side == COLOR_BLACK && from_rank != RANK_7)
				return false;
		}
	} else {
		/* If the move is not a pawn or castling move, it can only be of
		 * type MOVE_OTHER or MOVE_CAPTURE. */
		if (move_type != MOVE_OTHER && move_type != MOVE_CAPTURE)
			return false;

		u64 attacks_bb;
		switch (get_piece_type(piece)) {
		case PIECE_TYPE_KNIGHT:
			attacks_bb = get_knight_attacks(from);
			break;
		case PIECE_TYPE_BISHOP:
			attacks_bb = get_bishop_attacks(from, occ);
			break;
		case PIECE_TYPE_ROOK:
			attacks_bb = get_rook_attacks(from, occ);
			break;
		case PIECE_TYPE_QUEEN:
			attacks_bb = get_queen_attacks(from, occ);
			break;
		case PIECE_TYPE_KING:
			attacks_bb = get_king_attacks(from);
			break;
		default:
			abort();
		}
		if (!(attacks_bb & target_bb))
			return false;
		if (attacks_bb & target_bb & enemy_bb) {
			if (!move_is_capture(move))
				return false;
		}
	}

	return true;
}

bool square_is_attacked_by_pawn(Square sq, Color by_side, const Position *pos)
{
	const Piece p = by_side == COLOR_WHITE ? PIECE_WHITE_PAWN :
						 PIECE_BLACK_PAWN;
	const u64 bb = get_piece_bitboard(pos, p);
	/* We use the symmetry of pawn attacks to check if there is a pawn of
	 * color c attacking square sq. */
	const u64 attackers = get_pawn_attacks(sq, !by_side) & bb;
	if (attackers)
		return true;
	return false;
}

/*
 * This function returns true if the square sq is being attacked by any of the
 * opponent's pieces. It does so by generating attacks from the attacked square
 * and checking if one of the squares in this attack set has a piece, which
 * works because "square x attacked by square y" is a symmetric relation in the
 * context of one piece type.
 */
bool is_square_attacked(Square sq, Color by_side, const Position *restrict pos)
{
	const u64 occ = get_color_bitboard(pos, by_side) |
			get_color_bitboard(pos, !by_side);

	Piece piece = create_piece(PIECE_TYPE_PAWN, by_side);
	const u64 pawns = get_piece_bitboard(pos, piece);
	if (get_pawn_attacks(sq, !by_side) & pawns)
		return true;
	piece = create_piece(PIECE_TYPE_KNIGHT, by_side);
	const u64 knights = get_piece_bitboard(pos, piece);
	if (get_knight_attacks(sq) & knights)
		return true;
	piece = create_piece(PIECE_TYPE_ROOK, by_side);
	u64 rooks_queens = get_piece_bitboard(pos, piece);
	piece = create_piece(PIECE_TYPE_QUEEN, by_side);
	rooks_queens |= get_piece_bitboard(pos, piece);
	if (get_rook_attacks(sq, occ) & rooks_queens)
		return true;
	u64 bishops_queens = get_piece_bitboard(pos, piece);
	piece = create_piece(PIECE_TYPE_BISHOP, by_side);
	bishops_queens |= get_piece_bitboard(pos, piece);
	if (get_bishop_attacks(sq, occ) & bishops_queens)
		return true;
	piece = create_piece(PIECE_TYPE_KING, by_side);
	const u64 king = get_piece_bitboard(pos, piece);
	if (get_king_attacks(sq) & king)
		return true;
	return false;
}

/*
 * Adds the generated moves to the moves array and returns the number of moves.
 * The length of moves must be at least 256 (the maximum number of moves in a
 * chess position seems to be 218 but we use 256 just in case, and also because
 * powers of 2 are cool.)
 */
int get_pseudo_legal_moves(struct move_with_score *moves,
			   enum move_gen_type type,
			   const Position *restrict pos)
{
	MoveList list;
	list.capacity = 256;
	list.ptr = moves;
	list.len = 0;

	gen_moves(&list, PIECE_TYPE_PAWN, type, pos);
	gen_moves(&list, PIECE_TYPE_KNIGHT, type, pos);
	gen_moves(&list, PIECE_TYPE_ROOK, type, pos);
	gen_moves(&list, PIECE_TYPE_BISHOP, type, pos);
	gen_moves(&list, PIECE_TYPE_QUEEN, type, pos);
	gen_moves(&list, PIECE_TYPE_KING, type, pos);

	return list.len;
}

/*
 * Returns a bitboard of the pieces from both sides attacking a square. Note
 * that this only counts pieces that are attacking a square directly, so a rook
 * behind another rook will not be included.
 */
u64 get_attackers(Square sq, const Position *restrict pos)
{
	const u64 occ = get_color_bitboard(pos, COLOR_WHITE) |
			get_color_bitboard(pos, COLOR_BLACK);
	u64 (*const get_bb)(const Position *, Piece) = get_piece_bitboard;

	const u64 white_pawns = get_bb(pos, PIECE_WHITE_PAWN);
	const u64 black_pawns = get_bb(pos, PIECE_BLACK_PAWN);
	const u64 knights = get_bb(pos, PIECE_WHITE_KNIGHT) |
			    get_bb(pos, PIECE_BLACK_KNIGHT);
	const u64 kings = get_bb(pos, PIECE_WHITE_KING) |
			  get_bb(pos, PIECE_BLACK_KING);
	const u64 bishops_queens = get_bb(pos, PIECE_WHITE_QUEEN) |
				   get_bb(pos, PIECE_BLACK_QUEEN) |
				   get_bb(pos, PIECE_WHITE_BISHOP) |
				   get_bb(pos, PIECE_BLACK_BISHOP);
	const u64 rooks_queens = get_bb(pos, PIECE_WHITE_QUEEN) |
				 get_bb(pos, PIECE_BLACK_QUEEN) |
				 get_bb(pos, PIECE_WHITE_ROOK) |
				 get_bb(pos, PIECE_BLACK_ROOK);

	return (get_pawn_attacks(sq, COLOR_BLACK) & white_pawns) |
	       (get_pawn_attacks(sq, COLOR_WHITE) & black_pawns) |
	       (get_knight_attacks(sq) & knights) |
	       (get_king_attacks(sq) & kings) |
	       (get_bishop_attacks(sq, occ) & bishops_queens) |
	       (get_rook_attacks(sq, occ) & rooks_queens);
}

u64 movegen_perft(Position *restrict pos, int depth)
{
	u64 nodes = 0;

	if (!depth)
		return 1;
	struct move_with_score moves[256];
	int len = get_pseudo_legal_moves(moves, MOVE_GEN_TYPE_CAPTURE, pos);
	len += get_pseudo_legal_moves(moves + len, MOVE_GEN_TYPE_QUIET, pos);
	for (int i = 0; i < len; ++i) {
		Move move = moves[i].move;
		if (!move_is_legal(pos, move))
			continue;
		do_move(pos, move);
		nodes += movegen_perft(pos, depth - 1);
		undo_move(pos, move);
	}
	return nodes;
}

u64 get_west_ray(Square sq)
{
	return (1ull << sq) - (1ull << (sq & 56));
}

u64 get_east_ray(Square sq)
{
	return 2 * ((1ull << (sq | 7)) - (1ull << sq));
}

u64 get_southwest_ray(Square sq)
{
	u64 ray = shift_bb_west(U64(0x0040201008040201), 7 - (int)get_file(sq));
	ray >>= (7 - get_rank(sq)) * 8;
	return ray;
}

u64 get_southeast_ray(Square sq)
{
	u64 ray = shift_bb_east(U64(0x0002040810204080), (int)get_file(sq));
	ray >>= (7 - get_rank(sq)) * 8;
	return ray;
}

u64 get_northwest_ray(Square sq)
{
	u64 ray = shift_bb_west(U64(0x0102040810204000), 7 - (int)get_file(sq));
	ray <<= get_rank(sq) * 8;
	return ray;
}

u64 get_northeast_ray(Square sq)
{
	return shift_bb_east(U64(0x8040201008040200), (int)get_file(sq))
	       << (get_rank(sq) * 8);
}

u64 get_south_ray(Square sq)
{
	return U64(0x0080808080808080) >> (sq ^ 63);
}

u64 get_north_ray(Square sq)
{
	return U64(0x0101010101010100) << sq;
}

u64 shift_bb_southwest(u64 bb, int n)
{
	bb = shift_bb_south(bb, n);
	bb = shift_bb_west(bb, n);
	return bb;
}

u64 shift_bb_southeast(u64 bb, int n)
{
	bb = shift_bb_south(bb, n);
	bb = shift_bb_east(bb, n);
	return bb;
}

u64 shift_bb_northwest(u64 bb, int n)
{
	bb = shift_bb_north(bb, n);
	bb = shift_bb_west(bb, n);
	return bb;
}

u64 shift_bb_northeast(u64 bb, int n)
{
	bb = shift_bb_north(bb, n);
	bb = shift_bb_east(bb, n);
	return bb;
}

u64 shift_bb_south(u64 bb, int n)
{
	return bb >> 8 * n;
}

u64 shift_bb_north(u64 bb, int n)
{
	return bb << 8 * n;
}

/*
 * Right shift bits, removing bits that are pushed to file H.
 */
u64 shift_bb_west(u64 bb, int n)
{
	for (int i = 0; i < n; ++i)
		bb = (bb >> 1) & ~file_bitboards[FILE_H];
	return bb;
}

/*
 * Left shift bits, removing bits that are pushed to file A.
 */
u64 shift_bb_east(u64 bb, int n)
{
	for (int i = 0; i < n; ++i)
		bb = (bb << 1) & ~file_bitboards[FILE_A];
	return bb;
}

static void gen_moves(MoveList *restrict list, PieceType piece_type,
		      enum move_gen_type type, const Position *restrict pos)
{
	switch (piece_type) {
	case PIECE_TYPE_PAWN:
		gen_pawn_moves(list, type, pos);
		return;
	case PIECE_TYPE_KING:
		gen_king_moves(list, type, pos);
		return;
	default:
		break;
	}

	const Color color = get_side_to_move(pos);
	const Piece piece = create_piece(piece_type, color);
	const u64 occ = get_color_bitboard(pos, color) |
			get_color_bitboard(pos, !color);
	const u64 enemy_pieces = get_color_bitboard(pos, !color);

	u64 bb = get_piece_bitboard(pos, piece);
	while (bb) {
		const Square from = (Square)unset_ls1b(&bb);
		u64 targets = 0;
		switch (piece_type) {
		case PIECE_TYPE_KNIGHT:
			targets = get_knight_attacks(from);
			break;
		case PIECE_TYPE_BISHOP:
			targets = get_bishop_attacks(from, occ);
			break;
		case PIECE_TYPE_ROOK:
			targets = get_rook_attacks(from, occ);
			break;
		case PIECE_TYPE_QUEEN:
			targets = get_queen_attacks(from, occ);
			break;
		default:
			abort();
		}
		if (type == MOVE_GEN_TYPE_CAPTURE)
			targets &= enemy_pieces;
		else
			targets &= ~occ;
		while (targets) {
			const Square to = (Square)unset_ls1b(&targets);
			const Move move =
				get_piece_at(pos, to) == PIECE_NONE ?
					create_move(from, to, MOVE_OTHER) :
					create_move(from, to, MOVE_CAPTURE);
			add_move(list, move);
		}
	}
}

static void gen_king_moves(MoveList *restrict list, enum move_gen_type type,
			   const Position *restrict pos)
{
	const Color color = get_side_to_move(pos);
	const Square from = get_king_square(pos, color);
	const u64 occ = get_color_bitboard(pos, color) |
			get_color_bitboard(pos, !color);
	const u64 enemy_pieces = get_color_bitboard(pos, !color);

	if (type != MOVE_GEN_TYPE_CAPTURE) {
		gen_king_castling(list, pos);
		gen_queen_castling(list, pos);
	}

	u64 targets;
	if (type == MOVE_GEN_TYPE_CAPTURE)
		targets = get_king_attacks(from) & enemy_pieces;
	else
		targets = get_king_attacks(from) & ~occ;
	while (targets) {
		const Square to = (Square)unset_ls1b(&targets);
		const Move move = get_piece_at(pos, to) == PIECE_NONE ?
					  create_move(from, to, MOVE_OTHER) :
					  create_move(from, to, MOVE_CAPTURE);
		add_move(list, move);
	}
}

static void gen_queen_castling(MoveList *restrict list,
			       const Position *restrict pos)
{
	const Color color = get_side_to_move(pos);
	const Square from = get_king_square(pos, color);

	if (has_castling_right(pos, color, CASTLING_SIDE_QUEEN)) {
		const Square q_castling_test_sq1 = color == COLOR_WHITE ? D1 :
									  D8;
		const Square q_castling_test_sq2 = color == COLOR_WHITE ? C1 :
									  C8;
		const Square q_castling_test_sq3 = color == COLOR_WHITE ? B1 :
									  B8;
		if (get_piece_at(pos, q_castling_test_sq1) == PIECE_NONE &&
		    get_piece_at(pos, q_castling_test_sq2) == PIECE_NONE &&
		    get_piece_at(pos, q_castling_test_sq3) == PIECE_NONE &&
		    !is_square_attacked(q_castling_test_sq1, !color, pos) &&
		    !is_square_attacked(q_castling_test_sq2, !color, pos) &&
		    !is_square_attacked(from, !color, pos)) {
			const Square to = color == COLOR_WHITE ? C1 : C8;
			const Move move =
				create_move(from, to, MOVE_QUEEN_CASTLE);
			add_move(list, move);
		}
	}
}

static void gen_king_castling(MoveList *restrict list,
			      const Position *restrict pos)
{
	const Color color = get_side_to_move(pos);
	const Square from = get_king_square(pos, color);

	if (has_castling_right(pos, color, CASTLING_SIDE_KING)) {
		const Square k_castling_test_sq1 = color == COLOR_WHITE ? F1 :
									  F8;
		const Square k_castling_test_sq2 = color == COLOR_WHITE ? G1 :
									  G8;
		if (get_piece_at(pos, k_castling_test_sq1) == PIECE_NONE &&
		    get_piece_at(pos, k_castling_test_sq2) == PIECE_NONE &&
		    !is_square_attacked(k_castling_test_sq1, !color, pos) &&
		    !is_square_attacked(k_castling_test_sq2, !color, pos) &&
		    !is_square_attacked(from, !color, pos)) {
			const Square to = color == COLOR_WHITE ? G1 : G8;
			const Move move =
				create_move(from, to, MOVE_KING_CASTLE);
			add_move(list, move);
		}
	}
}

static void gen_pawn_moves(MoveList *restrict list, enum move_gen_type type,
			   const Position *restrict pos)
{
	const Color color = get_side_to_move(pos);
	const Piece piece = create_piece(PIECE_TYPE_PAWN, color);
	const u64 enemy_pieces = get_color_bitboard(pos, !color);
	const u64 occ = enemy_pieces | get_color_bitboard(pos, color);

	u64 bb = get_piece_bitboard(pos, piece);
	if (has_en_passant_square(pos) && type == MOVE_GEN_TYPE_CAPTURE) {
		const Square sq = get_en_passant_square(pos);

		if (!square_is_attacked_by_pawn(sq, color, pos))
			goto next;

		u64 attackers = get_pawn_attacks(sq, !color) & bb;
		while (attackers) {
			const Square from = (Square)unset_ls1b(&attackers);
			const Square to = sq;
			const Move move =
				create_move(from, to, MOVE_EP_CAPTURE);
			add_move(list, move);
		}
	}
next:

	while (bb) {
		const Square from = (Square)unset_ls1b(&bb);
		if (type == MOVE_GEN_TYPE_CAPTURE) {
			gen_pawn_attacks(list, from, enemy_pieces, color);
		} else if (type == MOVE_GEN_TYPE_QUIET) {
			gen_pushes(list, from, occ, color);
			gen_double_pushes(list, from, occ, color);
		}
	}
}

static void gen_pawn_attacks(MoveList *restrict list, Square from,
			     u64 enemy_pieces, Color color)
{
	u64 targets = get_pawn_attacks(from, color) & enemy_pieces;
	while (targets) {
		const Square to = (Square)unset_ls1b(&targets);
		if ((color == COLOR_WHITE && get_rank(to) == RANK_8) ||
		    (color == COLOR_BLACK && get_rank(to) == RANK_1)) {
			for (MoveType move_type = MOVE_KNIGHT_PROMOTION_CAPTURE;
			     move_type <= MOVE_QUEEN_PROMOTION_CAPTURE;
			     ++move_type) {
				const Move move =
					create_move(from, to, move_type);
				add_move(list, move);
			}
		} else {
			const Move move = create_move(from, to, MOVE_CAPTURE);
			add_move(list, move);
		}
	}
}

static void gen_double_pushes(MoveList *restrict list, Square from, u64 occ,
			      Color color)
{
	u64 targets = get_double_push(from, occ, color);
	if (targets) {
		const Square to = (Square)get_ls1b(targets);
		const Move move = create_move(from, to, MOVE_DOUBLE_PAWN_PUSH);
		add_move(list, move);
	}
}

static void gen_pushes(MoveList *restrict list, Square from, u64 occ,
		       Color color)
{
	u64 targets = get_single_push(from, occ, color);
	if (targets) {
		const Square to = (Square)get_ls1b(targets);
		if ((color == COLOR_WHITE && get_rank(to) == RANK_8) ||
		    (color == COLOR_BLACK && get_rank(to) == RANK_1)) {
			for (MoveType move_type = MOVE_KNIGHT_PROMOTION;
			     move_type <= MOVE_QUEEN_PROMOTION; ++move_type) {
				const Move move =
					create_move(from, to, move_type);
				add_move(list, move);
			}
		} else {
			const Move move = create_move(from, to, MOVE_OTHER);
			add_move(list, move);
		}
	}
}

static void add_move(MoveList *restrict list, Move move)
{
	struct move_with_score move_with_score;
	move_with_score.move = move;
	move_with_score.score = 0;
	list->ptr[list->len] = move_with_score;
	++list->len;
}

static u64 get_king_attacks(Square sq)
{
	return king_attack_table[sq];
}

static u64 get_queen_attacks(Square sq, u64 occ)
{
	return get_rook_attacks(sq, occ) | get_bishop_attacks(sq, occ);
}

static u64 get_rook_attacks(Square sq, u64 occ)
{
	const u64 *const aptr = rook_magics[sq].ptr;
#ifdef USE_BMI2
	return aptr[pext(occ, rook_magics[sq].mask)];
#endif
	occ &= rook_magics[sq].mask;
	occ *= rook_magics[sq].num;
	occ >>= rook_magics[sq].shift;
	return aptr[occ];
}

__attribute__((noinline)) static u64 get_bishop_attacks(Square sq, u64 occ)
{
	const u64 *const aptr = bishop_magics[sq].ptr;
#ifdef USE_BMI2
	return aptr[pext(occ, bishop_magics[sq].mask)];
#endif
	occ &= bishop_magics[sq].mask;
	occ *= bishop_magics[sq].num;
	occ >>= bishop_magics[sq].shift;
	return aptr[occ];
}

static u64 get_knight_attacks(Square sq)
{
	return knight_attack_table[sq];
}

u64 get_pawn_attacks(Square sq, Color c)
{
	const u64 bb = U64(0x1) << sq;
	if (c == COLOR_WHITE)
		return shift_bb_northeast(bb, 1) | shift_bb_northwest(bb, 1);
	else
		return shift_bb_southeast(bb, 1) | shift_bb_southwest(bb, 1);
}

static u64 get_double_push(Square sq, u64 occ, Color c)
{
	if (c == COLOR_WHITE) {
		const u64 single_push = get_single_push(sq, occ, c);
		return shift_bb_north(single_push, 1) & ~occ &
		       rank_bitboards[RANK_4];
	} else {
		const u64 single_push = get_single_push(sq, occ, c);
		return shift_bb_south(single_push, 1) & ~occ &
		       rank_bitboards[RANK_5];
	}
}

static u64 get_single_push(Square sq, u64 occ, Color c)
{
	const u64 bb = U64(0x1) << sq;
	if (c == COLOR_WHITE)
		return shift_bb_north(bb, 1) & ~occ;
	else
		return shift_bb_south(bb, 1) & ~occ;
}

static void init_king_attacks(void)
{
	for (int i = 0; i < 64; ++i) {
		u64 bb = U64(0x1) << i;
		king_attack_table[i] = shift_bb_east(bb, 1) |
				       shift_bb_west(bb, 1);
		bb |= king_attack_table[i];
		king_attack_table[i] |= shift_bb_north(bb, 1) |
					shift_bb_south(bb, 1);
	}
}

static void init_rook_attacks(void)
{
	init_sliding_atacks(PIECE_TYPE_ROOK);
}

static void init_bishop_attacks(void)
{
	init_sliding_atacks(PIECE_TYPE_BISHOP);
}

static void init_knight_attacks(void)
{
	for (int sq = A1; sq <= H8; ++sq) {
		const u64 bb = U64(0x1) << sq;
		const u64 l1 = (bb >> 1) & U64(0x7f7f7f7f7f7f7f7f);
		const u64 l2 = (bb >> 2) & U64(0x3f3f3f3f3f3f3f3f);
		const u64 r1 = (bb << 1) & U64(0xfefefefefefefefe);
		const u64 r2 = (bb << 2) & U64(0xfcfcfcfcfcfcfcfc);
		const u64 h1 = l1 | r1;
		const u64 h2 = l2 | r2;
		knight_attack_table[sq] = (h1 << 16) | (h1 >> 16) | (h2 << 8) |
					  (h2 >> 8);
	}
}

static void init_sliding_atacks(PieceType pt)
{
	static u64 occ[4096], ref[4096];
	static unsigned attempts[4096];

	u64 (*const gen)(enum square, u64) = pt == PIECE_TYPE_BISHOP ?
						     slow_get_bishop_attacks :
						     slow_get_rook_attacks;
	u64 *table = pt == PIECE_TYPE_BISHOP ? bishop_attack_table :
					       rook_attack_table;
	struct magic *magics = pt == PIECE_TYPE_BISHOP ? bishop_magics :
							 rook_magics;

	size_t size;
	for (Square sq = A1; sq <= H8; ++sq) {
		const File f = get_file(sq);
		const Rank r = get_rank(sq);

		const u64 edges =
			((file_bitboards[FILE_A] | file_bitboards[FILE_H]) &
			 ~file_bitboards[f]) |
			((rank_bitboards[RANK_1] | rank_bitboards[RANK_8]) &
			 ~rank_bitboards[r]);

		Magic *const m = &magics[sq];
		m->mask = gen(sq, 0) & ~edges;
		m->shift = 64 - popcnt(m->mask);
		m->ptr = sq == A1 ? table : magics[sq - 1].ptr + size;

		size = 0;
		u64 bb = 0;
		do {
			occ[size] = bb;
			ref[size] = gen(sq, bb);

			/* With BMI2 we have the PEXT instruction which allows
			 * us to extract the 1 bits from the occupancies
			 * without the need for magic numbers. */
#ifdef USE_BMI2
			m->ptr[pext(bb, m->mask)] = ref[size];
#endif

			/* This is the Carry-Rippler method to generate all
			 * possible permutations of bits along the mask. */
			bb = (bb - m->mask) & m->mask;
			++size;
		} while (bb);

#ifdef USE_BMI2
		continue;
#endif

		memset(attempts, 0, sizeof(attempts));
		unsigned current_attempt = 0;
		for (size_t i = 0; i < size;) {
			m->num = 0;
			while (popcnt((m->num * m->mask) >> 56) < 6)
				m->num = next_sparse_rand();
			++current_attempt;
			for (i = 0; i < size; ++i) {
				u64 idx = (occ[i] * m->num) >> m->shift;
				if (attempts[idx] < current_attempt) {
					attempts[idx] = current_attempt;
					m->ptr[idx] = ref[i];
				} else if (m->ptr[idx] != ref[i]) {
					break;
				}
			}
		}
	}
}

static u64 slow_get_rook_attacks(Square sq, u64 occ)
{
	return gen_ray_attacks(occ, NORTH, sq) |
	       gen_ray_attacks(occ, EAST, sq) |
	       gen_ray_attacks(occ, SOUTH, sq) | gen_ray_attacks(occ, WEST, sq);
}

static u64 slow_get_bishop_attacks(Square sq, u64 occ)
{
	return gen_ray_attacks(occ, NORTHEAST, sq) |
	       gen_ray_attacks(occ, SOUTHEAST, sq) |
	       gen_ray_attacks(occ, SOUTHWEST, sq) |
	       gen_ray_attacks(occ, NORTHWEST, sq);
}

static u64 gen_ray_attacks(u64 occ, Direction dir, Square sq)
{
	static const u64 dir_mask[] = {
		[NORTH] = 0x0,	   [SOUTH] = U64(0xffffffffffffffff),
		[NORTHEAST] = 0x0, [SOUTHEAST] = U64(0xffffffffffffffff),
		[EAST] = 0x0,	   [WEST] = U64(0xffffffffffffffff),
		[NORTHWEST] = 0x0, [SOUTHWEST] = U64(0xffffffffffffffff),
	};
	static const u64 dir_bit[] = {
		[SOUTH] = 0x1,	   [NORTH] = U64(0x8000000000000000),
		[SOUTHEAST] = 0x1, [NORTHEAST] = U64(0x8000000000000000),
		[WEST] = 0x1,	   [EAST] = U64(0x8000000000000000),
		[SOUTHWEST] = 0x1, [NORTHWEST] = U64(0x8000000000000000),
	};

	const u64 attacks = ray_bitboards[dir][sq];
	u64 blockers = attacks & occ;
	blockers |= dir_bit[dir];
	blockers &= -blockers | dir_mask[dir];
	sq = (Square)get_ms1b(blockers);
	return attacks ^ ray_bitboards[dir][sq];
}

static void init_rays(void)
{
	for (Square sq = A1; sq <= H8; ++sq) {
		ray_bitboards[NORTH][sq] = get_north_ray(sq);
		ray_bitboards[SOUTH][sq] = get_south_ray(sq);
		ray_bitboards[NORTHEAST][sq] = get_northeast_ray(sq);
		ray_bitboards[NORTHWEST][sq] = get_northwest_ray(sq);
		ray_bitboards[SOUTHEAST][sq] = get_southeast_ray(sq);
		ray_bitboards[SOUTHWEST][sq] = get_southwest_ray(sq);
		ray_bitboards[EAST][sq] = get_east_ray(sq);
		ray_bitboards[WEST][sq] = get_west_ray(sq);
	}
}

#ifdef TEST

#include <unity/unity.h>

static void recursively_test_move_is_pseudo_legal_true(Position *pos,
						       int depth);
static void test_move_is_pseudo_legal(void);

void test_movegen(void)
{
	test_move_is_pseudo_legal();
}

/*
 * Test if the function move_is_pseudo_legal is correct by testing it with all
 * the positions and moves in a game tree with some depth.
 */
static void recursively_test_move_is_pseudo_legal_true(Position *pos, int depth)
{
	if (!depth)
		return;
	struct move_with_score moves[256];
	int nb = get_pseudo_legal_moves(moves, MOVE_GEN_TYPE_CAPTURE, pos);
	nb += get_pseudo_legal_moves(moves + nb, MOVE_GEN_TYPE_QUIET, pos);
	for (int i = 0; i < nb; ++i) {
		const Move move = moves[i].move;
		if (!move_is_legal(pos, move))
			continue;

		char fen[512];
		get_fen(fen, pos);
		char lan[MAX_LAN_LEN + 1];
		move_to_lan(lan, move);
		char *fail_message;
		my_asprintf(&fail_message,
			    "Move '%s' from position '%s' is pseudo-legal but "
			    "move_is_pseudo_legal returned false.\n",
			    lan, fen);
		TEST_ASSERT_MESSAGE(move_is_pseudo_legal(move, pos),
				    fail_message);
		free(fail_message);

		do_move(pos, move);
		recursively_test_move_is_pseudo_legal_true(pos, depth - 1);
		undo_move(pos, move);
	}
}

static void test_move_is_pseudo_legal(void)
{
	/* clang-format off */
	/* These are moves that are not pseudo-legal from their respective positions. */
	const struct data {
		const char *fen;
		const Move move;
	} false_data[] = {
		/* MOVE_OTHER */
		{"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", create_move(G1, G3, MOVE_OTHER)},
		{"3r3r/p3kpnp/1pp3p1/2p2b2/2P1NP2/1P2P3/P4PBP/2KR3R w - - 1 17", create_move(A1, A2, MOVE_OTHER)},

		/* MOVE_DOUBLE_PAWN_PUSH */
		{"8/8/p3B3/Pp2P3/1P1P3k/2Ppn2r/1B6/3R2K1 b - - 7 53", create_move(D3, D1, MOVE_DOUBLE_PAWN_PUSH)},
		{"r1bqkb1r/pp1p1ppp/2n2n2/2p1p3/2PP4/2N1PN2/PP3PPP/R1BQKB1R b KQkq - 0 5", create_move(D4, D6, MOVE_DOUBLE_PAWN_PUSH)},

		/* MOVE_CAPTURE */
		{"3r3r/p3kpnp/1pp3p1/2p2b2/2P2P2/1P2P1N1/P4PBP/2KR3R b - - 2 17", create_move(F5, G6, MOVE_CAPTURE)},
		{"r1bqkb1r/pp1p1ppp/2n2n2/2p5/2PPp3/2N1PN2/PP3PPP/R1BQKB1R w KQkq - 0 6", create_move(D4, E5, MOVE_CAPTURE)},
		{"2kr1bnr/2q2ppp/p1p1p3/2P1P3/1PbB2PP/P1N2P2/2P1N3/R2QK2R w KQ - 1 16", create_move(C5, D6, MOVE_CAPTURE)},
		{"8/8/p3B3/Pp2P3/1P1P3k/2Ppn2r/1B6/3R2K1 b - - 7 53", create_move(D3, C4, MOVE_OTHER)},

		/* MOVE_KING_CASTLE */
		{"r1bk3r/p4pbp/1pp2np1/2p5/2P5/1PN1PP2/PB3P1P/R3KB1R w KQ - 0 12", create_move(E1, G1, MOVE_KING_CASTLE)},
		{"r1bk3r/pp3pbp/2p2np1/2p5/2P5/1PN1PP2/PB3P1P/R3KB1R b KQ - 2 11", create_move(D1, G1, MOVE_KING_CASTLE)},
		{"r1bk3r/pp3pbp/2p2np1/2p5/2P5/1PN1PP2/PB3P1P/R3KB1R b KQ - 2 11", create_move(E1, G1, MOVE_KING_CASTLE)},

		/* MOVE_QUEEN_CASTLE */

		/* MOVE_QUEEN_PROMOTION and MOVE_QUEEN_PROMOTION_CAPTURE.
		 * Other promotion types probably won't change anything. */
		{"8/8/2P5/3R4/4k3/1P6/2K1p3/8 b - - 0 52", create_move(E2, D1, MOVE_QUEEN_PROMOTION)},
		{"8/8/2P5/3R4/4k3/1P6/2K1p3/8 b - - 0 52", create_move(E2, D1, MOVE_QUEEN_PROMOTION_CAPTURE)},
		{"8/8/2P5/3R4/4k3/1P6/2K1p3/8 b - - 0 52", create_move(C6, C8, MOVE_QUEEN_PROMOTION)},

		/* MOVE_EP_CAPTURE */
		{"r1bqkb1r/pp1p1ppp/2n2n2/2p3N1/2PPp3/2N1P3/PP3PPP/R1BQKB1R b KQkq - 1 6", create_move(E4, D3, MOVE_EP_CAPTURE)},
		{"2kr1bnr/2q2ppp/p1p1p3/2P1P3/1PbB2PP/P1N2P2/2P1N3/R2QK2R w KQ - 1 16", create_move(C5, D6, MOVE_EP_CAPTURE)},
	};
	/* We use these positions to test that the function returns true for all
	 * moves in the game tree. We use positions in different game phases of
	 * the game to make sure we get to see a move diverse set of moves (for
	 * example, promotions might not be possible from the starting position
	 * if the depth is too low. */
	const char *phases_fen[] = {
		/* Start position */
		"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
		/* Middlegame */
		"r2rq1k1/1b1n1pbp/3Bpnp1/p7/2B1P3/P4N2/3NQPPP/1R1R2K1 b - - 1 19",
		/* Endgame */
		"8/8/6p1/3B4/R3P2P/2bbk1P1/8/2K2Rr1 b - - 4 56",
	};
	/* clang-format on */

	Position *pos = malloc(sizeof(Position));
	for (size_t i = 0; i < sizeof(phases_fen) / sizeof(phases_fen[0]);
	     ++i) {
		init_position(pos, phases_fen[i]);
		recursively_test_move_is_pseudo_legal_true(pos, 5);
	}

	for (size_t i = 0; i < sizeof(false_data) / sizeof(false_data[0]);
	     ++i) {
		const char *fen = false_data[i].fen;
		init_position(pos, false_data[i].fen);
		const Move move = false_data[i].move;
		char lan[MAX_LAN_LEN + 1];
		move_to_lan(lan, move);
		char *fail_message;
		my_asprintf(
			&fail_message,
			"Move '%s' from position '%s' (in data[%zu] is not pseudo-legal but move_is_pseudo_legal returned true.\n",
			lan, fen, i);
		TEST_ASSERT_MESSAGE(!move_is_pseudo_legal(move, pos),
				    fail_message);
		free(fail_message);
	}

	free(pos);
}

#endif
