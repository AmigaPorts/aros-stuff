/*
	Copyright (C) 2015 Szilard Biro

	This software is provided 'as-is', without any express or implied
	warranty.	In no event will the authors be held liable for any damages
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

#include "semaphore.h"
#include "debug.h"

#ifndef EOVERFLOW
#define EOVERFLOW EINVAL
#endif

int sem_init(sem_t *sem, int pshared, unsigned int value)
{
	D(bug("%s(%p, %d, %u)\n", __FUNCTION__, sem, pshared, value));

	if (sem == NULL || value > (unsigned int)SEM_VALUE_MAX)
	{
		errno = EINVAL;
		return -1;
	}

	sem->value = value;
	sem->waiters_count = 0;
	pthread_mutex_init(&sem->lock, NULL);
	pthread_cond_init(&sem->count_nonzero, NULL);

	return 0;
}

int sem_destroy(sem_t *sem)
{
	D(bug("%s(%p)\n", __FUNCTION__, sem));

	if (sem == NULL)
	{
		errno = EINVAL;
		return -1;
	}

	if (pthread_mutex_trylock(&sem->lock) != 0)
	{
		errno = EBUSY;
		return -1;
	}

	pthread_mutex_unlock(&sem->lock);
	pthread_mutex_destroy(&sem->lock);
	pthread_cond_destroy(&sem->count_nonzero);
	sem->value = sem->waiters_count = 0;

	return 0;
}

int sem_trywait(sem_t *sem)
{
	int result = 0;

	D(bug("%s(%p)\n", __FUNCTION__, sem));

	if (sem == NULL)
	{
		errno = EINVAL;
		return -1;
	}

	pthread_mutex_lock(&sem->lock);

	if (sem->value > 0)
		sem->value--;
	else
		result = EAGAIN;

	pthread_mutex_unlock(&sem->lock);

	if (result != 0)
	{
		errno = result;
		return -1;
	}

	return 0;
}

int sem_timedwait(sem_t *sem, const struct timespec *abstime)
{
	int result = 0;

	D(bug("%s(%p, %p)\n", __FUNCTION__, sem, abstime));

	if (sem == NULL)
	{
		errno = EINVAL;
		return -1;
	}

	pthread_mutex_lock(&sem->lock);

	sem->waiters_count++;

	while (sem->value == 0)
		result = pthread_cond_timedwait(&sem->count_nonzero, &sem->lock, abstime);

	sem->waiters_count--;

	if (result == ETIMEDOUT)
	{
		pthread_mutex_unlock(&sem->lock);
		errno = ETIMEDOUT;
		return -1;
	}

	sem->value--;

	pthread_mutex_unlock(&sem->lock);

	return 0;
}

int sem_wait(sem_t *sem)
{
	D(bug("%s(%p)\n", __FUNCTION__, sem));

	return sem_timedwait(sem, NULL);
}

int sem_post(sem_t *sem)
{
	D(bug("%s(%p)\n", __FUNCTION__, sem));

	if (sem == NULL)
	{
		errno = EINVAL;
		return -1;
	}

	pthread_mutex_lock(&sem->lock);

	if (sem->value >= SEM_VALUE_MAX)
	{
		pthread_mutex_unlock(&sem->lock);
		errno = EOVERFLOW;
		return -1;
	}

	sem->value++;

	if (sem->waiters_count > 0)
		pthread_cond_signal(&sem->count_nonzero);

	pthread_mutex_unlock(&sem->lock);

	return 0;
}

int sem_getvalue(sem_t *sem, int *sval)
{
	D(bug("%s(%p)\n", __FUNCTION__, sem));

	if (sem == NULL || sval == NULL)
	{
		errno = EINVAL;
		return -1;
	}

	if (pthread_mutex_trylock(&sem->lock) == 0)
	{
		*sval = sem->value;
		pthread_mutex_unlock(&sem->lock);
	}
	else
	{
		*sval = 0;
	}

	return 0;
}