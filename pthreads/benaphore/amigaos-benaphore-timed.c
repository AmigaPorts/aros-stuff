/* The timed waits of <benaphore.h> for AmigaOS, apart so that only
   programs that use them link DOSBase and gettimeofday().
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

   A copy of libgcc/config/m68k/amigaos-benaphore-timed.c from AmigaPorts/gcc
   (branch amiga16.2), for a gcc that has none; changed only so that
   gcc 6 and 13 build it, see ../README.md.  */

/* A timed wait sleeps in the same queue as an untimed one, so the task
   that releases the lock or signals wakes it at once, and a timer request
   only ends the wait at the deadline.  */

#include <errno.h>
#include <sys/time.h>
#include <exec/execbase.h>
#include <exec/io.h>
#include <exec/tasks.h>
#include <devices/timer.h>
#include <dos/dosextens.h>
#include <proto/exec.h>

#include "amigaos-benaphore-int.h"

extern struct DosLibrary *DOSBase;

/* A request for the deadline, on the waiting task's stack: each wait needs
   its own, since a task can have only one in flight per request.  A clone
   of dos.library's, which is already open, as libnix's sleep() and AROS's
   Delay() use it.  It replies on SIGF_SINGLE, which the task sleeps on
   anyway, so it needs no signal of its own and the wait loop sees either.  */
struct timer {
  struct MsgPort port;
  struct timerequest io;
};

/* 0 with the request ready to send, or ETIMEDOUT if the deadline has
   passed.  The deadline is on the clock of gettimeofday(), as libstdc++'s
   system_clock and pthreads' are.  Seconds and microseconds apart, so that
   no 64-bit division is needed.  */
static int
timer_init (struct timer *t, const benaphore_time_t *deadline)
{
  struct MsgPort *p = &t->port;
  struct timeval now;
  long long secs;
  long micro;

  gettimeofday (&now, 0);
  secs = deadline->__sec - now.tv_sec;
  micro = deadline->__nsec / 1000 - now.tv_usec;
  if (micro < 0)
    {
      micro += 1000000;
      secs--;
    }
  if (secs < 0 || (secs == 0 && micro == 0))
    return ETIMEDOUT;
  /* tr_time's seconds are 32 bits: wait no more than 68 years.  */
  if (secs > 0x7fffffff)
    secs = 0x7fffffff;

  p->mp_Node.ln_Type = NT_MSGPORT;
  p->mp_Flags = PA_SIGNAL;
  p->mp_SigBit = SIGB_SINGLE;
  p->mp_SigTask = SysBase->ThisTask;
  p->mp_MsgList.lh_Head = (struct Node *) &p->mp_MsgList.lh_Tail;
  p->mp_MsgList.lh_Tail = 0;
  p->mp_MsgList.lh_TailPred = (struct Node *) &p->mp_MsgList.lh_Head;

  t->io = *DOSBase->dl_TimeReq;
  t->io.tr_node.io_Message.mn_Node.ln_Type = NT_REPLYMSG;
  t->io.tr_node.io_Message.mn_ReplyPort = p;
  t->io.tr_node.io_Command = TR_ADDREQUEST;
  t->io.tr_node.io_Flags = 0;
  t->io.tr_time.tv_secs = secs;
  t->io.tr_time.tv_micro = micro;
  return 0;
}

/* After the wait, whatever ended it.  Then no reply can come any more, and
   one that came after the last Wait() mustn't wake a later one early.  */
static void
timer_stop (struct timer *t)
{
  if (!CheckIO (&t->io.tr_node))
    AbortIO (&t->io.tr_node);
  WaitIO (&t->io.tr_node);
  SetSignal (0, SIGF_SINGLE);
}

/* Take w off the benaphore's circular queue, whose last sleeper the
   longword points to, keeping the lock bit as it is.  */
static void
dequeue (benaphore_t *b, struct __benaphore_waiter *w)
{
  unsigned long locked = (unsigned long) b->__last & __BENAPHORE_LOCKED;
  struct __benaphore_waiter *l = __benaphore_last (b), *p = l;

  while (p->next != w)
    p = p->next;
  if (p == w)
    l = 0;
  else
    {
      p->next = w->next;
      if (l == w)
	l = p;
    }
  b->__last = (struct __benaphore_waiter *) ((unsigned long) l | locked);
}

/* As __benaphore_lock_slow(), until the deadline.  */
int
__benaphore_lock_timed (benaphore_t *b, const benaphore_time_t *deadline)
{
  struct __benaphore_waiter w, *l;
  struct timer t;
  int err;

  if ((err = timer_init (&t, deadline)))
    return err;
  w.task = SysBase->ThisTask;
  w.granted = false;

  Forbid ();
  if (benaphore_trylock (b))
    {
      Permit ();
      return 0;
    }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wdangling-pointer"
  l = __benaphore_last (b);
  if (l)
    {
      w.next = l->next;
      l->next = &w;
    }
  else
    w.next = &w;
  b->__last = (struct __benaphore_waiter *)
    ((unsigned long) &w | __BENAPHORE_LOCKED);
#pragma GCC diagnostic pop
  SendIO (&t.io.tr_node);
  while (!w.granted && !CheckIO (&t.io.tr_node))
    Wait (SIGF_SINGLE);
  if (!w.granted)
    {
      dequeue (b, &w);
      err = ETIMEDOUT;
    }
  Permit ();

  timer_stop (&t);
  return err;
}

/* As __benaphore_cond_wait(), until the deadline.  The lock is taken again
   either way, without a deadline, as POSIX says.  */
int
__benaphore_cond_timedwait (benaphore_cond_t *c, benaphore_t *b,
			    const benaphore_time_t *deadline)
{
  struct __benaphore_cond_waiter w;
  struct timer t;
  int err;

  if ((err = timer_init (&t, deadline)))
    return err;

  Forbid ();
  __benaphore_cond_enqueue (c, &w);
  benaphore_unlock (b);
  SendIO (&t.io.tr_node);
  while (!w.signalled && !CheckIO (&t.io.tr_node))
    Wait (SIGF_SINGLE);
  if (!w.signalled)
    {
      __benaphore_cond_dequeue (c, &w);
      err = ETIMEDOUT;
    }
  Permit ();

  timer_stop (&t);
  benaphore_lock (b);
  return err;
}
