/*
 Copyright (C) 2014 Szilard Biro
 Copyright (C) 2018 Harry Sintonen
 Copyright (C) 2019/20 Stefan "Bebbo" Franke - AmigaOS 3 port / bug fixes

 Altered in 2026: normalize signed condition timeout intervals.
 Altered in 2026: condition variables on <benaphore.h>.

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
// <benaphore.h>'s condition variables: a queue of the waiting tasks, which
// the wait joins and gives the mutex up in one step for the tasks that
// signal.  All zeros has no waiters, so the static initializer needs
// nothing at run time.

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
	D(bug("%s(%8lx, %8lx)\n", __FUNCTION__, cond, attr));

	if (cond == NULL)
		return EINVAL;

	memset(cond, 0, sizeof(pthread_cond_t));

	return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond) {
	D(bug("%s(%8lx)\n", __FUNCTION__, cond));

	if (cond == NULL)
		return EINVAL;

	if (CondQueue(cond)->__head)
		return EBUSY;

	memset(cond, 0, sizeof(pthread_cond_t));

	return 0;
}

// pthread_cond_timedwait() and pthread_cond_timedwait_relative_np() are in
// pthread_cond_timed.c

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
	benaphore_recursive_t *r;

	D(bug("%s(%8lx)\n", __FUNCTION__, cond));

	if (cond == NULL || mutex == NULL)
		return EINVAL;

	if (!(r = CondMutex(mutex)))
		return EPERM;
	mutex->incond++;
	benaphore_cond_wait_recursive(CondQueue(cond), r);
	mutex->incond--;

	return 0;
}

int pthread_cond_signal(pthread_cond_t *cond) {
	D(bug("%s(%8lx)\n", __FUNCTION__, cond));

	if (cond == NULL)
		return EINVAL;

	benaphore_cond_signal(CondQueue(cond));

	return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond) {
	D(bug("%s(%8lx)\n", __FUNCTION__, cond));

	if (cond == NULL)
		return EINVAL;

	benaphore_cond_broadcast(CondQueue(cond));

	return 0;
}
