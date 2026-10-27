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
// Read-write lock attribute functions
//

int pthread_rwlockattr_init(pthread_rwlockattr_t *attr) {
	D(bug("%s(%8lx)\n", __FUNCTION__, attr));

	if (attr == NULL)
		return EINVAL;

	memset(attr, 0, sizeof(pthread_rwlockattr_t));

	return 0;
}

int pthread_rwlockattr_destroy(pthread_rwlockattr_t *attr) {
	D(bug("%s(%8lx)\n", __FUNCTION__, attr));

	if (attr == NULL)
		return EINVAL;

	memset(attr, 0, sizeof(pthread_rwlockattr_t));

	return 0;
}

//
// Read-write lock functions
//

int pthread_rwlock_init(pthread_rwlock_t *lock, const pthread_rwlockattr_t *attr) {
	D(bug("%s(%8lx, %8lx)\n", __FUNCTION__, lock, attr));

	if (lock == NULL)
		return EINVAL;

	InitSemaphore(&lock->semaphore);

	return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t *lock) {
	D(bug("%s(%8lx)\n", __FUNCTION__, lock));

	if (lock == NULL)
		return EINVAL;

	// probably a statically allocated rwlock
	if (SemaphoreIsInvalid(&lock->semaphore))
		return 0;

	if (AttemptSemaphore(&lock->semaphore) == FALSE)
		return EBUSY;

	ReleaseSemaphore(&lock->semaphore);
	memset(lock, 0, sizeof(pthread_rwlock_t));

	return 0;
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *lock) {
	ULONG ret;

	D(bug("%s(%8lx)\n", __FUNCTION__, lock));

	if (lock == NULL)
		return EINVAL;

	// initialize static rwlocks
	if (SemaphoreIsInvalid(&lock->semaphore))
		pthread_rwlock_init(lock, NULL);

	ret = AttemptSemaphoreShared(&lock->semaphore);

	return (ret == TRUE) ? 0 : EBUSY;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *lock) {
	ULONG ret;

	D(bug("%s(%8lx)\n", __FUNCTION__, lock));

	if (lock == NULL)
		return EINVAL;

	// initialize static rwlocks
	if (SemaphoreIsInvalid(&lock->semaphore))
		pthread_rwlock_init(lock, NULL);

	ret = AttemptSemaphore(&lock->semaphore);

	return (ret == TRUE) ? 0 : EBUSY;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *lock) {
	D(bug("%s(%8lx)\n", __FUNCTION__, lock));

	if (lock == NULL)
		return EINVAL;

	pthread_testcancel();

	// initialize static rwlocks
	if (SemaphoreIsInvalid(&lock->semaphore))
		pthread_rwlock_init(lock, NULL);

	// "Results are undefined if the calling thread holds a write lock on rwlock at the time the call is made."
#if !defined(__MORPHOS__) && !defined(__AMIGA__)
	// we might already have a write lock
	if (SemaphoreIsMine(&lock->semaphore))
		return EDEADLK;
#endif

	ObtainSemaphoreShared(&lock->semaphore);

	return 0;
}

int pthread_rwlock_timedrdlock(pthread_rwlock_t *lock, const struct timespec *abstime) {
	struct timeval end;
	int result;

	D(bug("%s(%8lx, %8lx)\n", __FUNCTION__, lock, abstime));

	if (lock == NULL)
		return EINVAL;

	if (abstime == NULL)
		return pthread_rwlock_rdlock(lock);
	else if (abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000)
		return EINVAL;

	pthread_testcancel();

	TIMESPEC_TO_TIMEVAL(&end, abstime);

#if defined(__MORPHOS__) || defined(__AMIGA__)
	result = pthread_rwlock_tryrdlock(lock);
	if (result != EBUSY)
		return result;

	return _obtain_sema_timed(&lock->semaphore, &end, 1);
#else
	// busy waiting is not very nice, but ObtainSemaphore doesn't support timeouts
	while ((result = pthread_rwlock_tryrdlock(lock)) == EBUSY)
	{
		struct timeval now;
		sched_yield();
		gettimeofday(&now, NULL);
		if (timercmp(&end, &now, <))
			return ETIMEDOUT;
	}

	return result;
#endif
}

int pthread_rwlock_wrlock(pthread_rwlock_t *lock) {
	D(bug("%s(%8lx)\n", __FUNCTION__, lock));

	if (lock == NULL)
		return EINVAL;

	pthread_testcancel();

	// initialize static rwlocks
	if (SemaphoreIsInvalid(&lock->semaphore))
		pthread_rwlock_init(lock, NULL);

	if (SemaphoreIsMine(&lock->semaphore))
		return EDEADLK;

	ObtainSemaphore(&lock->semaphore);

	return 0;
}

int pthread_rwlock_timedwrlock(pthread_rwlock_t *lock, const struct timespec *abstime) {
	struct timeval end;
	int result;

	D(bug("%s(%8lx, %8lx)\n", __FUNCTION__, lock, abstime));

	if (lock == NULL)
		return EINVAL;

	if (abstime == NULL)
		return pthread_rwlock_wrlock(lock);
	else if (abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000)
		return EINVAL;

	pthread_testcancel();

	TIMESPEC_TO_TIMEVAL(&end, abstime);

#if defined(__MORPHOS__) || defined(__AMIGA__)
	result = pthread_rwlock_trywrlock(lock);
	if (result != EBUSY)
		return result;

	return _obtain_sema_timed(&lock->semaphore, &end, 0);
#else
	// busy waiting is not very nice, but ObtainSemaphore doesn't support timeouts
	while ((result = pthread_rwlock_trywrlock(lock)) == EBUSY)
	{
		struct timeval now;
		sched_yield();
		gettimeofday(&now, NULL);
		if (timercmp(&end, &now, <))
			return ETIMEDOUT;
	}

	return result;
#endif
}

int pthread_rwlock_unlock(pthread_rwlock_t *lock) {
	D(bug("%s(%8lx)\n", __FUNCTION__, lock));

	if (lock == NULL)
		return EINVAL;

	// initialize static rwlocks
	if (SemaphoreIsInvalid(&lock->semaphore))
		pthread_rwlock_init(lock, NULL);

	//if (!SemaphoreIsMine(&lock->semaphore))
	// if no one has obtained the semaphore don't unlock the rwlock
	// this can be a leap of faith because we don't maintain a separate list of readers
	if (lock->semaphore.ss_NestCount < 1)
		return EPERM;

	ReleaseSemaphore(&lock->semaphore);

	return 0;
}

