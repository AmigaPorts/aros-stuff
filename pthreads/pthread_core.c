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

#include "pthread_intern.h"

/* One ThreadInfo per slot, each allocated once and never moved: a running
   thread keeps its ThreadInfo pointer for its whole life (StarterFunc), so
   growing the table must not realloc the ThreadInfos themselves. */
ThreadInfo **_threads;
unsigned numThreads;
struct SignalSemaphore thread_sem;
TLSKey *_tlskeys;
unsigned numTlskeys;
struct SignalSemaphore tls_sem;

int (*__pthread_join_hook)(pthread_t, void **);

int pthread_equal(pthread_t t1, pthread_t t2) {
	D(bug("%s(%ld, %ld)\n", __FUNCTION__, t1, t2));

	return (t1 == t2);
}

pthread_t pthread_self(void) {
	struct Task *task;
	pthread_t thread;

//	D(bug("%s()\n", __FUNCTION__));

#ifdef __AMIGA__
	task = SysBase->ThisTask;
#else
	task = FindTask(NULL);
#endif

	thread = GetThreadId(task);

	if (thread == PTHREAD_THREADS_MAX)
		return 0;

	return thread;
}


//
// Constructors, destructors
//

#ifndef __AMIGA__
static
#endif
int __pthread_Init_Func(void) {
	D(bug("%s\n", "********************"));

	if (sizeof(struct SignalSemaphore) > sizeof(struct __OpaqueSemaphore))
		perror("__OpaqueSemaphore to small");

	if (sizeof(struct MinList) > sizeof(struct __OpaqueMinList))
		perror("__OpaqueMinList to small");

	DB2(bug("%s()\n", __FUNCTION__));

#ifdef __MORPHOS__
	memset(&waitutc, 0, sizeof(waitutc));
	if (OpenDevice("timer.device", UNIT_WAITUTC, (APTR) &waitutc, 0) != 0)
		return FALSE;
#endif
	//memset(&threads, 0, sizeof(threads));
	InitSemaphore(&thread_sem);
	InitSemaphore(&tls_sem);

	// reserve ID 0 for the main thread
	_threads = (ThreadInfo**) calloc(PTHREAD_THREADS_MAX, sizeof(ThreadInfo*));
	if (!_threads)
		exit(10);
	for (numThreads = 0; numThreads < INITIALSIZE; numThreads++) {
		_threads[numThreads] = (ThreadInfo*) calloc(1, sizeof(ThreadInfo));
		if (!_threads[numThreads])
			exit(10);
	}

	ThreadInfo *inf = _threads[0];
#ifdef __AMIGA__
	inf->task = SysBase->ThisTask;
#else
    inf->task = FindTask(NULL);
#endif

	NEWLIST((struct List* )&inf->cleanup);
	return TRUE;
}

#ifndef __AMIGA__
static
#endif
void __pthread_Exit_Func(void) {
#if defined(__MORPHOS__) || defined(__AMIGA__)
	pthread_t i;
#endif

	/* main thread is no longer cancelable. */
	GetThreadInfo(pthread_self())->cancelstate = PTHREAD_CANCEL_DISABLE;

	DB2(bug("%s()\n", __FUNCTION__));

	// wait for the threads?
#if defined(__MORPHOS__) || defined(__AMIGA__)

	// if we don't do this we can easily end up with unloaded code being executed
	for (i = 1; __pthread_join_hook && i < numThreads; i++) {
		for (;;) {
			ThreadInfo *inf;
			ObtainSemaphore(&thread_sem);
			inf = GetThreadInfo(i);
			if (!inf->task) {
				ReleaseSemaphore(&thread_sem);
				break;
			}
			if (inf->joiner) {
				// The registered joiner must reclaim the slot itself.
				ReleaseSemaphore(&thread_sem);
				Delay(1);
				continue;
			}
			inf->attr.detachstate = PTHREAD_CREATE_JOINABLE;
			ReleaseSemaphore(&thread_sem);
			if (__pthread_join_hook(i, NULL) == 0)
				break;
			// Another task may have registered as joiner meanwhile.
			Delay(1);
		}
	}

#endif
#ifdef __MORPHOS__
	CloseDevice((APTR) &waitutc);
#endif
}

#if defined(__MORPHOS__) || defined(__AMIGA__)
ADD2INIT(__pthread_Init_Func, -6); // below init cpp with -5 - it's needed there
ADD2EXIT(__pthread_Exit_Func, -6);
#else
static CONSTRUCTOR_P(__pthread_Init_Func, 100)
{
	return !__pthread_Init_Func();
}

static DESTRUCTOR_P(__pthread_Exit_Func, 100)
{
	__pthread_Exit_Func();
}
#endif

