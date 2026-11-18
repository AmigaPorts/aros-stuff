/* A task pthread_create didn't start, here one from CreateNewProc, is the
   main thread to pthreads: pthread_self() is 0, and it shares the main
   thread's thread-specific values.
   Distributed under the pthread library's zlib license.  */

#include <pthread.h>
#include <stdio.h>
#include <dos/dostags.h>
#include <proto/dos.h>
#include <proto/exec.h>

static pthread_key_t key;
static int main_value, child_value;
static struct Task *parent;
static pthread_t child_self;
static void *child_saw;
static int child_set;

// in a base-relative build the process starts with no a4 of its own
#ifdef __baserel__
static void __saveds child(void)
#else
static void child(void)
#endif
{
	child_self = pthread_self();
	if (child_self == 0) {
		child_saw = pthread_getspecific(key);
		child_set = pthread_setspecific(key, &child_value);
	}

	// still in Forbid() when the process ends, so main can't unload the code first
	Forbid();
	Signal(parent, SIGBREAKF_CTRL_F);
}

int main(void)
{
	int bad = 0;

	parent = FindTask(NULL);
	pthread_key_create(&key, NULL);
	pthread_setspecific(key, &main_value);

	SetSignal(0, SIGBREAKF_CTRL_F);
	if (!CreateNewProcTags(NP_Entry, (ULONG) child, NP_Name, (ULONG) "foreign child", TAG_DONE)) {
		printf("CreateNewProc failed\n");
		return 20;
	}
	Wait(SIGBREAKF_CTRL_F);

	if (child_self != 0) {
		printf("pthread_self() in the child: %lu, not 0\n", (unsigned long) child_self);
		bad++;
	} else {
		if (child_saw != &main_value) {
			printf("pthread_getspecific() in the child: %p, not the main thread's\n", child_saw);
			bad++;
		}
		if (child_set != 0 || pthread_getspecific(key) != &child_value) {
			printf("pthread_setspecific() in the child: %d, and main sees %p\n",
			       child_set, pthread_getspecific(key));
			bad++;
		}
	}
	if (!bad)
		printf("foreign OK\n");
	return bad;
}
