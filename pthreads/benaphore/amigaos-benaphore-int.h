/* What the amigaos-benaphore*.c files on m68k share.
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

   A copy of libgcc/config/m68k/amigaos-benaphore-int.h from AmigaPorts/gcc
   (branch amiga16.2), for a gcc that has none; changed only so that
   gcc 6 and 13 build it, see ../README.md.  */

#include <stdbool.h>
#include "benaphore.h"

/* A task sleeping on a benaphore, on its own stack.  The benaphore's
   longword is the last sleeper's address with the lock in bit 0, and the
   last sleeper's next is the first.  Only touched under Forbid().  */
struct __benaphore_waiter {
	struct __benaphore_waiter *next;
	struct Task *task;
	bool granted;
};

#define __BENAPHORE_LOCKED 1UL

static inline struct __benaphore_waiter *__benaphore_last(benaphore_t *b)
{
	return (struct __benaphore_waiter *)
		((unsigned long) b->__last & ~__BENAPHORE_LOCKED);
}

/* A task waiting on a condition variable, on its own stack.  It is
   signalled once, after it is taken off the queue.  */
struct __benaphore_cond_waiter {
	struct __benaphore_cond_waiter *next;
	struct Task *task;
	bool signalled;
};

void __benaphore_cond_enqueue(benaphore_cond_t *c, struct __benaphore_cond_waiter *w);
void __benaphore_cond_dequeue(benaphore_cond_t *c, struct __benaphore_cond_waiter *w);
