/*
 Copyright (C) 2014 Szilard Biro
 Copyright (C) 2018 Harry Sintonen
 Copyright (C) 2019/20 Stefan "Bebbo" Franke - AmigaOS 3 port / bug fixes

 Altered in 2026: normalize signed condition timeout intervals.
 Altered in 2026: condition variables on <benaphore.h>, and the timed waits
 in a file of their own, apart from pthread_cond.c.

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

static int timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const benaphore_time_t *deadline) {
	benaphore_recursive_t *r;
	int err;

	if (!(r = CondMutex(mutex)))
		return EPERM;
	mutex->incond++;
	err = benaphore_cond_timedwait_recursive(CondQueue(cond), r, deadline);
	mutex->incond--;

	return err;
}

int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime) {
	benaphore_time_t deadline;

	D(bug("%s(%8lx, %8lx, %8lx)\n", __FUNCTION__, cond, mutex, abstime));

	if (cond == NULL || mutex == NULL || abstime == NULL
			|| abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000L)
		return EINVAL;

	deadline = Deadline(abstime);
	return timedwait(cond, mutex, &deadline);
}

int pthread_cond_timedwait_relative_np(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *reltime) {
	benaphore_time_t deadline;

	D(bug("%s(%8lx, %8lx, %8lx)\n", __FUNCTION__, cond, mutex, reltime));

	if (cond == NULL || mutex == NULL || reltime == NULL || reltime->tv_sec < 0
			|| reltime->tv_nsec < 0 || reltime->tv_nsec >= 1000000000L)
		return EINVAL;

	deadline = DeadlineAfter(reltime);
	return timedwait(cond, mutex, &deadline);
}
