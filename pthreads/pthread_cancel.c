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

int pthread_cancel(pthread_t thread) {
	ThreadInfo *inf;

	D(bug("%s(%ld)\n", __FUNCTION__, thread));

	ObtainSemaphore(&thread_sem);
	inf = GetThreadInfo(thread);

	if (inf == NULL || inf->parent == NULL || inf->canceled == TRUE) {
		ReleaseSemaphore(&thread_sem);
		return ESRCH;
	}

	inf->canceled = TRUE;

	// we might have to cancel the thread immediately
	if (inf->canceltype == PTHREAD_CANCEL_ASYNCHRONOUS && inf->cancelstate == PTHREAD_CANCEL_ENABLE) {
		struct Task *task;

#ifdef __AMIGA__
		task = SysBase->ThisTask;
#else
		task = FindTask(NULL);
#endif

		if (inf->task == task)
			pthread_testcancel(); // cancel ourselves
		else
			Signal(inf->task, SIGBREAKF_CTRL_C); // trigger the exception handler 
	}
	ReleaseSemaphore(&thread_sem);

	return 0;
}

int pthread_setcancelstate(int state, int *oldstate) {
	pthread_t thread;
	ThreadInfo *inf;

	D(bug("%s(%ld, %8lx)\n", __FUNCTION__, state, oldstate));

	if (state != PTHREAD_CANCEL_ENABLE && state != PTHREAD_CANCEL_DISABLE)
		return EINVAL;

	thread = pthread_self();
	inf = GetThreadInfo(thread);

	if (oldstate)
		*oldstate = inf->cancelstate;

	inf->cancelstate = state;

	return 0;
}

int pthread_setcanceltype(int type, int *oldtype) {
	pthread_t thread;
	ThreadInfo *inf;

	D(bug("%s(%ld, %8lx)\n", __FUNCTION__, type, oldtype));

	if (type != PTHREAD_CANCEL_DEFERRED && type != PTHREAD_CANCEL_ASYNCHRONOUS)
		return EINVAL;

	thread = pthread_self();

	ObtainSemaphore(&thread_sem);
	inf = GetThreadInfo(thread);

	if (oldtype)
		*oldtype = inf->canceltype;

	inf->canceltype = type;
	ReleaseSemaphore(&thread_sem);

	return 0;
}

void pthread_testcancel(void) {
	pthread_t thread;
	ThreadInfo *inf;
	int flag;

	D(bug("%s()\n", __FUNCTION__));

	thread = pthread_self();

	ObtainSemaphore(&thread_sem);
	inf = GetThreadInfo(thread);

	flag = inf->canceled && (inf->cancelstate == PTHREAD_CANCEL_ENABLE);
	ReleaseSemaphore(&thread_sem);

	if (flag)
		pthread_exit(PTHREAD_CANCELED);
}

void pthread_exit(void *value_ptr) {
	pthread_t pid;
	ThreadInfo *inf;
	CleanupHandler *handler;

	pid = pthread_self();

	D(bug("%s(pid=%ld, %8lx)\n", __FUNCTION__, pid, value_ptr));

	ObtainSemaphore(&thread_sem);
	inf = GetThreadInfo(pid);
	inf->ret = value_ptr;

	// execute the clean-up handlers
	while ((handler = (CleanupHandler*) RemTail((struct List*) &inf->cleanup)))
		if (handler->routine)
			handler->routine(handler->arg);

	// only for real threads.
	if (pid) {
		Forbid();
		ReleaseSemaphore(&thread_sem);
		longjmp(inf->jmp, 1);
	}
	ReleaseSemaphore(&thread_sem);

}


//
// Cancellation cleanup
//

void pthread_cleanup_push(void (*routine)(void*), void *arg) {
	pthread_t thread;
	ThreadInfo *inf;
	CleanupHandler *handler;

	D(bug("%s(%8lx, %8lx)\n", __FUNCTION__, routine, arg));

	if (routine == NULL)
		return;

	handler = malloc(sizeof(CleanupHandler));

	if (handler == NULL)
		return;

	thread = pthread_self();

	handler->routine = routine;
	handler->arg = arg;

	ObtainSemaphore(&thread_sem);
	inf = GetThreadInfo(thread);
	AddTail((struct List*) &inf->cleanup, (struct Node*) handler);
	ReleaseSemaphore(&thread_sem);
}

void pthread_cleanup_pop(int execute) {
	pthread_t thread;
	ThreadInfo *inf;
	CleanupHandler *handler;

	D(bug("%s(%d)\n", __FUNCTION__, execute));

	thread = pthread_self();

	ObtainSemaphore(&thread_sem);
	inf = GetThreadInfo(thread);
	handler = (CleanupHandler*) RemTail((struct List*) &inf->cleanup);
	ReleaseSemaphore(&thread_sem);

	if (handler && handler->routine && execute)
		handler->routine(handler->arg);

	free(handler);
}

