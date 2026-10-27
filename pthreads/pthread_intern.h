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
#define SIGB_COND_FALLBACK SIGBREAKB_CTRL_E
#define SIGF_COND_FALLBACK (1 << SIGB_COND_FALLBACK)
#define SIGB_TIMER_FALLBACK SIGBREAKB_CTRL_D
#define SIGF_TIMER_FALLBACK (1 << SIGB_TIMER_FALLBACK)

#define NAMELEN 32
#define PTHREAD_FIRST_THREAD_ID (1)
#define PTHREAD_BARRIER_FLAG (1UL << 31)

//#define USE_ASYNC_CANCEL

typedef struct {
	struct MinNode node;
	struct Task *task;
	ULONG sigmask;
} CondWaiter;

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

#if defined(__MORPHOS__) || defined(__AMIGA__)
#define _obtain_sema_timed __pthread_obtain_sema_timed
int _obtain_sema_timed(struct SignalSemaphore *sema, const struct timeval *end, int shared);
#endif

//
// Helper functions
//

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
