/* The out-of-line half of <benaphore.h> for AmigaOS.
   Copyright (C) 2026 Bernie Innocenti

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

   A copy of libgcc/config/m68k/amigaos-benaphore.c from AmigaPorts/gcc
   (branch amiga16.2), for a gcc that has none; changed only so that
   gcc 6 and 13 build it, see ../README.md.  */

/* The queue of sleeping tasks, see amigaos-benaphore-int.h.  Both slow
   paths run with the lock held, so they write the longword back with bit 0
   set.  */

#include <exec/execbase.h>
#include <exec/tasks.h>
#include <proto/exec.h>
#include "amigaos-benaphore-int.h"

static inline void set_last(benaphore_t *b, struct __benaphore_waiter *w)
{
	b->__last = (struct __benaphore_waiter *)
		((unsigned long) w | __BENAPHORE_LOCKED);
}

void __BENAPHORE_ABI __benaphore_lock_slow(benaphore_t *b)
{
	struct __benaphore_waiter w, *l;

	w.task = SysBase->ThisTask;
	w.granted = false;

	Forbid();
	if (!benaphore_trylock(b)) {
		/* w is on our stack, but out of the queue before we return: the
		   task that grants us the lock dequeues us first.  */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wdangling-pointer"
		l = __benaphore_last(b);
		if (l) {
			w.next = l->next;
			l->next = &w;
		} else
			w.next = &w;
		set_last(b, &w);
#pragma GCC diagnostic pop
		/* SIGF_SINGLE is what exec's semaphores wait on; a task can't be
		   in both at once.  Wait() breaks the Forbid() while it sleeps and
		   restores it, and a stray signal only goes round the loop.  */
		while (!w.granted)
			Wait(SIGF_SINGLE);
	}
	Permit();
}

/* After an unlock found a sleeper: take the lock on behalf of the first one
   and hand it over.  If another task took the lock in between, it wakes the
   sleeper when it unlocks.  */
void __BENAPHORE_ABI __benaphore_wake(benaphore_t *b)
{
	struct __benaphore_waiter *w, *l;

	Forbid();
	l = __benaphore_last(b);
	if (l && benaphore_trylock(b)) {
		w = l->next;
		if (w == l)
			set_last(b, 0);
		else
			l->next = w->next;
		w->granted = true;
		Signal(w->task, SIGF_SINGLE);
	}
	Permit();
}
