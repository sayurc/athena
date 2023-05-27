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

#ifndef TT_H
#define TT_H

/*
 * PV-nodes are positions that have a score in the interval [alpha, beta). All
 * the child nodes have been searched because there was not prunning and the
 * value returned is exact, that is, the best possible score since there was no
 * branch cut-off during the search.
 *
 * A cut-node had a beta-cutoff performed during its search, so a minimum of one
 * move for this position has been searched, since that's needed for the
 * prunning. Because not all the child nodes are searched the score returned is
 * a lower bound, so a player could possibly get a better score from the moves
 * that weren't used in the search but the opponent wouldn't allow (hence the
 * branch cut-off).
 *
 * If no moves exceeded alpha, this node is called an all-node. In
 * this case alpha is returned as the score so the score is an upper bound.
 */
typedef enum node_type {
	NODE_TYPE_EXACT,
	NODE_TYPE_CUT,
	NODE_TYPE_ALPHA_UNCHANGED,
} NodeType;

typedef struct node_data {
	int score;
	u8 depth;
	u8 type;
	u64 hash;
	Move best_move;
} NodeData;

u64 tt_hash(const Position *pos);
bool tt_get(NodeData *data, const Position *pos);
void tt_store(const NodeData *data);
void tt_entry_init(NodeData *pos_data, int score, int depth, NodeType type,
                   Move best_move, const Position *pos);
void tt_prefetch(void);
void tt_clear(void);
void tt_resize(int size);
void tt_init(int size);
void tt_finish(void);

#endif
