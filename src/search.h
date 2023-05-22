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

#ifndef SEARCH_H
#define SEARCH_H

/*
 * The flags INFO_FLAG_MATE and INFO_FLAG_CP are mutually exclusive and the flag
 * INFO_FLAG_LBOUND should be set only if one of them is set.
 */
enum info_flag {
	INFO_FLAG_DEPTH  = 0x1,
	INFO_FLAG_NODES  = 0x1 << 1,
	INFO_FLAG_NPS    = 0x1 << 2,
	INFO_FLAG_MATE   = 0x1 << 3,
	INFO_FLAG_TIME   = 0x1 << 4,
	INFO_FLAG_CP     = 0x1 << 5,
	INFO_FLAG_LBOUND = 0x1 << 6,
};

struct info {
	enum info_flag flags;
	int depth;
	int cp;
	int mate;
	long long nodes;
	long long nps;
	long long time;
};

struct thread_data;

/*
 * "moves" points to a list of moves made throughout the game, before the
 * current search.
 *
 * If "infinite" is true the search won't stop until the calling
 * thread sets "stop" to true and "depth", "mate", "movestogo", "perft",
 * "nodes", "time", "inc" and "movetime" are ignored.
 *
 * "depth", "mate", "movestogo", "nodes", "time", "inc" and "movetime" work just
 * as described in the UCI protocol. The search will stop as soon as any of
 * these limits are reached. If any of these are used then "infinite" must be
 * set to false. The unused limits must be set to 0 unless "infinite" is true,
 * in which case they are just ignored.
 * 
 * "running" must be set to true when the search function is called.
 * 
 * While the search is running the calling thread must not modify any element of
 * this struct except the data that "running" and "running_mtx" point to. The
 * search function doesn't free the memory of any element, this should be done
 * by the caller after the search thread terminates.
 */
struct search_argument {
	Position *pos;
	Move *moves;
	int num_moves;
	bool infinite;
	int depth;
	int mate;
	int movestogo;
	int perft;
	long long nodes;
	long long time[2];
	long long inc[2];
	long long movetime;
	void (*best_move_sender)(Move);
	void (*info_sender)(const struct info *);
	pthread_mutex_t *running_mtx;
	bool *running;
};

int search_run(void *data);
void search_finish(void);
void search_clear_hash_table(void);
void search_resize_hash_table(int tt_size);
void search_init(int size);

#endif
