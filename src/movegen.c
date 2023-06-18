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
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "bit.h"
#include "pos.h"
#include "move.h"
#include "movegen.h"
#include "rng.h"

typedef u64 SlidingAttackGenerator(Square, u64);
typedef u64 AttackGenerator(Square);

typedef struct magic {
	u64 *ptr;
	u64 mask;
	u64 num;
	int shift;
} Magic;

typedef struct move_list {
	Move *ptr;
	size_t capacity;
	size_t len;
} MoveList;

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
	U64(0x00000000000000ff),
	U64(0x000000000000ff00),
	U64(0x0000000000ff0000),
	U64(0x00000000ff000000),
	U64(0x000000ff00000000),
	U64(0x0000ff0000000000),
	U64(0x00ff000000000000),
	U64(0xff00000000000000),
};
static const u64 file_bitboards[] = {
	U64(0x0101010101010101),
	U64(0x0202020202020202),
	U64(0x0404040404040404),
	U64(0x0808080808080808),
	U64(0x1010101010101010),
	U64(0x2020202020202020),
	U64(0x4040404040404040),
	U64(0x8080808080808080),
};
static Magic rook_magics[64];
static Magic bishop_magics[64];
static u64 ray_bitboards[8][64];
static u64 knight_attack_table[64];
static u64 rook_attack_table[0x19000];
static u64 bishop_attack_table[0x1480];
static u64 king_attack_table[64];

/*
 * Right shift bits, removing bits that are pushed to file H.
 */
static u64 move_west(u64 bb, int n)
{
	for (int i = 0; i < n; ++i)
		bb = (bb >> 1) & ~file_bitboards[FILE_H];
	return bb;
}

/*
 * Left shift bits, removing bits that are pushed to file A.
 */
static u64 move_east(u64 bb, int n)
{
	for (int i = 0; i < n; ++i)
		bb = (bb << 1) & ~file_bitboards[FILE_A];
	return bb;
}

static u64 move_south(u64 bb, int n)
{
	return bb >> 8 * n;
}

static u64 move_north(u64 bb, int n)
{
	return bb << 8 * n;
}

static u64 move_southwest(u64 bb, int n)
{
	bb = move_south(bb, n);
	bb = move_west(bb, n);
	return bb;
}

static u64 move_southeast(u64 bb, int n)
{
	bb = move_south(bb, n);
	bb = move_east(bb, n);
	return bb;
}

static u64 move_northwest(u64 bb, int n)
{
	bb = move_north(bb, n);
	bb = move_west(bb, n);
	return bb;
}

static u64 move_northeast(u64 bb, int n)
{
	bb = move_north(bb, n);
	bb = move_east(bb, n);
	return bb;
}

static u64 get_north_ray(Square sq)
{
	return U64(0x0101010101010100) << sq;
}

static u64 get_south_ray(Square sq)
{
	return U64(0x0080808080808080) >> (sq ^ 63);
}

static u64 get_northeast_ray(Square sq)
{
	return move_east(U64(0x8040201008040200), pos_get_file(sq)) <<
	(pos_get_rank(sq) * 8);
}

static u64 get_northwest_ray(Square sq)
{
	u64 ray = move_west(U64(0x0102040810204000),
	                    7 - pos_get_file(sq));
	ray <<= pos_get_rank(sq) * 8;
	return ray;
}

static u64 get_southeast_ray(Square sq)
{
	u64 ray = move_east(U64(0x0002040810204080), pos_get_file(sq));
	ray >>= (7 - pos_get_rank(sq)) * 8;
	return ray;
}

static u64 get_southwest_ray(Square sq)
{
	u64 ray = move_west(U64(0x0040201008040201),
	                    7 - pos_get_file(sq));
	ray >>= (7 - pos_get_rank(sq)) * 8;
	return ray;
}

static u64 get_east_ray(Square sq)
{
	return 2 * ((1ull << (sq | 7)) - (1ull << sq));
}

static u64 get_west_ray(Square sq)
{
	return (1ull << sq) - (1ull << (sq & 56));
}

