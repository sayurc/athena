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
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>

#ifdef __x86_64__
#include <x86intrin.h>
#endif

#include "bit.h"
#include "rng.h"
#include "pos.h"
#include "move.h"
#include "tt.h"

/*
 * The set of random numbers in the Zobrist array map to each possible variation
 * in the state of the position. 12 * 64 random numbers for each piece on each
 * square, 16 permutations of castling rights, 8 possible en passant files and
 * finally 1 possible variation of color when it is black instead of white.
 */
#define NUM_PIECES 12
#define NUM_SQUARES 64
#define NUM_CASTLING_RIGHTS 16
#define NUM_EN_PASSANT_FILES 8
#define NUM_COLOR_VARIATION 1
#define ZOBRIST_ARRAY_SIZE (NUM_PIECES * NUM_SQUARES + NUM_CASTLING_RIGHTS +\
                            NUM_EN_PASSANT_FILES + NUM_COLOR_VARIATION)

struct transposition_table {
	NodeData *ptr;
	size_t capacity;
} transposition_table = {.ptr = NULL, .capacity = 0};

static u64 zobrist_numbers[ZOBRIST_ARRAY_SIZE];

static bool is_prime(unsigned long long n)
{
	for (unsigned long long m = n - 1; m * m >= n; --m) {
		if (n % m == 0)
			return false;
	}
	return true;
}

/*
 * Finds the greatest prime number less than or equal to n. Notice that n must
 * be at least 2.
 */
static unsigned long long find_prime(unsigned long long n)
{
	for (unsigned long long p = n; p > 0; --p) {
		if (is_prime(p))
			return p;
	}

	fprintf(stderr, "Internal error.\n");
	exit(1);
}

/*
 * Generate a set of unique random numbers for Zobrist hashing.
 */
static void init_hash(void)
{
	for (size_t i = 0; i < ZOBRIST_ARRAY_SIZE; ++i) {
		zobrist_numbers[i] = rng_next();
		for (size_t j = 0; j < i; ++j) {
			if (zobrist_numbers[i] == zobrist_numbers[j])
				--i;
		}
	}
}

u64 tt_hash(const Position *pos)
{
	u64 key = 0;
	u64 *ptr = zobrist_numbers;

	for (Square sq = 0; sq < NUM_SQUARES; ++sq) {
		const Piece piece = pos_get_piece_at(pos, sq);
		if (piece != PIECE_NONE)
			key ^= ptr[NUM_SQUARES * piece + sq];
	}
	ptr += NUM_PIECES * NUM_SQUARES;

	u8 rights = 0;
	for (Color color = COLOR_WHITE; color <= COLOR_BLACK; ++color) {
		const bool has_king_right  = pos_has_castling_right(pos, color,
		                             CASTLING_SIDE_KING);
		const bool has_queen_right = pos_has_castling_right(pos, color,
		                             CASTLING_SIDE_QUEEN);
		rights = (has_king_right << 1) | has_queen_right;
		rights <<= (2 * color);
	}
	key ^= ptr[rights];
	ptr += NUM_CASTLING_RIGHTS;

	if (pos_enpassant_possible(pos)) {
		const Square sq = pos_get_enpassant(pos);
		const File file = pos_get_file(sq);
		key ^= ptr[file];
	}
	ptr += NUM_EN_PASSANT_FILES;

	if (pos_get_side_to_move(pos) == COLOR_BLACK)
		key ^= ptr[0];

	return key;
}

/*
 * It will return true if the node data is in the transposition table table and
 * false otherwise.
 */
bool tt_get(NodeData *restrict data, const Position *restrict pos)
{
	const u64 node_hash = tt_hash(pos);
	const size_t key = node_hash % transposition_table.capacity;
	struct node_data tt_data = transposition_table.ptr[key];
	if (node_hash == tt_data.hash) {
		*data = tt_data;
		return true;
	}
	return false;
}

void tt_store(const NodeData *data)
{
	const size_t key = data->hash % transposition_table.capacity;
	transposition_table.ptr[key] = *data;
}

void tt_entry_init(NodeData *data, int score, int depth, NodeType type,
                   Move best_move, const Position *pos)
{
	data->score = score;
	data->depth = depth;
	data->type = type;
	data->best_move = best_move;
	data->hash = tt_hash(pos);
}

void tt_prefetch(void)
{
#ifdef __x86_64__
	_mm_prefetch(transposition_table.ptr, _MM_HINT_T0);
#else
	return;
#endif
}

static size_t compute_capacity(int size)
{
	const long mib_in_byte = 1048576;

	long tmp = size;
	if (tmp > LONG_MAX / mib_in_byte)
		tmp = LONG_MAX / sizeof(NodeData);
	else
		tmp = (tmp * mib_in_byte) / sizeof(NodeData);
	tmp %= SIZE_MAX;
	return find_prime(tmp);
}

/*
 * This function does nothing if the transposition table has not been
 * initialized.
 */
void tt_clear(void)
{
	NodeData *const ptr = transposition_table.ptr;
	if (!ptr)
		return;
	const size_t capacity = transposition_table.capacity;
	memset(ptr, 0, capacity * sizeof(NodeData));
}

/*
 * This function does nothing if the transposition table has not been
 * initialized.
 */
void tt_resize(int size)
{
	if (!transposition_table.ptr)
		return;
	const size_t old_capacity = transposition_table.capacity;
	const size_t new_capacity = compute_capacity(size);
	transposition_table.capacity = new_capacity;
	transposition_table.ptr = realloc(transposition_table.ptr,
	                                  new_capacity * sizeof(NodeData));
	if (!transposition_table.ptr) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}

	if (new_capacity > old_capacity) {
		memset(transposition_table.ptr + old_capacity, 0,
		       (new_capacity - old_capacity) * sizeof(NodeData));
	}
}

/*
 * The capacity of the transposition table is calculated with the size given in
 * mebibytes. For performance reasons the capacity is a prime number less than
 * the originally calculated capacity. If the size is too big then a default
 * capacity is used instead.
 */
void tt_init(int size)
{
	init_hash();

	transposition_table.capacity = compute_capacity(size);
	transposition_table.ptr = calloc(transposition_table.capacity,
	                                 sizeof(NodeData));
	if (!transposition_table.ptr) {
		fprintf(stderr, "Out of memory.");
		exit(1);
	}
}

void tt_finish(void)
{
	transposition_table.capacity = 0;
	free(transposition_table.ptr);
	transposition_table.ptr = NULL;
}
