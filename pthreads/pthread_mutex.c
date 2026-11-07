/*
 Copyright (C) 2014 Szilard Biro
 Copyright (C) 2018 Harry Sintonen
 Copyright (C) 2019/20 Stefan "Bebbo" Franke - AmigaOS 3 port / bug fixes

 Altered in 2026: mutexes on <benaphore.h>.

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
// Every kind is a recursive benaphore: its owner and count tell the kinds
// apart, without another call.  All zeros is an unlocked normal mutex, so
// the static initializers need nothing at run time.

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
	D(bug("%s(%8lx, %8lx)\n", __FUNCTION__, mutex, attr));

	if (mutex == NULL)
		return EINVAL;

	memset(mutex, 0, sizeof(pthread_mutex_t));
	mutex->kind = attr ? attr->kind : PTHREAD_MUTEX_DEFAULT;

	return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
	D(bug("%s(%8lx)\n", __FUNCTION__, mutex));

	if (mutex == NULL)
		return EINVAL;

	// locked, or released by a condition wait that will take it back
	if (MutexLock(mutex)->__b.__last || mutex->incond)
		return EBUSY;

	memset(mutex, 0, sizeof(pthread_mutex_t));

	return 0;
}

// The calling task holds it already: a recursive mutex counts, and the
// others refuse rather than deadlock.
static int relock(pthread_mutex_t *mutex) {
	if (mutex->kind != PTHREAD_MUTEX_RECURSIVE)
		return EDEADLK;
	MutexLock(mutex)->__count++;
	return 0;
}

static void own(benaphore_recursive_t *r, void *me) {
	__atomic_store_n(&r->__owner, me, __ATOMIC_RELAXED);
	r->__count = 1;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
	benaphore_recursive_t *r;
	void *me;

	D(bug("%s(%8lx)\n", __FUNCTION__, mutex));

	if (mutex == NULL)
		return EINVAL;

	r = MutexLock(mutex);
	me = SysBase->ThisTask;
	if (__atomic_load_n(&r->__owner, __ATOMIC_RELAXED) == me)
		return relock(mutex);
	benaphore_lock(&r->__b);
	own(r, me);

	return 0;
}

int pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *abstime) {
	benaphore_recursive_t *r;
	benaphore_time_t deadline;
	void *me;
	int err;

	D(bug("%s(%8lx, %7lx)\n", __FUNCTION__, mutex, abstime));

	if (mutex == NULL)
		return EINVAL;

	if (abstime == NULL)
		return pthread_mutex_lock(mutex);
	else if (abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000)
		return EINVAL;

	r = MutexLock(mutex);
	me = SysBase->ThisTask;
	if (__atomic_load_n(&r->__owner, __ATOMIC_RELAXED) == me)
		return relock(mutex);
	deadline = Deadline(abstime);
	if ((err = benaphore_timedlock(&r->__b, &deadline)))
		return err;
	own(r, me);

	return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
	benaphore_recursive_t *r;
	void *me;

	D(bug("%s(%8lx)\n", __FUNCTION__, mutex));

	if (mutex == NULL)
		return EINVAL;

	r = MutexLock(mutex);
	me = SysBase->ThisTask;
	if (__atomic_load_n(&r->__owner, __ATOMIC_RELAXED) == me)
		return mutex->kind == PTHREAD_MUTEX_RECURSIVE ? relock(mutex) : EBUSY;
	if (!benaphore_trylock(&r->__b))
		return EBUSY;
	own(r, me);

	return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
	benaphore_recursive_t *r;

	D(bug("%s(%8lx)\n", __FUNCTION__, mutex));

	if (mutex == NULL)
		return EINVAL;

	r = MutexLock(mutex);
	if (__atomic_load_n(&r->__owner, __ATOMIC_RELAXED) != SysBase->ThisTask)
		return EPERM;
	benaphore_recursive_unlock(r);

	return 0;
}
