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

#include "check.h"

START_TEST(test_attackers)
{
	puts("Test: attackers");

	const char *fen = "r3r1k1/pp3pbp/1qp1b1p1/2B5/2BP4/Q1n2N2/P4PPP/3R1K1R "
	                  "w - - 4 18";
	const int num_attackers[] = {
		[A8] = 1, [B8] = 2, [C8] = 3, [D8] = 3, [E8] = 1, [F8] = 4, [G8] = 1, [H8] = 2,
		[A7] = 3, [B7] = 1, [C7] = 1, [D7] = 1, [E7] = 2, [F7] = 2, [G7] = 1, [H7] = 1,
		[A6] = 4, [B6] = 2, [C6] = 2, [D6] = 1, [E6] = 3, [F6] = 1, [G6] = 2, [H6] = 1,
		[A5] = 2, [B5] = 4, [C5] = 3, [D5] = 4, [E5] = 3, [F5] = 2, [G5] = 1, [H5] = 1,
		[A4] = 2, [B4] = 3, [C4] = 1, [D4] = 4, [E4] = 1, [F4] = 0, [G4] = 1, [H4] = 1,
		[A3] = 1, [B3] = 4, [C3] = 1, [D3] = 2, [E3] = 1, [F3] = 1, [G3] = 2, [H3] = 2,
		[A2] = 3, [B2] = 2, [C2] = 0, [D2] = 2, [E2] = 3, [F2] = 1, [G2] = 1, [H2] = 2,
		[A1] = 1, [B1] = 3, [C1] = 2, [D1] = 1, [E1] = 3, [F1] = 3, [G1] = 3, [H1] = 0,
	};

	Position *pos = pos_create(fen);
	for (Square sq = A1; sq <= H8; ++sq) {
		u64 bb = movegen_get_attackers(sq, pos);
		int num = popcnt(bb);
		ck_assert_int_eq(num, num_attackers[sq]);
	}
	pos_destroy(pos);
}
END_TEST

/*
 * This function generates all possible attacks for every piece at every square
 * and tests the movegen_is_square_attacked function by calling it with the
 * generated attack set.
 * We test the sliding pieces with only the piece itself on the board,
 * otherwise the test would become too complex.
 */
START_TEST(test_square_attacked)
{
	u64 (*sliding[])(Square, u64) = {
		[PIECE_TYPE_BISHOP] = get_bishop_attacks,
		[PIECE_TYPE_ROOK  ] = get_rook_attacks,
		[PIECE_TYPE_QUEEN ] = get_queen_attacks,
	};

	for (PieceType pt = PIECE_TYPE_BISHOP; pt <= PIECE_TYPE_QUEEN; ++pt) {
		Piece piece = pos_make_piece(pt, COLOR_WHITE);
		for (Square sq = A1; sq <= H8; ++sq) {
			Position *pos = pos_create("8/8/8/8/8/8/8/8 w - - 0 1");
			pos_place_piece(pos, sq, piece);
			u64 bb = sliding[pt](sq, 0);
			while (bb) {
				Square target = unset_ls1b(&bb);
				ck_assert(movegen_is_square_attacked(target, COLOR_WHITE, pos));
			}
			pos_destroy(pos);
		}
	}

	for (Color c = COLOR_WHITE; c <= COLOR_BLACK; ++c) {
		Piece piece = pos_make_piece(PIECE_TYPE_PAWN, c);
		for (Square sq = A1; sq <= H8; ++sq) {
			Position *pos = pos_create("8/8/8/8/8/8/8/8 w - - 0 1");
			pos_place_piece(pos, sq, piece);
			u64 bb = get_pawn_attacks(sq, c);
			while (bb) {
				Square target = unset_ls1b(&bb);
				ck_assert(movegen_is_square_attacked(target, c, pos));
			}
			pos_destroy(pos);
		}
	}

	PieceType others[] = {PIECE_TYPE_KNIGHT, PIECE_TYPE_KING};
	u64 (*others_func[])(Square sq) = {get_knight_attacks, get_king_attacks};
	for (size_t i = 0; i < sizeof(others)/sizeof(others[0]); ++i) {
		PieceType pt = others[i];
		Piece piece = pos_make_piece(pt, COLOR_WHITE);
		for (Square sq = A1; sq <= H8; ++sq) {
			Position *pos = pos_create("8/8/8/8/8/8/8/8 w - - 0 1");
			pos_place_piece(pos, sq, piece);
			u64 bb = others_func[i](sq);
			while (bb) {
				Square target = unset_ls1b(&bb);
				ck_assert(movegen_is_square_attacked(target, COLOR_WHITE, pos));
			}
			pos_destroy(pos);
		}
	}
}
END_TEST

START_TEST(test_num_nodes)
{
	const u64 perft_results[] = {
		U64(1), U64(20), U64(400), U64(8902), U64(197281), U64(4865609),
		U64(119060324), U64(3195901860), U64(84998978956),
		U64(2439530234167), U64(69352859712417), U64(2097651003696806),
		U64(62854969236701747)
	};
	const char *fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR"
	                  " w KQkq - 0 1";

	const int depth = 6;
	Position *pos = pos_create(fen);
	const u64 nodes = movegen_perft(pos, depth);
	pos_destroy(pos);

	ck_assert_int_eq(nodes, perft_results[depth]);
}
END_TEST

int test_movegen(void)
{
	movegen_init();

	Suite *const suite = suite_create("movegen");
	SRunner *runner = srunner_create(suite);

	TCase *const tc_core = tcase_create("core");

	tcase_add_test(tc_core, test_num_nodes);
	tcase_add_test(tc_core, test_square_attacked);
	tcase_add_test(tc_core, test_attackers);

	suite_add_tcase(suite, tc_core);
	srunner_run_all(runner, CK_NORMAL);
	int failed = srunner_ntests_failed(runner);
	srunner_free(runner);
	return failed;
}
