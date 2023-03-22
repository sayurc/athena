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
};

struct search_info {
	enum info_flag flags;
	int depth;
	long long nodes;
	long long nps;
	long long mate;
};

enum search_type {
	SEARCH_TYPE_NORMAL,
	SEARCH_TYPE_INFINITE,
	SEARCH_TYPE_FIND_MATE,
}; 

struct search_settings {
	Position *position;
	enum search_type type;
	int depth;
	long long nodes;
	int moves_to_mate;
};

struct internal_data;

struct thread_data {
	pthread_mutex_t stop_mtx;
	bool stop;
	struct search_settings settings;
	void (*best_move_sender)(Move);
	void (*info_sender)(const struct search_info *);
	struct internal_data *internal;
};

void *search_get_best_move(void *data);
void search_finish(void);
void search_init(void);

#endif