static void init_rays(void)
{
	for (Square sq = A1; sq <= H8; ++sq) {
		ray_bitboards[NORTH][sq]     = get_north_ray(sq);
		ray_bitboards[SOUTH][sq]     = get_south_ray(sq);
		ray_bitboards[NORTHEAST][sq] = get_northeast_ray(sq);
		ray_bitboards[NORTHWEST][sq] = get_northwest_ray(sq);
		ray_bitboards[SOUTHEAST][sq] = get_southeast_ray(sq);
		ray_bitboards[SOUTHWEST][sq] = get_southwest_ray(sq);
		ray_bitboards[EAST][sq]      = get_east_ray(sq);
		ray_bitboards[WEST][sq]      = get_west_ray(sq);
	}
}

/*
 * This is a slow approach to generate ray attacks for sliding pieces, it uses
 * a generalized bit scan to share the same code for all directions. The
 * dir_bit values are used to ensure an empty board is never scanned, it uses
 * the first square for negative directions and the last square for positive
 * ones. The reason is that these squares are going to be returned by the scan
 * function and the ray_bitboards table will return an empty ray bitboard for
 * square 0 when the direction is south, west, southeast or southwest and for
 * square 63 when the direction is north, east, notheast or northwest, since
 * there are no squares to those directions beyond those two squares.
 */
static u64 gen_ray_attacks(u64 occ, Direction dir, Square sq)
{
	static const u64 dir_mask[] = {
		[NORTH    ] = 0x0, [SOUTH    ] = U64(0xffffffffffffffff),
		[NORTHEAST] = 0x0, [SOUTHEAST] = U64(0xffffffffffffffff),
		[EAST     ] = 0x0, [WEST     ] = U64(0xffffffffffffffff),
		[NORTHWEST] = 0x0, [SOUTHWEST] = U64(0xffffffffffffffff),
	};
	static const u64 dir_bit[] = {
		[SOUTH    ] = 0x1, [NORTH    ] = U64(0x8000000000000000),
		[SOUTHEAST] = 0x1, [NORTHEAST] = U64(0x8000000000000000),
		[WEST     ] = 0x1, [EAST     ] = U64(0x8000000000000000),
		[SOUTHWEST] = 0x1, [NORTHWEST] = U64(0x8000000000000000),
	};

	const u64 attacks = ray_bitboards[dir][sq];
	u64 blockers = attacks & occ;
	blockers |= dir_bit[dir];
	blockers &= -blockers | dir_mask[dir];
	sq        = get_ms1b(blockers);
	return attacks ^ ray_bitboards[dir][sq];
}

static u64 slow_gen_bishop_attacks(Square sq, u64 occ)
{
	return gen_ray_attacks(occ, NORTHEAST, sq) |
	       gen_ray_attacks(occ, SOUTHEAST, sq) |
	       gen_ray_attacks(occ, SOUTHWEST, sq) |
	       gen_ray_attacks(occ, NORTHWEST, sq);
}

static u64 slow_gen_rook_attacks(Square sq, u64 occ)
{
	return gen_ray_attacks(occ, NORTH, sq) |
	       gen_ray_attacks(occ, EAST,  sq) |
	       gen_ray_attacks(occ, SOUTH, sq) |
	       gen_ray_attacks(occ, WEST,  sq);
}

