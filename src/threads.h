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

#ifndef THREADS_H
#define THREADS_H

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

typedef int (*thrd_start_t)(void *);
#ifdef _WIN32
typedef HANDLE thrd_t;
typedef CRITICAL_SECTION mtx_t;
#else
typedef pthread_t thrd_t;
typedef pthread_mutex_t mtx_t;
#endif

enum {
	thrd_success,
	thrd_nomem,
	thrd_timedout,
	thrd_busy,
	thrd_error,
};

enum {
	mtx_plain = 0x1,
	mtx_timed = 0x1 << 1,
	mtx_recursive = 0x1 << 2,
};

int thrd_create(thrd_t *thr, thrd_start_t func, void *arg);
int thrd_join(thrd_t thr, int *res);
int mtx_init(mtx_t *mutex, int type);
void mtx_destroy(mtx_t *mutex);
int mtx_lock(mtx_t *mutex);
int mtx_unlock(mtx_t *mutex);

#endif
