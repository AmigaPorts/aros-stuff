/*
 Copyright (C) 2014 Szilard Biro
 Copyright (C) 2018 Harry Sintonen
 Copyright (C) 2019/20 Stefan "Bebbo" Franke - AmigaOS 3 port / bug fixes

 This software is provided 'as-is', without any express or implied
 warranty.  In no event will the authors be held liable for any damages
 arising from the use of this software.

 Permission is granted to anyone to use this software for any purpose,
 including commercial applications, and to alter it and redistribute it
 freely, subject to the following restrictions:

 1. The origin of this software must not be misrepresented; you must not
 claim that you wrote the original software. If you use this software
 in a product, an acknowledgment in the product documentation would be
 appreciated but is not required.
 2. Altered source versions must be plainly marked as such, and must not be
 misrepresented as being the original software.
 3. This notice may not be removed or altered from any source distribution.
 */

#ifndef PTHREAD_INTERN_H
#define PTHREAD_INTERN_H

#ifdef __MORPHOS__
#include <sys/time.h>
#endif
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#ifdef __AROS__
#include <aros/symbolsets.h>
#define	TIMESPEC_TO_TIMEVAL(tv, ts) {	\
	(tv)->tv_sec = (ts)->tv_sec;		\
	(tv)->tv_usec = (ts)->tv_nsec / 1000; }
#elif !defined(__AMIGA__)
#include <constructor.h>
#define StackSwapArgs PPCStackSwapArgs
#define NewStackSwap NewPPCStackSwap
#endif

#define __PTHREAD_SEMAPHORE_TYPE struct SignalSemaphore
#define __PTHREAD_MINLIST_TYPE struct MinList

#include <setjmp.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/time.h>

#include "pthread.h"
#include "debug.h"
#include <benaphore.h>

#if defined(__AMIGA__)
#include <exec/execbase.h>
#include <inline/alib.h>
#define NEWLIST(a) NewList(a)

#include <stabs.h>

#define INITIALSIZE 1

#ifndef IPTR
#define IPTR ULONG
#endif

#   define ForeachNode(l,n) \
	for (n=(void *)(l)->mlh_Head; \
	    ((struct Node *)(n))->ln_Succ; \
	    n=(void *)(((struct Node *)(n))->ln_Succ))
#endif

#define SIGB_PARENT SIGBREAKB_CTRL_F
#define SIGF_PARENT (1 << SIGB_PARENT)

#define NAMELEN 32
#define PTHREAD_FIRST_THREAD_ID (1)
#define PTHREAD_BARRIER_FLAG (1UL << 31)

//#define USE_ASYNC_CANCEL

typedef struct {
	void (*destructor)(void*);
	short used;
} TLSKey;

typedef struct {
	struct MinNode node;
	void (*routine)(void*);
	void *arg;
} CleanupHandler;

typedef struct {
	void* (*start)(void*);
	void *arg;
	struct Task *parent;
	struct Task *joiner;
	ULONG joinmask;		// the signal the joiner waits for, if one was available
	struct Task *task;
#ifdef __baserel__
	void *a4;		// the creator's a4: this program's (or this invocation's, if resident) data
	pthread_t self;		// the thread's id, which pr_CIS carries in a plain build
#endif
	void *ret;
	pthread_attr_t attr;
	void *tlsvalues[PTHREAD_KEYS_MAX];
	struct MinList cleanup;
	char started;
	char finished;
	short cancelstate;
	short canceltype;
	short canceled;
	jmp_buf jmp;
} ThreadInfo;

/* The library's state, shared by its objects: pthread_core.c defines it.
   The names stay in the implementation's namespace, so they can't clash
   with a program's. */
#define _threads __pthread_threads
#define numThreads __pthread_numThreads
#define thread_sem __pthread_thread_sem
#define _tlskeys __pthread_tlskeys
#define numTlskeys __pthread_numTlskeys
#define tls_sem __pthread_tls_sem
extern ThreadInfo **_threads;
extern unsigned numThreads;
extern struct SignalSemaphore thread_sem;
extern TLSKey *_tlskeys;
extern unsigned numTlskeys;
extern struct SignalSemaphore tls_sem;

/* pthread_join, once pthread_create has run: the exit code joins the threads
   still running through it, so a program that never creates one doesn't
   link the thread code. */
extern int (*__pthread_join_hook)(pthread_t, void **);

//
// Helper functions
//

/* pthread.h lays out <benaphore.h>'s types for its users, so that it needn't
   include that: libpthread takes them as the real ones. */
