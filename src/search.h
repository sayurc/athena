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

enum info_flag {
	INFO_FLAG_DEPTH = 0x1,
	INFO_FLAG_NODES = 0x1 << 1,
	INFO_FLAG_NPS = 0x1 << 2,
	INFO_FLAG_MATE = 0x1 << 3,
	INFO_FLAG_TIME = 0x1 << 4,
};

struct info {
	enum info_flag flags;
	int depth;
	long long nodes;
	long long nps;
	long long mate;
	long long time;
};

struct thread_data;

struct search_settings {
	Position **positions;
	size_t num_positions;
	bool infinite;
	int depth;
	int mate;
	int movestogo;
	int perft;
	long long nodes;
	long long time[2];
	long long inc[2];
	void (*best_move_sender)(Move);
	void (*info_sender)(const struct info *);
};

struct search_argument {
	struct search_settings settings;
	pthread_mutex_t *stop_mtx;
	bool *stop;
};

void *search_run(void *data);
void search_finish(void);
void search_init(void);

#endif
