/*
 * Creates and joins a thread, and uses nothing from stdio: make check links
 * it and fails if libpthread brought in snprintf.
 *
 * Copyright (c) 2026 Bernie Innocenti. zlib licence, as aros-stuff's pthreads.
 */
#include <pthread.h>

static void *run(void *arg)
{
	return arg;
}

int main(void)
{
	pthread_t t;
	void *ret;

	if (pthread_create(&t, 0, run, &t) || pthread_join(t, &ret))
		return 1;
	return ret != &t;
}
