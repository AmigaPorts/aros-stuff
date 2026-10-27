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
// Barrier functions
//

int pthread_barrier_init(pthread_barrier_t *barrier, const pthread_barrierattr_t *attr, unsigned int count) {
	D(bug("%s(%8lx, %8lx, %ld)\n", __FUNCTION__, barrier, attr, count));

	if (barrier == NULL || count == 0)
		return EINVAL;

	barrier->curr_height = count;
	barrier->total_height = PTHREAD_BARRIER_FLAG;
	pthread_cond_init(&barrier->breeched, NULL);
	pthread_mutex_init(&barrier->lock, NULL);

	return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *barrier) {
	D(bug("%s(%8lx)\n", __FUNCTION__, barrier));

	if (barrier == NULL)
		return EINVAL;

	if (pthread_mutex_trylock(&barrier->lock) != 0)
		return EBUSY;

	if (barrier->total_height > PTHREAD_BARRIER_FLAG) {
		pthread_mutex_unlock(&barrier->lock);
		return EBUSY;
	}

	pthread_mutex_unlock(&barrier->lock);

	if (pthread_cond_destroy(&barrier->breeched) != 0)
		return EBUSY;

	pthread_mutex_destroy(&barrier->lock);
	barrier->curr_height = barrier->total_height = 0;

	return 0;
}

int pthread_barrier_wait(pthread_barrier_t *barrier) {
	D(bug("%s(%8lx)\n", __FUNCTION__, barrier));

	if (barrier == NULL)
		return EINVAL;

	pthread_mutex_lock(&barrier->lock);

	// wait until everyone exits the barrier
	while (barrier->total_height > PTHREAD_BARRIER_FLAG)
		pthread_cond_wait(&barrier->breeched, &barrier->lock);

	// are we the first to enter?
	if (barrier->total_height == PTHREAD_BARRIER_FLAG)
		barrier->total_height = 0;

	barrier->total_height++;

	if (barrier->total_height == barrier->curr_height) {
		barrier->total_height += PTHREAD_BARRIER_FLAG - 1;
		pthread_cond_broadcast(&barrier->breeched);

		pthread_mutex_unlock(&barrier->lock);

		return PTHREAD_BARRIER_SERIAL_THREAD;
	} else {
		// wait until enough threads enter the barrier
		while (barrier->total_height < PTHREAD_BARRIER_FLAG)
			pthread_cond_wait(&barrier->breeched, &barrier->lock);

		barrier->total_height--;

		// get entering threads to wake up
		if (barrier->total_height == PTHREAD_BARRIER_FLAG)
			pthread_cond_broadcast(&barrier->breeched);

		pthread_mutex_unlock(&barrier->lock);

		return 0;
	}
}

