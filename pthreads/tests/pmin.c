/* Minimal pthreads: start N threads (argv[1], default 1) that return their
   argument, and join them.  "tls" makes each touch a thread-local first.
   Distributed under the pthread library's zlib license.  */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static __thread int tl;
static int use_tls;

static void *run(void *arg)
{
	if (use_tls)
		tl = (int) (long) arg;
	return arg;
}

int main(int argc, char **argv)
{
	pthread_t t[8];
	int n = argc > 1 ? atoi(argv[1]) : 1;

	if (n < 1 || n > 8) {
		printf("usage: pmin [1-8] [tls]\n");
		return 20;
	}
	use_tls = argc > 2 && !strcmp(argv[2], "tls");

	int started = 0;
	while (started < n
	       && !pthread_create(&t[started], NULL, run, (void *) (long) (started + 1)))
		started++;

	int bad = n - started;	/* the ones that didn't start */
	for (int i = 0; i < started; i++) {
		void *r = NULL;

		pthread_join(t[i], &r);
		bad += r != (void *) (long) (i + 1);
	}
	printf("%d threads, %d bad\n", n, bad);
	return bad;
}
