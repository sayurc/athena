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

#ifdef _WIN32
#include <windows.h>
#else
#define _XOPEN_SOURCE 700
#include <pthread.h>
#include <errno.h>
#endif

#include "threads.h"

/*
 * This is a incomplete implementation of C11 threads on top of pthreads and
 * Win32 threads. I only implemented enough for the use case of this program.
 */

#ifdef _WIN32
int thrd_create(thrd_t *thr, thrd_start_t func, void *arg)
{
	*thr = CreateThread(NULL, 0, func, arg, 0, NULL);
	if (*thr)
		return thrd_success;
	else if (GetLastError() == ERROR_NOT_ENOUGH_MEMORY)
		return thrd_nomem;
	return thrd_error;
}

int thrd_join(thrd_t thr, int *res)
{
	int ret = WaitForSingleObject(thr, INFINITE);
	if (ret == WAIT_FAILED)
		return thrd_error;
	ret = GetExitCodeThread(thr, res);
	if (ret)
		return thrd_success;
	return thrd_error;
}

int mtx_init(mtx_t *mutex, int type)
{
	InitializeCriticalSection(mutex);
	return thrd_success;
}

void mtx_destroy(mtx_t *mutex)
{
	DeleteCriticalSection(mutex);
}

int mtx_lock(mtx_t *mutex)
{
	EnterCriticalSection(mutex);
	return thrd_success;
}

int mtx_unlock(mtx_t *mutex)
{
	LeaveCriticalSection(mutex);
	return thrd_success;
}
#else
struct func_wrapper {
	thrd_start_t func;
	void *arg;
	int ret;
};

static void *call(void *wrapper_ptr)
{
	struct func_wrapper *const wrapper = wrapper_ptr;
	wrapper->ret = wrapper->func(wrapper->arg);
	return wrapper;
}

int thrd_create(thrd_t *thr, thrd_start_t func, void *arg)
{
	struct func_wrapper *wrapper = malloc(sizeof(struct func_wrapper));
	if (!wrapper)
		return thrd_nomem;
	wrapper->func = func;
	wrapper->arg = arg;
	const int err = pthread_create(thr, NULL, call, wrapper);
	switch (err) {
	case 0:
		return thrd_success;
	case EAGAIN:
		return thrd_nomem;
	default:
		return thrd_error;
	}
}

int thrd_join(thrd_t thr, int *res)
{
	struct func_wrapper *wrapper;
	const int err = pthread_join(thr, (void **)&wrapper);
	if (err) {
		free(wrapper);
		return thrd_error;
	}
	if (res)
		*res = wrapper->ret;
	free(wrapper);
	return thrd_success;
}

/*
 * In pthreads any mutex can be locked with pthread_mutex_timedlock so we don't
 * need to take mtx_timed into account.
 */
int mtx_init(mtx_t *mutex, int type)
{
	pthread_mutexattr_t mtx_attr;
	int err = pthread_mutexattr_init(&mtx_attr);
	if (err)
		return thrd_error;
	if (type & mtx_recursive)
		pthread_mutexattr_settype(&mtx_attr, PTHREAD_MUTEX_RECURSIVE);
	err = pthread_mutex_init(mutex, NULL);
	if (err)
		return thrd_error;
	return thrd_success;
}

void mtx_destroy(mtx_t *mutex)
{
	pthread_mutex_destroy(mutex);
}

int mtx_lock(mtx_t *mutex)
{
	const int err = pthread_mutex_lock(mutex);
	if (err)
		return thrd_error;
	return thrd_success;
}

int mtx_unlock(mtx_t *mutex)
{
	const int err = pthread_mutex_unlock(mutex);
	if (err)
		return thrd_error;
	return thrd_success;
}
#endif
