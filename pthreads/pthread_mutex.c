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

// Mutex attribute functions
//

int pthread_mutexattr_init(pthread_mutexattr_t *attr) {
	D(bug("%s(%8lx)\n", __FUNCTION__, attr));

	if (attr == NULL)
		return EINVAL;

	attr->kind = PTHREAD_MUTEX_DEFAULT;

	return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr) {
	D(bug("%s(%8lx)\n", __FUNCTION__, attr));

	if (attr == NULL)
		return EINVAL;

	memset(attr, 0, sizeof(pthread_mutexattr_t));

	return 0;
}

int pthread_mutexattr_gettype(pthread_mutexattr_t *attr, int *kind) {
	D(bug("%s(%8lx, %8lx)\n", __FUNCTION__, attr, kind));

	if (attr == NULL)
		return EINVAL;

	if (kind)
		*kind = attr->kind;

	return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int kind) {
	D(bug("%s(%8lx)\n", __FUNCTION__, attr));

	if (attr == NULL || !(kind >= PTHREAD_MUTEX_NORMAL && kind <= PTHREAD_MUTEX_ERRORCHECK))
		return EINVAL;

	attr->kind = kind;

	return 0;
}

//
// Mutex functions
//

static int _pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr, BOOL staticinit) {
	DB2(bug("%s(%8lx, %8lx)\n", __FUNCTION__, mutex, attr));

	if (mutex == NULL)
		return EINVAL;

	if (attr)
		mutex->kind = attr->kind;
	else if (!staticinit)
		mutex->kind = PTHREAD_MUTEX_DEFAULT;
	InitSemaphore(&mutex->semaphore);
	mutex->incond = 0;

	return 0;
}

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
	D(bug("%s(%8lx, %8lx)\n", __FUNCTION__, mutex, attr));

	return _pthread_mutex_init(mutex, attr, FALSE);
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
	D(bug("%s(%8lx)\n", __FUNCTION__, mutex));

	if (mutex == NULL)
		return EINVAL;

	// probably a statically allocated mutex
	if (SemaphoreIsInvalid(&mutex->semaphore))
		return 0;

	if (/*mutex->incond ||*/AttemptSemaphore(&mutex->semaphore) == FALSE)
		return EBUSY;

	if (mutex->incond) {
		ReleaseSemaphore(&mutex->semaphore);
		return EBUSY;
	}

	ReleaseSemaphore(&mutex->semaphore);
	memset(mutex, 0, sizeof(pthread_mutex_t));

	return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
	D(bug("%s(%8lx)\n", __FUNCTION__, mutex));

	if (mutex == NULL)
		return EINVAL;

	struct SignalSemaphore *sigSem = &mutex->semaphore;

	// initialize static mutexes
	if (SemaphoreIsInvalid(sigSem))
		_pthread_mutex_init(mutex, NULL, TRUE);

	// normal mutexes would simply deadlock here
	if (mutex->kind == PTHREAD_MUTEX_ERRORCHECK && SemaphoreIsMine(sigSem))
		return EDEADLK;

	ObtainSemaphore(sigSem);

	if (mutex->kind == PTHREAD_MUTEX_NORMAL && sigSem->ss_NestCount > 1) {
		// should have blocked - fix this
		ReleaseSemaphore(sigSem);
		return EDEADLK;
	}
#ifndef __AMIGA__
	if (from->tv_secs >= unix_to_amiga)
	{
		to->tv_secs = from->tv_secs - unix_to_amiga;
		to->tv_micro = from->tv_micro;
	}
	else
	{
		to->tv_secs = 0;
		to->tv_micro = 0;
	}