_Static_assert(sizeof(struct __pthread_benaphore) == sizeof(benaphore_recursive_t)
	       && __builtin_offsetof(struct __pthread_benaphore, __owner)
		  == __builtin_offsetof(benaphore_recursive_t, __owner)
	       && __builtin_offsetof(struct __pthread_benaphore, __count)
		  == __builtin_offsetof(benaphore_recursive_t, __count),
	       "pthread_mutex_t's lock is not a benaphore_recursive_t");
_Static_assert(sizeof(pthread_cond_t) == sizeof(benaphore_cond_t),
	       "pthread_cond_t is not a benaphore_cond_t");

static inline benaphore_recursive_t *MutexLock(pthread_mutex_t *mutex) {
	return (benaphore_recursive_t *) &mutex->lock;
}

static inline benaphore_cond_t *CondQueue(pthread_cond_t *cond) {
	return (benaphore_cond_t *) cond;
}

/* The timed waits have files of their own, so that a program that doesn't
   use them doesn't link <benaphore.h>'s timed code.  These are shared. */

// The calling task holds it already: a recursive mutex counts, and the
// others refuse rather than deadlock.
static inline int MutexRelock(pthread_mutex_t *mutex) {
	if (mutex->kind != PTHREAD_MUTEX_RECURSIVE)
		return EDEADLK;
	MutexLock(mutex)->__count++;
	return 0;
}

static inline void MutexOwn(benaphore_recursive_t *r, void *me) {
	__atomic_store_n(&r->__owner, me, __ATOMIC_RELAXED);
	r->__count = 1;
}

// The lock of the mutex a condition wait gives up, whatever its count, and
// takes back with it; NULL if the calling task doesn't hold it.
static inline benaphore_recursive_t *CondMutex(pthread_mutex_t *mutex) {
	benaphore_recursive_t *r;

	pthread_testcancel();
	r = MutexLock(mutex);
	if (__atomic_load_n(&r->__owner, __ATOMIC_RELAXED) != SysBase->ThisTask)
		return NULL;
	return r;
}

/* A timespec deadline, as <benaphore.h> takes it. */
static inline benaphore_time_t Deadline(const struct timespec *ts) {
	benaphore_time_t d;

	d.__sec = ts->tv_sec;
	d.__nsec = ts->tv_nsec;
	return d;
}

/* The deadline rel after now, on the clock of the absolute deadlines.  Not
   with timespecadd(): time_t is 32 bits, and a long wait would overflow it. */
static inline benaphore_time_t DeadlineAfter(const struct timespec *rel) {
	benaphore_time_t d;
	struct timeval now;

	gettimeofday(&now, NULL);
	d.__sec = (long long) now.tv_sec + rel->tv_sec;
	d.__nsec = now.tv_usec * 1000L + rel->tv_nsec;
	if (d.__nsec >= 1000000000L) {
		d.__sec++;
		d.__nsec -= 1000000000L;
	}
	return d;
}

/* The time left until end, a deadline on gettimeofday()'s clock, in the
   seconds and microseconds of a timer request; 0 if it has passed.
   timersub() keeps the two apart, so no 64-bit arithmetic is needed. */
static inline int TimeLeft(const struct timeval *end, ULONG *secs, ULONG *micro) {
	struct timeval now, left;

	gettimeofday(&now, NULL);
	if (!timercmp(end, &now, >))
		return 0;
	timersub(end, &now, &left);
	*secs = left.tv_sec;
	*micro = left.tv_usec;
	return 1;
}

static inline int SemaphoreIsInvalid(struct SignalSemaphore *sem) {
	int r = (!sem || sem->ss_Link.ln_Type != NT_SIGNALSEM || sem->ss_WaitQueue.mlh_Tail != NULL);
	DB2(bug("%s(%8lx)=%ld\n", __FUNCTION__, sem, r));
	return r;
}

static inline int SemaphoreIsMine(struct SignalSemaphore *sem) {
	struct Task *me;

	DB2(bug("%s(%8lx)\n", __FUNCTION__, sem));

#ifdef __AMIGA__
	me = SysBase->ThisTask;
#else
	me = FindTask(NULL);
#endif
	return (sem && sem->ss_NestCount > 0 && sem->ss_Owner == me);
}

static inline ThreadInfo* GetThreadInfo(pthread_t thread) {
	DB2(bug("%s(%ld)\n", __FUNCTION__, thread));

	// TODO: more robust error handling?
	if (thread < numThreads)
		return _threads[thread];

	return 0;
}

static inline pthread_t GetThreadId(struct Task *task) {
	pthread_t i;

// would recurse...
//	DB2(bug("%s(%ld)\n", __FUNCTION__, task));

// 0 is main task, First thread id will be 1 so that it is different than default value of pthread_t
	for (i = 0; i < numThreads; i++) {
		if (_threads[i]->task == task)
			break;
	}

	return i;
}

#endif /* PTHREAD_INTERN_H */
