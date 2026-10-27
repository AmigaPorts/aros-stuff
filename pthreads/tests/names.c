/* names: each new thread is named "pthread thread #<id>", padded with spaces
 * to leave room for pthread_setname_np. Starts THREADS threads that stay alive
 * together, so the ids reach two digits, and checks each one's name from inside
 * it. Prints "names OK" (rc 0) or "names FAILED" (rc 20).
 *
 *   m68k-amigaos-gcc -mcrt=nix20 -m68020 -O2 -Wall -o names names.c -lpthread
 *
 * Copyright (c) 2026 Bernie Innocenti. zlib licence, as aros-stuff's pthreads. */
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define THREADS 12

static pthread_barrier_t barrier;
static int failed;

static void *run(void *arg)
{
	char want[32], name[64];
	size_t len;

	(void)arg;
	snprintf(want, sizeof(want), "pthread thread #%lu", (unsigned long)pthread_self());
	len = strlen(want);
	if (pthread_getname_np(pthread_self(), name, sizeof(name)) != 0
	    || strncmp(name, want, len) != 0 || strspn(name + len, " ") != strlen(name + len)) {
		printf("FAIL thread %lu is named \"%s\"\n", (unsigned long)pthread_self(), name);
		failed = 1;
	}
	// stay alive until all of them have started, so no id is reused
	pthread_barrier_wait(&barrier);
	return NULL;
}

int main(void)
{
	pthread_t t[THREADS];
	int i;

	pthread_barrier_init(&barrier, NULL, THREADS);
	for (i = 0; i < THREADS; i++) {
		if (pthread_create(&t[i], NULL, run, NULL) != 0) {
			printf("FAIL pthread_create %d\n", i);
			return 20;
		}
	}
	for (i = 0; i < THREADS; i++)
		pthread_join(t[i], NULL);
	if (t[THREADS - 1] < 10) {
		printf("FAIL the last id is %lu, not two digits\n", (unsigned long)t[THREADS - 1]);
		failed = 1;
	}
	puts(failed ? "names FAILED" : "names OK");
	return failed ? 20 : 0;
}