/*
 * This function initializes the magic numbers and the attack tables using
 * brute-force by generating random numbers and checking whether it's a valid
 * magic or not, that is, if all the occupancies mapping to the same attack set
 * are equivalent occupancies.
 * 
 * It works by using an occupancy and reference table, where the reference
 * table contains the attack sets for each occupancy in the occupancy table.
 * The Carry-Rippler method is used to generate the relevant occupancies for
 * each square and piece, since each of them is a permutation of the attack set
 * on an empty board. For example, if a rook is at A1, the relevant occupancies
 * are the permutations of zeroes and ones in the positions of the bits set in
 * the following bitboard:
 * 
 * 10000000
 * 10000000
 * 10000000
 * 10000000
 * 10000000
 * 10000000
 * 10000000
 * 01111111
 * 
 * Many magic candidates are tested for each square so we keep track of the
 * current attempt number for a square. With an attempt table indexed by the
 * same index as the attack table and storing the number of the attempt that
 * last modified the attack table at that index it's possible to know if that
 * index has been used in the current attempt, if it has then the next time
 * that same index is calculated from another occupancy the attack sets must be
 * the same. If, however, the index has not been used for the current magic
 * candidate, the attack set from the reference table is stored there.
 * 
 * As another optimization technique, we use sparser random numbers with only
 * 1/8 of their bits set on average, since this is usually the case for magic
 * numbers.
 */
