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

//
// Condition variable attribute functions
//

int pthread_condattr_init(pthread_condattr_t *attr) {
	D(bug("%s(%8lx)\n", __FUNCTION__, attr));

	if (attr == NULL)
		return EINVAL;

	memset(attr, 0, sizeof(pthread_condattr_t));

	return 0;
}

int pthread_condattr_destroy(pthread_condattr_t *attr) {
	D(bug("%s(%8lx)\n", __FUNCTION__, attr));

	if (attr == NULL)
		return EINVAL;

	memset(attr, 0, sizeof(pthread_condattr_t));

	return 0;
}

//
// Condition variable functions
//

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
	D(bug("%s(%8lx, %8lx)\n", __FUNCTION__, cond, attr));

	if (cond == NULL)
		return EINVAL;

	InitSemaphore(&cond->semaphore);
	NEWLIST((struct List* )&cond->waiters);

	return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond) {
	D(bug("%s(%8lx)\n", __FUNCTION__, cond));

	if (cond == NULL)
		return EINVAL;

	// probably a statically allocated condition
	if (SemaphoreIsInvalid(&cond->semaphore))
		return 0;

	if (AttemptSemaphore(&cond->semaphore) == FALSE)
		return EBUSY;

	if (!IsListEmpty((struct List*) &cond->waiters)) {
		ReleaseSemaphore(&cond->semaphore);
		return EBUSY;
	}

	ReleaseSemaphore(&cond->semaphore);
	memset(cond, 0, sizeof(pthread_cond_t));

	return 0;
}

#ifndef timersub
extern void timersub(struct timeval *a, struct timeval *b, struct timeval *res);
#endif

static int _pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime, BOOL relative) {
	CondWaiter waiter;
	BYTE signal;
	ULONG sigs = 0;
	ULONG timermask = 0;
	struct MsgPort timermp;
	struct timerequest timerio;
	struct Task *task;

	DB2(bug("%s(%8lx, %8lx, %8lx)\n", __FUNCTION__, cond, mutex, abstime));

	if (cond == NULL || mutex == NULL)
		return EINVAL;

	pthread_testcancel();

	// initialize static conditions
	if (SemaphoreIsInvalid(&cond->semaphore))
		pthread_cond_init(cond, NULL);

#ifdef __AMIGA__
	task = SysBase->ThisTask;
#else
	task = FindTask(NULL);
#endif

	if (abstime) {
		// prepare MsgPort
		timermp.mp_Node.ln_Type = NT_MSGPORT;
		timermp.mp_Node.ln_Pri = 0;
		timermp.mp_Node.ln_Name = NULL;
		timermp.mp_Flags = PA_SIGNAL;
		timermp.mp_SigTask = task;
		signal = AllocSignal(-1);
		if (signal == -1) {
			signal = SIGB_TIMER_FALLBACK;
			SetSignal(SIGF_TIMER_FALLBACK, 0);
		}
		timermp.mp_SigBit = signal;
		NEWLIST(&timermp.mp_MsgList);

		// prepare IORequest
		timerio.tr_node.io_Message.mn_Node.ln_Type = NT_MESSAGE;
		timerio.tr_node.io_Message.mn_Node.ln_Pri = 0;
		timerio.tr_node.io_Message.mn_Node.ln_Name = NULL;
		timerio.tr_node.io_Message.mn_ReplyPort = &timermp;
		timerio.tr_node.io_Message.mn_Length = sizeof(struct timerequest);

		// open timer.device
		if (OpenDevice((STRPTR) TIMERNAME, UNIT_MICROHZ, &timerio.tr_node, 0) != 0) {
			if (timermp.mp_SigBit != SIGB_TIMER_FALLBACK)
				FreeSignal(timermp.mp_SigBit);

			return EINVAL;
		}

		// prepare the device command and send it
		timerio.tr_node.io_Command = TR_ADDREQUEST;
		timerio.tr_node.io_Flags = 0;
		TIMESPEC_TO_TIMEVAL(&timerio.tr_time, abstime);
		if (!relative) {
			struct timeval starttime;
			// absolute time has to be converted to relative
			// GetSysTime can't be used due to the timezone offset in abstime
			gettimeofday(&starttime, NULL);
			timersub(&timerio.tr_time, &starttime, &timerio.tr_time);
		}
		timermask = 1 << timermp.mp_SigBit;
		sigs |= timermask;
		SendIO((struct IORequest*) &timerio);
	}

	// prepare a waiter node
	waiter.task = task;
	signal = AllocSignal(-1);
	if (signal == -1) {
		signal = SIGB_COND_FALLBACK;
		SetSignal(SIGF_COND_FALLBACK, 0);
	}
	waiter.sigmask = 1 << signal;
	sigs |= waiter.sigmask;

	// add it to the end of the list
	ObtainSemaphore(&cond->semaphore);
	AddTail((struct List*) &cond->waiters, (struct Node*) &waiter);
	ReleaseSemaphore(&cond->semaphore);

	// wait for the condition to be signalled or the timeout
	mutex->incond++;
	pthread_mutex_unlock(mutex);
	sigs = Wait(sigs);
	pthread_mutex_lock(mutex);
	mutex->incond--;

	// remove the node from the list
	ObtainSemaphore(&cond->semaphore);
	Remove((struct Node*) &waiter);
	ReleaseSemaphore(&cond->semaphore);

	if (signal != SIGB_COND_FALLBACK)
		FreeSignal(signal);

	if (abstime) {
		// clean up the timerequest
		if (!CheckIO((struct IORequest*) &timerio)) {
			AbortIO((struct IORequest*) &timerio);
			WaitIO((struct IORequest*) &timerio);
		}
		CloseDevice((struct IORequest*) &timerio);

		if (timermp.mp_SigBit != SIGB_TIMER_FALLBACK)
			FreeSignal(timermp.mp_SigBit);

		// did we timeout?
		if (sigs & timermask)
			return ETIMEDOUT;
	}

	return 0;
}

