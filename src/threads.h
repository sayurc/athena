#ifndef THREADS_H
#define THREADS_H

#ifdef _WIN32
#else
#include <pthread.h>
#endif

#ifdef _WIN32
#else
typedef pthread_t thrd_t;
typedef int (*thrd_start_t)(void *);
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