#endif
	return 0;
}
#if defined(__MORPHOS__) || defined(__AMIGA__)
int _obtain_sema_timed(struct SignalSemaphore *sema, const struct timeval *end, int shared) {
	struct MsgPort msgport;
	struct SemaphoreMessage msg;
#ifndef __AMIGA__
	struct Message *m1, *m2;
#endif
	struct timerequest timerio;
	struct Task *task;

#ifdef __AMIGA__
	task = SysBase->ThisTask;
#else
	task = FindTask(NULL);
#endif

	msgport.mp_SigBit = AllocSignal(-1);
	if ((BYTE) msgport.mp_SigBit == -1)
		return EINVAL;
	msgport.mp_Node.ln_Type = NT_MSGPORT;
	msgport.mp_Flags = PA_SIGNAL;
	msgport.mp_SigTask = task;
	NEWLIST(&msgport.mp_MsgList);

	msg.ssm_Semaphore = 0;
	msg.ssm_Message.mn_Node.ln_Type = NT_MESSAGE;
	msg.ssm_Message.mn_Node.ln_Name = (char*) shared;
	msg.ssm_Message.mn_ReplyPort = &msgport;

	timerio.tr_node.io_Message.mn_Node.ln_Type = NT_MESSAGE;
	timerio.tr_node.io_Command = TR_ADDREQUEST;
	timerio.tr_node.io_Message.mn_ReplyPort = &msgport;
#ifdef __AMIGA__
	timerio.tr_time = *end;
	timerio.tr_node.io_Device = DOSBase->dl_TimeReq->tr_node.io_Device;
	timerio.tr_node.io_Unit = DOSBase->dl_TimeReq->tr_node.io_Unit;

	// Procure is broken on older systems... hand made...
	struct SemaphoreRequest sr;
	sr.sr_Waiter = task;

	SendIO((APTR) & timerio);

	ULONG mask = SIGF_SINGLE | (1 << msgport.mp_SigBit);
	Forbid();
	task->tc_SigRecvd &= ~mask;
	AddTail((struct List*) &sema->ss_WaitQueue, (struct Node*) &sr.sr_Link);
	ULONG signal = Wait(mask);
	Permit();

	if (signal & SIGF_SINGLE) {
		msg.ssm_Semaphore = sema;
	}
#else
	UNIXTIME_TO_AMIGATIME(end, &timerio.tr_time);
	timerio.tr_node.io_Device = waitutc.tr_node.io_Device;
	timerio.tr_node.io_Unit	= waitutc.tr_node.io_Unit;

	Procure(sema, &msg);
	SendIO((APTR)&timerio);

	WaitPort(&msgport);
	m1 = GetMsg(&msgport);
	m2 = GetMsg(&msgport);
	if (m1 == &timerio.tr_node.io_Message || m2 == &timerio.tr_node.io_Message)
		Vacate(sema, &msg);
#endif

	else {
		AbortIO((APTR) & timerio);
		WaitIO((APTR) & timerio);
	}
	FreeSignal(msgport.mp_SigBit);
	if (msg.ssm_Semaphore == 0)
		return ETIMEDOUT;
	return 0;
}
#endif

int pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *abstime) {
	struct timeval end;
#ifndef __AMIGA__
	struct timeval now;
#endif
	int result;

	D(bug("%s(%8lx, %7lx)\n", __FUNCTION__, mutex, abstime));

	if (mutex == NULL)
		return EINVAL;

	if (abstime == NULL)
		return pthread_mutex_lock(mutex);
	else if (abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000)
		return EINVAL;

	TIMESPEC_TO_TIMEVAL(&end, abstime);

#if defined(__MORPHOS__) || defined(__AMIGA__)
	result = pthread_mutex_trylock(mutex);
	if (result != EBUSY)
		return result;

	return _obtain_sema_timed(&mutex->semaphore, &end, 0);
#else
	// busy waiting is not very nice, but ObtainSemaphore doesn't support timeouts
	while ((result = pthread_mutex_trylock(mutex)) == EBUSY)
	{
		sched_yield();
		gettimeofday(&now, NULL);
		if (timercmp(&end, &now, <))
			return ETIMEDOUT;
	}

	return result;
#endif
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
	ULONG ret;

	D(bug("%s(%8lx)\n", __FUNCTION__, mutex));

	if (mutex == NULL)
		return EINVAL;

	// initialize static mutexes
	if (SemaphoreIsInvalid(&mutex->semaphore))
		_pthread_mutex_init(mutex, NULL, TRUE);

	if (mutex->kind != PTHREAD_MUTEX_RECURSIVE && SemaphoreIsMine(&mutex->semaphore))
		return EDEADLK;

	ret = AttemptSemaphore(&mutex->semaphore);

	return (ret == TRUE) ? 0 : EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
	D(bug("%s(%8lx)\n", __FUNCTION__, mutex));

	if (mutex == NULL)
		return EINVAL;

	// initialize static mutexes
	if (SemaphoreIsInvalid(&mutex->semaphore))
		_pthread_mutex_init(mutex, NULL, TRUE);

	if (mutex->kind != PTHREAD_MUTEX_NORMAL && !SemaphoreIsMine(&mutex->semaphore))
		return EPERM;

	ReleaseSemaphore(&mutex->semaphore);

	return 0;
}

