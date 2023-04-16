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

#ifndef EVALUATION_H
#define EVALUATION_H

typedef enum phase {
	PHASE_OPENING,
	PHASE_MIDDLEGAME,
	PHASE_ENDGAME,
} Phase;

int eval_evaluate(const Position *pos);
int eval_get_average_mvv_lva_score(void);
int eval_opening_evaluate(const Position *pos);
int eval_evaluate_move(Move move, Position *pos);
int eval_evaluate_qmove(Move move, Position *pos);
void eval_init(void);

#endif
