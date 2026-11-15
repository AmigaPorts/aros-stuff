/* The condition variables of <benaphore.h> for AmigaOS.
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

   A copy of libgcc/config/m68k/amigaos-benaphore-cond.c from AmigaPorts/gcc
   (branch amiga16.2), for a gcc that has none; changed only so that
   gcc 6 and 13 build it, see ../README.md.  */

#include <exec/execbase.h>
#include <exec/tasks.h>
#include <proto/exec.h>

#include "amigaos-benaphore-int.h"

/* Condition variables: a FIFO of waiters on their own stacks, as the
   benaphore's.  The queue is only touched under Forbid(), and a waiter is
   signalled once, after it is taken off it.  */

void
__benaphore_cond_enqueue (benaphore_cond_t *c, struct __benaphore_cond_waiter *w)
{
  w->next = 0;
  w->task = SysBase->ThisTask;
  w->signalled = false;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wdangling-pointer"
  if (c->__tail)
    c->__tail->next = w;
  else
    c->__head = w;
  c->__tail = w;
#pragma GCC diagnostic pop
}

void
__benaphore_cond_dequeue (benaphore_cond_t *c, struct __benaphore_cond_waiter *w)
{
  struct __benaphore_cond_waiter **link, *prev = 0;

  for (link = &c->__head; *link; prev = *link, link = &(*link)->next)
    if (*link == w)
      {
	*link = w->next;
	if (c->__tail == w)
	  c->__tail = prev;
	return;
      }
}

static void
wake (benaphore_cond_t *c)
{
  struct __benaphore_cond_waiter *w = c->__head;

  c->__head = w->next;
  if (!c->__head)
    c->__tail = 0;
  w->signalled = true;
  Signal (w->task, SIGF_SINGLE);
}

void
__benaphore_cond_signal (benaphore_cond_t *c)
{
  Forbid ();
  if (c->__head)
    wake (c);
  Permit ();
}

void
__benaphore_cond_broadcast (benaphore_cond_t *c)
{
  Forbid ();
  while (c->__head)
    wake (c);
  Permit ();
}

/* The unlock can't miss a signal: it happens under the Forbid() in which
   we queue ourselves, and Wait() breaks it only once we sleep.  */
void
__benaphore_cond_wait (benaphore_cond_t *c, benaphore_t *b)
{
  struct __benaphore_cond_waiter w;

  Forbid ();
  __benaphore_cond_enqueue (c, &w);
  benaphore_unlock (b);
  while (!w.signalled)
    Wait (SIGF_SINGLE);
  Permit ();
  benaphore_lock (b);
}
