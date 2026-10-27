/*
 * The pthread calls libstdc++'s POSIX gthreads make in a program that never
 * starts a thread: a key for the exception globals, once, a mutex and a
 * condition variable for the static-local guards. make check links it and
 * fails if pthread_create came with them.
 *
 * Copyright (c) 2026 Bernie Innocenti. zlib licence, as aros-stuff's pthreads.
 */
#include <pthread.h>

static pthread_key_t key;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

static void init(void)
{
	pthread_key_create(&key, 0);
}

int main(void)
{
	pthread_once(&once, init);
	pthread_setspecific(key, &key);
	pthread_mutex_lock(&mutex);
	pthread_cond_broadcast(&cond);
	pthread_mutex_unlock(&mutex);
	return pthread_getspecific(key) != &key;
}
