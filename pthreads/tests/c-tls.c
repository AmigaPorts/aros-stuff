/* C thread-local variables from pthreads: _Thread_local with an initializer,
   __thread without one.  Each thread must start with the initial values and
   keep its own copy, also when many threads run one after the other.
   Prints what fails; the exit code is the number of failures.
   Distributed under the pthread library's zlib license.  */

#include <pthread.h>
#include <stdio.h>
#include <proto/dos.h>

static _Thread_local int tl = 42;
static __thread int tz;

static int failures;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static void fail(const char *what, int n, int v)
{
	pthread_mutex_lock(&lock);
	printf("FAIL: %s, thread %d: %d\n", what, n, v);
	failures++;
	pthread_mutex_unlock(&lock);
}

static void *run(void *arg)
{
	int n = (int) (long) arg;

	if (tl != 42)
		fail("tl starts at 42", n, tl);
	if (tz != 0)
		fail("tz starts at 0", n, tz);
	tl = n;
	tz = -n;
	Delay(2);	/* let the others run in between */
	if (tl != n)
		fail("tl kept", n, tl);
	if (tz != -n)
		fail("tz kept", n, tz);
	return NULL;
}

int main(void)
{
	pthread_t t[4];

	tl = 7;
	int started = 0;
	for (; started < 4; started++)
		if (pthread_create(&t[started], NULL, run, (void *) (long) (started + 1))) {
			fail("pthread_create", started + 1, 0);
			break;
		}
	for (int i = 0; i < started; i++)
		pthread_join(t[i], NULL);

	/* one after the other: a new process may reuse an old one's memory */
	for (int i = 0; i < 30; i++) {
		pthread_t one;

		if (pthread_create(&one, NULL, run, (void *) (long) (100 + i))) {
			fail("pthread_create", 100 + i, 0);
			break;
		}
		pthread_join(one, NULL);
	}
	if (tl != 7)
		fail("main's tl kept", 0, tl);
	printf("%d failed\n", failures);
	return failures;
}
