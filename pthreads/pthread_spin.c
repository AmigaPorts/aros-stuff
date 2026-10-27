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

#if defined __mc68000__
/* No CAS instruction on m68k */
static int __m68k_sync_val_compare_and_swap(int *v, int o, int n)
{
	volatile int * vv = (volatile int *)v;
	int old;

	// as GCC's builtin: the value that was there, so the swap was made when it equals o
	Disable();
	old = *vv;
	if (old == o)
		*vv = n;
	Enable();
	return old;
}
#undef __sync_val_compare_and_swap
#define __sync_val_compare_and_swap(v, o, n) __m68k_sync_val_compare_and_swap(v, o, n)

#undef __sync_lock_test_and_set
#endif


//
// Spinlock functions
//

int pthread_spin_init(pthread_spinlock_t *lock, int pshared) {
	D(bug("%s(%8lx, %ld)\n", __FUNCTION__, lock, pshared));

	if (lock == NULL)
		return EINVAL;

	*lock = 0;

	return 0;
}

int pthread_spin_destroy(pthread_spinlock_t *lock) {
	D(bug("%s(%8lx)\n", __FUNCTION__, lock));

	return 0;
}

int pthread_spin_lock(pthread_spinlock_t *lock) {
	D(bug("%s(%8lx)\n", __FUNCTION__, lock));

	if (lock == NULL)
		return EINVAL;

#ifdef __MORPHOS__
	{
	unsigned int cnt = 0;
	while (__sync_val_compare_and_swap((int *)lock, 0, 1) != 0)
	{
		asm volatile("" ::: "memory");
		if ((cnt++ & 255) == 0)
			sched_yield();
	}
	}
#else
	while (__sync_val_compare_and_swap((int*) lock, 0, 1) != 0)
		sched_yield(); // TODO: don't yield the CPU every iteration
					   // SBF: if yield is implemented correctly there's nothing else to do.
#endif

	return 0;
}

int pthread_spin_trylock(pthread_spinlock_t *lock) {
	D(bug("%s(%8lx)\n", __FUNCTION__, lock));

	if (lock == NULL)
		return EINVAL;

	if (__sync_val_compare_and_swap((int*) lock, 0, 1) != 0)
		return EBUSY;

	return 0;
}

int pthread_spin_unlock(pthread_spinlock_t *lock) {
	D(bug("%s(%8lx)\n", __FUNCTION__, lock));

	if (lock == NULL)
		return EINVAL;

	__sync_lock_release((int*) lock);

	return 0;
}