static void init_magics_for_piece_type(PieceType piece_type)
{
	static u64 occ[4096], ref[4096];
	static unsigned attempts[4096];

	SlidingAttackGenerator *attack_generator;
	u64 *attack_table;
	Magic *magics;

	if (piece_type == PIECE_TYPE_BISHOP) {
		attack_generator = slow_gen_bishop_attacks;
		attack_table = bishop_attack_table;
		magics = bishop_magics;
	} else if (piece_type == PIECE_TYPE_ROOK) {
		attack_generator = slow_gen_rook_attacks;
		attack_table = rook_attack_table;
		magics = rook_magics;
	}

	size_t size;
	for (Square sq = A1; sq <= H8; ++sq) {
		const File f = pos_get_file(sq);
		const Rank r = pos_get_rank(sq);

		const u64 edges  = ((file_bitboards[FILE_A] |
		                     file_bitboards[FILE_H]) &
		                    ~file_bitboards[f]) |
		                   ((rank_bitboards[RANK_1] |
		                     rank_bitboards[RANK_8]) &
		                    ~rank_bitboards[r]);

		Magic *const m = &magics[sq];
		m->mask = attack_generator(sq, 0) & ~edges;
		m->shift = 64 - popcnt(m->mask);
		m->ptr = sq == A1 ? attack_table : magics[sq - 1].ptr + size;

		size = 0;
		u64 bb = 0;
		do {
			occ[size] = bb;
			ref[size] = attack_generator(sq, bb);

			if (HAS_BMI2)
				m->ptr[pext(bb, m->mask)] = ref[size];

			bb = (bb - m->mask) & m->mask;
			++size;
		} while (bb);

		if (HAS_BMI2)
			continue;

		memset(attempts, 0, sizeof(attempts));
		for (size_t i = 0, current_attempt = 0; i < size;) {
			m->num = 0;
			while (popcnt((m->num * m->mask) >> 56) < 6)
				m->num = rng_next_sparse();
			++current_attempt;
			for (i = 0; i < size; ++i) {
				int idx = (occ[i] * m->num) >> m->shift;
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

static void init_magics(void)
{
	init_magics_for_piece_type(PIECE_TYPE_BISHOP);
	init_magics_for_piece_type(PIECE_TYPE_ROOK);
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
		knight_attack_table[sq] = (h1 << 16) | (h1 >> 16)
		                        | (h2 <<  8) | (h2 >>  8);
	}
}

static void init_king_attacks(void)
{
	for (int i = 0; i < 64; ++i) {
		u64 bb = U64(0x1) << i;
		king_attack_table[i] = move_east(bb, 1)
		                     | move_west(bb, 1);
		bb |= king_attack_table[i];
		king_attack_table[i] |= move_north(bb, 1)
		                     | move_south(bb, 1);
	}
}

static u64 get_single_push(Square sq, u64 occ, Color c)
{
	const u64 bb = U64(0x1) << sq;
	if (c == COLOR_WHITE)
		return move_north(bb, 1) & ~occ;
	else
		return move_south(bb, 1) & ~occ;
}

static u64 get_double_push(Square sq, u64 occ, Color c)
{
	if (c == COLOR_WHITE) {
		const u64 single_push = get_single_push(sq, occ, c);
		return move_north(single_push, 1) &
		       ~occ &
		       rank_bitboards[RANK_4];
	} else {
		const u64 single_push = get_single_push(sq, occ, c);
		return move_south(single_push, 1)
		       & ~occ
		       & rank_bitboards[RANK_5];
	}
}

static u64 get_pawn_attacks(Square sq, Color c)
{
	const u64 bb = U64(0x1) << sq;
	if (c == COLOR_WHITE)
		return move_northeast(bb, 1) | move_northwest(bb, 1);
	else
		return move_southeast(bb, 1) | move_southwest(bb ,1);
}

static u64 get_knight_attacks(Square sq)
{
	return knight_attack_table[sq];
}

static u64 get_king_attacks(Square sq)
{
	return king_attack_table[sq];
}

static u64 get_rook_attacks(Square sq, u64 occ)
{
	const u64 *const aptr = rook_magics[sq].ptr;
	if (HAS_BMI2)
		return aptr[pext(occ, rook_magics[sq].mask)];
	occ &= rook_magics[sq].mask;
	occ *= rook_magics[sq].num;
	occ >>= rook_magics[sq].shift;
	return aptr[occ];
}

static u64 get_bishop_attacks(Square sq, u64 occ)
{
	const u64 *const aptr = bishop_magics[sq].ptr;
	if (HAS_BMI2)
		return aptr[pext(occ, bishop_magics[sq].mask)];
	occ &= bishop_magics[sq].mask;
	occ *= bishop_magics[sq].num;
	occ >>= bishop_magics[sq].shift;
	return aptr[occ];
}

static u64 get_queen_attacks(Square sq, u64 occ)
{
	return get_rook_attacks(sq, occ) | get_bishop_attacks(sq, occ);
}

static void add_move(MoveList *list, Move move)
{
	if (list->len == list->capacity) {
		list->capacity <<= 2;
		Move *const tmp = realloc(list->ptr,
		                          list->capacity * sizeof(Move));
		if (!tmp) {
			fprintf(stderr, "Out of memory.");
			exit(1);
		}
		list->ptr = tmp;
	}
	list->ptr[list->len] = move;
	++list->len;
}

static void add_pushes(MoveList *list, Square from, u64 occ, Color color)
{
	u64 targets = get_single_push(from, occ, color);
	if (targets) {
		const Square to = get_ls1b(targets);
		if ((color == COLOR_WHITE && pos_get_rank(to) == RANK_8) ||
		(color == COLOR_BLACK && pos_get_rank(to) == RANK_1)) {
			for (MoveType move_type = MOVE_KNIGHT_PROMOTION;
			move_type <= MOVE_QUEEN_PROMOTION; ++move_type) {
				const Move move = move_new(from, to, move_type);
				add_move(list, move);
			}
		} else {
			const Move move = move_new(from, to, MOVE_QUIET);
			add_move(list, move);
		}
	}
}

static void add_double_pushes(MoveList *list, Square from, u64 occ, Color color)
{
	u64 targets = get_double_push(from, occ, color);
	if (targets) {
		const Square to = get_ls1b(targets);
		const Move move = move_new(from, to, MOVE_DOUBLE_PAWN_PUSH);
		add_move(list, move);
	}
}

static void add_pawn_attacks(MoveList *list, Square from, u64 enemy_pieces,
                             Color color)
{
	u64 targets = get_pawn_attacks(from, color) & enemy_pieces;
	while (targets) {
		const Square to = unset_ls1b(&targets);
		if ((color == COLOR_WHITE && pos_get_rank(to) == RANK_8) ||
		    (color == COLOR_BLACK && pos_get_rank(to) == RANK_1)) {
			for (MoveType move_type = MOVE_KNIGHT_PROMOTION_CAPTURE;
			move_type <= MOVE_QUEEN_PROMOTION_CAPTURE;
			++move_type) {
				const Move move = move_new(from, to, move_type);
				add_move(list, move);
			}
		} else {
			const Move move = move_new(from, to, MOVE_CAPTURE);
			add_move(list, move);
		}
	}
}

static void add_pawn_moves(MoveList *restrict list,
                           const Position *restrict pos)
{
	const Color color = pos_get_side_to_move(pos);
	const Piece piece = pos_make_piece(PIECE_TYPE_PAWN,
	color);
	const u64 enemy_pieces = pos_get_color_bitboard(pos, !color);
	const u64 occ = enemy_pieces | pos_get_color_bitboard(pos, color);

	u64 bb = pos_get_piece_bitboard(pos, piece);
	if (pos_enpassant_possible(pos)) {
		const Square sq = pos_get_enpassant(pos);
		u64 attackers = get_pawn_attacks(sq, !color) & bb;
		while (attackers) {
			const Square from = unset_ls1b(&attackers);
			const Square to = sq;
			const Move move = move_new(from, to, MOVE_EP_CAPTURE);
			add_move(list, move);
		}
	}

	while (bb) {
		const Square from = unset_ls1b(&bb);
		add_pushes(list, from, occ, color);
		add_double_pushes(list, from, occ, color);
		add_pawn_attacks(list, from, enemy_pieces, color);
	}
}

static void add_king_castling(MoveList *restrict list,
                              const Position *restrict pos)
{
	const Color color = pos_get_side_to_move(pos);
	const Square from = pos_get_king_square(pos, color);

	if (pos_has_castling_right(pos, color, CASTLING_SIDE_KING)) {
		const Square k_castling_test_sq1 = color == COLOR_WHITE ? F1 :
		                                                          F8;
		const Square k_castling_test_sq2 = color == COLOR_WHITE ? G1 :
		                                                          G8;
		if (pos_get_piece_at(pos, k_castling_test_sq1) == PIECE_NONE &&
		    pos_get_piece_at(pos, k_castling_test_sq2) == PIECE_NONE &&
		    !movegen_is_square_attacked(k_castling_test_sq1, !color,
		                                pos) &&
		    !movegen_is_square_attacked(k_castling_test_sq2, !color,
		                                pos) &&
		    !movegen_is_square_attacked(from, !color, pos)) {
			const Square to   = color == COLOR_WHITE ? G1 : G8;
			const Move move = move_new(from, to, MOVE_KING_CASTLE);
			add_move(list, move);
		}
	}
}

static void add_queen_castling(MoveList *restrict list,
                              const Position *restrict pos)
{
	const Color color = pos_get_side_to_move(pos);
	const Square from = pos_get_king_square(pos, color);

	if (pos_has_castling_right(pos, color, CASTLING_SIDE_QUEEN)) {
		const Square q_castling_test_sq1 = color == COLOR_WHITE ? D1 :
		                                                          D8;
		const Square q_castling_test_sq2 = color == COLOR_WHITE ? C1 :
		                                                          C8;
		const Square q_castling_test_sq3 = color == COLOR_WHITE ? B1 :
		                                                          B8;
		if (pos_get_piece_at(pos, q_castling_test_sq1) == PIECE_NONE &&
		    pos_get_piece_at(pos, q_castling_test_sq2) == PIECE_NONE &&
		    pos_get_piece_at(pos, q_castling_test_sq3) == PIECE_NONE &&
		    !movegen_is_square_attacked(q_castling_test_sq1, !color,
		                                pos) &&
		    !movegen_is_square_attacked(q_castling_test_sq2, !color,
		                                pos) &&
		    !movegen_is_square_attacked(from, !color, pos)) {
			const Square to   = color == COLOR_WHITE ? C1 : C8;
			const Move move = move_new(from, to, MOVE_QUEEN_CASTLE);
			add_move(list, move);
		}
	}
}

static void add_king_moves(MoveList *restrict list,
                           const Position *restrict pos)
{
	const Color color = pos_get_side_to_move(pos);
	const Square from = pos_get_king_square(pos, color);
	const u64 occ = pos_get_color_bitboard(pos, color) |
	pos_get_color_bitboard(pos, !color);
	const u64 friendly_pieces = occ & pos_get_color_bitboard(pos, color);

	add_king_castling(list, pos);
	add_queen_castling(list, pos);

	u64 targets = get_king_attacks(from) & ~friendly_pieces;
	while (targets) {
		const Square to = unset_ls1b(&targets);
		const Move move = pos_get_piece_at(pos, to) == PIECE_NONE ?
		                  move_new(from, to, MOVE_QUIET) :
		                  move_new(from, to, MOVE_CAPTURE);
		add_move(list, move);
	}
}

static void add_moves(MoveList *restrict list, PieceType piece_type,
                      const Position *restrict pos)
{
	switch (piece_type) {
	case PIECE_TYPE_PAWN:
		add_pawn_moves(list, pos);
		return;
	case PIECE_TYPE_KING:
		add_king_moves(list, pos);
		return;
	default:
		break;
	}

	const Color color = pos_get_side_to_move(pos);
	const Piece piece = pos_make_piece(piece_type, color);
	const u64 occ = pos_get_color_bitboard(pos, color) |
	                pos_get_color_bitboard(pos, !color);
	const u64 friendly_pieces = occ & pos_get_color_bitboard(pos, color);

	u64 bb = pos_get_piece_bitboard(pos, piece);
	while (bb) {
		const Square from = unset_ls1b(&bb);
		u64 targets = 0;
		switch (piece_type) {
		case PIECE_TYPE_KNIGHT:
			targets = get_knight_attacks(from);
			break;
		case PIECE_TYPE_ROOK:
			targets = get_rook_attacks(from, occ);
			break;
		case PIECE_TYPE_BISHOP:
			targets = get_bishop_attacks(from, occ);
			break;
		case PIECE_TYPE_QUEEN:
			targets = get_queen_attacks(from, occ);
			break;
		default:
			abort();
		}
		targets &= ~friendly_pieces;
		while (targets) {
			const Square to = unset_ls1b(&targets);
			const Move move = pos_get_piece_at(pos, to) == PIECE_NONE ?
			                  move_new(from, to, MOVE_QUIET) :
			                  move_new(from, to, MOVE_CAPTURE);
			add_move(list, move);
		}
	}
}

int movegen_get_number_of_pseudo_legal_moves(PieceType piece_type, Color c,
                                             const Position *pos)
{
	const Piece piece = pos_make_piece(piece_type, c);
	const u64 occ = pos_get_color_bitboard(pos, c)
	              | pos_get_color_bitboard(pos, !c);
	const u64 friendly_pieces = occ & pos_get_color_bitboard(pos, c);

	int cnt = 0;
	u64 bb = pos_get_piece_bitboard(pos, piece);
	while (bb) {
		const Square sq = unset_ls1b(&bb);
		u64 targets = 0;
		switch (pos_get_piece_type(piece)) {
		case PIECE_TYPE_PAWN:
			targets = get_single_push(sq, occ, c) |
			          get_double_push(sq, occ, c) |
			          (get_pawn_attacks(sq, c) & ~friendly_pieces);
			break;
		case PIECE_TYPE_KNIGHT:
			targets = get_knight_attacks(sq) &
			~friendly_pieces;
			break;
		case PIECE_TYPE_ROOK:
			targets = get_rook_attacks(sq, occ) &
			~friendly_pieces;
			break;
		case PIECE_TYPE_BISHOP:
			targets = get_bishop_attacks(sq, occ) &
			~friendly_pieces;
			break;
		case PIECE_TYPE_QUEEN:
			targets = get_queen_attacks(sq, occ) &
			~friendly_pieces;
			break;
		case PIECE_TYPE_KING:
			targets = get_king_attacks(sq) &
			~friendly_pieces;
			break;
		}
		cnt += popcnt(targets);
	}

	return cnt;
}

void movegen_init(void)
{
	rng_seed(374583);
	init_rays();
	init_magics();
	init_knight_attacks();
	init_king_attacks();
}

/*
 * This function returns true if the square sq is being attacked by any of the
 * opponent's pieces. It works by generating attacks from the attacked square
 * and checking if one of the squares in this attack set has a piece, and since
 * all chess moves are reversible (not talking about the overall state of the
 * game like en passant, castling, promotions, etc. Just the piece movement
 * from one square to the other) a piece at one of these squares can attack sq.
 * Pawn moves are not reversible because pawns can't return, but the opponent's
 * pawn moves are the inverse of the pawn moves, so we can just use the opposite
 * side's attacks for pawns.
 */
bool movegen_is_square_attacked(Square sq, Color by_side, const Position *pos)
{
	const u64 occ = pos_get_color_bitboard(pos, by_side)
	              | pos_get_color_bitboard(pos, !by_side);

	Piece piece = pos_make_piece(PIECE_TYPE_PAWN, by_side);
	const u64 pawns = pos_get_piece_bitboard(pos, piece);
	if (get_pawn_attacks(sq, !by_side) & pawns)
		return true;
	piece = pos_make_piece(PIECE_TYPE_KNIGHT, by_side);
	const u64 knights = pos_get_piece_bitboard(pos, piece);
	if (get_knight_attacks(sq) & knights)
		return true;
	piece = pos_make_piece(PIECE_TYPE_ROOK, by_side);
	u64 rooks_queens = pos_get_piece_bitboard(pos, piece);
	piece = pos_make_piece(PIECE_TYPE_QUEEN, by_side);
	rooks_queens |= pos_get_piece_bitboard(pos, piece);
	if (get_rook_attacks(sq, occ) & rooks_queens)
		return true;
	u64 bishops_queens = pos_get_piece_bitboard(pos, piece);
	piece = pos_make_piece(PIECE_TYPE_BISHOP, by_side);
	bishops_queens |= pos_get_piece_bitboard(pos, piece);
	if (get_bishop_attacks(sq, occ) & bishops_queens)
		return true;
	piece = pos_make_piece(PIECE_TYPE_KING, by_side);
	const u64 king = pos_get_piece_bitboard(pos, piece);
	if (get_king_attacks(sq) & king)
		return true;
	return false;
}

/*
 * Return a list containing all the pseudo legal moves possible for a position
 * (that is, moves that may put the king in check), or NULL if no moves are
 * possible.
 */
Move *movegen_get_pseudo_legal_moves(const Position *restrict pos,
                                     size_t *restrict len)
{
	const size_t initial_capacity = 2 << 8;
	MoveList list;

	list.capacity = initial_capacity;
	list.ptr = malloc(list.capacity * sizeof(Move));
	if (!list.ptr) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	list.len = 0;

	add_moves(&list, PIECE_TYPE_PAWN, pos);
	add_moves(&list, PIECE_TYPE_KNIGHT, pos);
	add_moves(&list, PIECE_TYPE_ROOK, pos);
	add_moves(&list, PIECE_TYPE_BISHOP, pos);
	add_moves(&list, PIECE_TYPE_QUEEN, pos);
	add_moves(&list, PIECE_TYPE_KING, pos);

	*len = list.len;
	if (!*len) {
		free(list.ptr);
		return NULL;
	}
	return list.ptr;
}

/*
 * Return the number of possible moves on an empty board containing only the
 * moving piece. The color of the piece is only used for pawns so for any other
 * piece the result will be the same for either white or black.
 */
int movegen_get_number_of_moves_empty_board(Piece piece, Square sq) {
	const PieceType pt = pos_get_piece_type(piece);
	const Color c = pos_get_piece_color(piece);
	u64 targets = 0;
	switch (pt) {
	case PIECE_TYPE_PAWN:
		targets = get_single_push(sq, 0, c) | get_double_push(sq, 0, c);
		break;
	case PIECE_TYPE_KNIGHT:
		targets = get_knight_attacks(sq);
		break;
	case PIECE_TYPE_ROOK:
		targets = get_rook_attacks(sq, 0);
		break;
	case PIECE_TYPE_BISHOP:
		targets = get_bishop_attacks(sq, 0);
		break;
	case PIECE_TYPE_QUEEN:
		targets = get_queen_attacks(sq, 0);
		break;
	case PIECE_TYPE_KING:
		targets = get_king_attacks(sq);
		break;
	default:
		abort();
	}
	return popcnt(targets);
}

/*
 * Returns a bitboard of the pieces attacking a square. Note that this only
 * counts pieces that are attacking a square directly, so a rook behind another
 * rook will not be included.
 */
u64 movegen_get_attackers(Square sq, const Position *pos)
{
	const u64 occ = pos_get_color_bitboard(pos, COLOR_WHITE)
	              | pos_get_color_bitboard(pos, COLOR_BLACK);
	u64 (*const get_bb)(const Position *pos,
	                    Piece piece) = pos_get_piece_bitboard;

	const u64 white_pawns = get_bb(pos, PIECE_BLACK_PAWN);
	const u64 black_pawns = get_bb(pos, PIECE_WHITE_PAWN);
	const u64 knights = get_bb(pos, PIECE_WHITE_KNIGHT)
	                  | get_bb(pos, PIECE_BLACK_KNIGHT);
	const u64 kings   = get_bb(pos, PIECE_WHITE_KING)
	                  | get_bb(pos, PIECE_BLACK_KING);
	const u64 bishops_queens = get_bb(pos, PIECE_WHITE_QUEEN)
	                         | get_bb(pos, PIECE_BLACK_QUEEN)
	                         | get_bb(pos, PIECE_WHITE_BISHOP)
	                         | get_bb(pos, PIECE_BLACK_BISHOP);
	const u64 rooks_queens   = get_bb(pos, PIECE_WHITE_QUEEN)
	                         | get_bb(pos, PIECE_BLACK_QUEEN)
	                         | get_bb(pos, PIECE_WHITE_ROOK)
	                         | get_bb(pos, PIECE_BLACK_ROOK);

	return (get_pawn_attacks(sq, COLOR_WHITE) & white_pawns)
	     | (get_pawn_attacks(sq, COLOR_BLACK) & black_pawns)
	     | (get_knight_attacks(sq)            & knights)
	     | (get_king_attacks(sq)              & kings)
	     | (get_bishop_attacks(sq, occ)       & bishops_queens)
	     | (get_rook_attacks(sq, occ)         & rooks_queens);
}

/*
 * Returns a bitboard containing the squares attacked by a piece at square sq.
 */
u64 movegen_get_attacked_squares(Square sq, const Position *pos)
{
	const Piece piece = pos_get_piece_at(pos, sq);
	const Color color = pos_get_piece_color(piece);
	const PieceType pt = pos_get_piece_type(piece);
	const u64 occ = pos_get_color_bitboard(pos, COLOR_WHITE)
	              | pos_get_color_bitboard(pos, COLOR_BLACK);

	switch (pt) {
	case PIECE_TYPE_PAWN:
		return get_pawn_attacks(sq, color);
	case PIECE_TYPE_KNIGHT:
		return get_knight_attacks(sq);
	case PIECE_TYPE_BISHOP:
		return get_bishop_attacks(sq, occ);
	case PIECE_TYPE_ROOK:
		return get_rook_attacks(sq, occ);
	case PIECE_TYPE_QUEEN:
		return get_queen_attacks(sq, occ);
	case PIECE_TYPE_KING:
		return get_king_attacks(sq);
	default:
		abort();
	}
}

u64 movegen_perft(Position *pos, int depth)
{
	u64 nodes = 0;

	if (!depth)
		return 1;
	size_t len;
	Move *moves = movegen_get_pseudo_legal_moves(pos, &len);
	for (size_t i = 0; i < len; ++i) {
		Move move = moves[i];
		if (!move_is_legal(pos, move))
			continue;
		move_do(pos, move);
		nodes += movegen_perft(pos, depth - 1);
		move_undo(pos, move);
	}
	free(moves);
	return nodes;
}

#ifdef TEST
#include "test_movegen.c"
#endif
