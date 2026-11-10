/*
 Copyright (C) 2014 Szilard Biro
 Copyright (C) 2018 Harry Sintonen
 Copyright (C) 2019/20 Stefan "Bebbo" Franke - AmigaOS 3 port / bug fixes

 Altered in 2026: mutexes on <benaphore.h>, and pthread_mutex_timedlock()
 in a file of its own, apart from pthread_mutex.c.

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
		return MutexRelock(mutex);
	deadline = Deadline(abstime);
	if ((err = benaphore_timedlock(&r->__b, &deadline)))
		return err;
	MutexOwn(r, me);

	return 0;
}