int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime) {
	D(bug("%s(%8lx, %8lx, %8lx)\n", __FUNCTION__, cond, mutex, abstime));

	return _pthread_cond_timedwait(cond, mutex, abstime, FALSE);
}

int pthread_cond_timedwait_relative_np(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *reltime) {
	D(bug("%s(%8lx, %8lx, %8lx)\n", __FUNCTION__, cond, mutex, reltime));

	return _pthread_cond_timedwait(cond, mutex, reltime, TRUE);
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
	D(bug("%s(%8lx)\n", __FUNCTION__, cond));

	return _pthread_cond_timedwait(cond, mutex, NULL, FALSE);
}

static int _pthread_cond_broadcast(pthread_cond_t *cond, BOOL onlyfirst) {
	CondWaiter *waiter;

	DB2(bug("%s(%8lx, %ld)\n", __FUNCTION__, cond, onlyfirst));

	if (cond == NULL)
		return EINVAL;

	// initialize static conditions
	if (SemaphoreIsInvalid(&cond->semaphore))
		pthread_cond_init(cond, NULL);

	// signal the waiting threads 
	ObtainSemaphore(&cond->semaphore);
	ForeachNode(&cond->waiters, waiter)
	{
		Signal(waiter->task, waiter->sigmask);
		if (onlyfirst)
			break;
	}
	ReleaseSemaphore(&cond->semaphore);

	return 0;
}

int pthread_cond_signal(pthread_cond_t *cond) {
	D(bug("%s(%8lx)\n", __FUNCTION__, cond));

	return _pthread_cond_broadcast(cond, TRUE);
}

int pthread_cond_broadcast(pthread_cond_t *cond) {
	D(bug("%s(%8lx)\n", __FUNCTION__, cond));

	return _pthread_cond_broadcast(cond, FALSE);
}

