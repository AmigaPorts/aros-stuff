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
// Thread functions
//

#ifdef USE_ASYNC_CANCEL
#ifdef __MORPHOS__
static ULONG CancelHandlerFunc(void);
static struct EmulLibEntry CancelHandler =
{
	TRAP_LIB, 0, (void (*)(void))CancelHandlerFunc
};
static ULONG CancelHandlerFunc(void)
{
	ULONG signals = (ULONG)REG_D0;
	APTR data = (APTR)REG_A1;
	struct ExecBase *SysBase = (struct ExecBase *)REG_A6;
#else
AROS_UFH3S(ULONG, CancelHandler,
	AROS_UFHA(ULONG, signals, D0),
	AROS_UFHA(APTR, data, A1),
	AROS_UFHA(struct ExecBase *, SysBase, A6))
{
    AROS_USERFUNC_INIT
#endif

	DB2(bug("%s(%ld, %ld, %ld)\n", __FUNCTION__, signals, data, SysBase));

	pthread_testcancel();

	return signals;
#ifdef __AROS__
    AROS_USERFUNC_EXIT
#endif
}
#endif

#ifdef __baserel__
// A base-relative program (-fbaserel, -fbaserel32, -resident, -resident32): the new
// process starts with no a4 of its own. pr_CIS carries the ThreadInfo (which never
// moves), and its a4 field holds the creator's a4, saved by pthread_create. That is
// right for a resident program too, whose data is per invocation (no _geta4 there).
// Nothing here reads a global until a4 is loaded; pr_CIS then gets the thread's id,
// as the body expects, and the body runs as a separate function.
static void StarterBody(void) __attribute__((noinline));
static void StarterFunc(void) {
	struct Process *proc = (struct Process *)(*(struct ExecBase **)4)->ThisTask;
	ThreadInfo *inf = (ThreadInfo *)proc->pr_CIS;
	__asm__ volatile ("movea.l %0,%%a4" : : "a" (inf->a4) : "memory");
	proc->pr_CIS = (BPTR)inf->self;
	StarterBody();
}
static void StarterBody(void) {
#else
static void StarterFunc(void) {
#endif
	ThreadInfo *inf;
	void *ret;
	pthread_t pid;
	int i, j;
	int foundkey = TRUE;
	struct Task *joiner;
	ULONG joinmask;
#ifdef USE_ASYNC_CANCEL
	APTR oldexcept;
#endif

#ifdef __AMIGA__
	struct Process *proc = (struct Process*) SysBase->ThisTask;
	pid = (pthread_t) proc->pr_CIS;
	proc->pr_CIS = 0;
#else
	inf = (pthread_t)FindTask(NULL)->tc_UserData;
#endif

	DB2(bug("%s(%ld)\n", __FUNCTION__, pid));

	ObtainSemaphore(&thread_sem);
	inf = GetThreadInfo(pid);
	inf->started = 1;
	// trim the name
	//inf->task->tc_Node.ln_Name[inf->oldlen];

	// we have to set the priority here to avoid race conditions
	SetTaskPri(inf->task, inf->attr.param.sched_priority);

#ifdef USE_ASYNC_CANCEL
	// set the exception handler for async cancellation
	oldexcept = inf->task->tc_ExceptCode;
#ifdef __AROS__
	inf->task->tc_ExceptCode = &AROS_ASMSYMNAME(CancelHandler);
#else
	inf->task->tc_ExceptCode = &CancelHandler;
#endif
	SetExcept(SIGBREAKF_CTRL_C, SIGBREAKF_CTRL_C);
#endif

	// set a jump point for pthread_exit
	if (!setjmp(inf->jmp)) {
		void* (*start)(void*) = inf->start;
		void *arg = inf->arg;
		ReleaseSemaphore(&thread_sem);

		DB2(bug("stack soll %ld, ist %ld\n", inf->attr.stacksize, (unsigned)proc->pr_Task.tc_SPUpper - (unsigned)proc->pr_Task.tc_SPLower));
		// custom stack requires special handling
		if (inf->attr.stackaddr != NULL && inf->attr.stacksize > 0) {
#ifdef __AMIGA__
			struct StackSwapStruct stack;
			stack.stk_Lower = inf->attr.stackaddr;
			stack.stk_Upper = (ULONG)((char*) stack.stk_Lower + inf->attr.stacksize);
			stack.stk_Pointer = (APTR) stack.stk_Upper;

			StackSwap(&stack);

			ret = start(arg);
#else
			struct StackSwapArgs swapargs;
			struct StackSwapStruct stack;

			swapargs.Args[0] = (IPTR)inf->arg;
			stack.stk_Lower = inf->attr.stackaddr;
#ifdef __MORPHOS__
			stack.stk_Upper = (IPTR)stack.stk_Lower + inf->attr.stacksize;
			stack.stk_Pointer = (APTR)stack.stk_Upper;
#else
			stack.stk_Upper = (APTR)((IPTR)stack.stk_Lower + inf->attr.stacksize);
			stack.stk_Pointer = stack.stk_Upper;
#endif

			inf->ret = (void *)NewStackSwap(&stack, inf->start, &swapargs);
#endif
		} else {
			ret = start(arg);
		}
		Forbid();
	}
	// we end up here with Forbid() called before longjmp()
	Permit();

	D(bug("* %s(pid=%ld after long jump)\n", __FUNCTION__, pid));


	// destroy all non-NULL TLS key values
	// since the destructors can set the keys themselves, we have to do multiple iterations
	ObtainSemaphoreShared(&tls_sem);
	for (j = 0; foundkey && j < PTHREAD_DESTRUCTOR_ITERATIONS; j++) {
		foundkey = FALSE;
		for (i = 0; i < numTlskeys; i++) {
			if (_tlskeys[i].used && _tlskeys[i].destructor && inf->tlsvalues[i]) {
				void *oldvalue = inf->tlsvalues[i];
				inf->tlsvalues[i] = NULL;
				_tlskeys[i].destructor(oldvalue);
				foundkey = TRUE;
			}
		}
	}
	ReleaseSemaphore(&tls_sem);

	// inf may be invalid due to realloc
	ObtainSemaphore(&thread_sem);
	inf = GetThreadInfo(pid);
	inf->ret = ret;

#ifdef USE_ASYNC_CANCEL
	// remove the exception handler
	SetExcept(0, SIGBREAKF_CTRL_C);
	inf->task->tc_ExceptCode = oldexcept;
#endif

	DB2(bug("%s(releasing inf ptr %8lx)\n", __FUNCTION__, inf));
	// tell the parent thread that we are done
	Forbid();
	inf->finished = 1;
	if (inf->attr.detachstate == PTHREAD_CREATE_DETACHED) {
		// nobody will join it: the slot is free again (pthread_create takes a slot whose task is NULL),
		// and its parent, which may have ended, is not signalled
		inf->task = NULL;
		ReleaseSemaphore(&thread_sem);
		return;
	}
	inf->task = (struct Task *)-1;
	// only a joiner is told, on the signal it waits for (normally one of its own): the
	// parent's Ctrl-F is the program's. Read under thread_sem; Forbid() holds until this
	// process has gone, so the joiner can't run (or free its signal) before it is sent.
	joiner = inf->joiner;
	joinmask = inf->joinmask;
	ReleaseSemaphore(&thread_sem);
	if (joiner)
		Signal(joiner, joinmask);
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void* (*start)(void*), void *arg) {
	ThreadInfo *inf;
	char name[NAMELEN];
	size_t oldlen;
	pthread_t threadnew;

	D(bug("%s(%8lx, %8lx, %8lx, %8lx)\n", __FUNCTION__, thread, attr, start, arg));

	if (thread == NULL || start == NULL)
		return EINVAL;

	ObtainSemaphore(&thread_sem);

	// from now on, the exit code has threads to join
	__pthread_join_hook = pthread_join;

	// grab an empty thread slot
	threadnew = GetThreadId(NULL);
	if (threadnew == PTHREAD_THREADS_MAX) {
		ReleaseSemaphore(&thread_sem);
		return EAGAIN;
	}

	if (threadnew == numThreads) {
		// resize the thread's storage.
		unsigned n = numThreads + numThreads + 2;

		D(bug("** %s(resize to %ld)\n", __FUNCTION__, n));

		if (n > PTHREAD_THREADS_MAX)
			n = PTHREAD_THREADS_MAX;

		if (n != numThreads) {
			unsigned i;
			for (i = numThreads; i < n; i++) {
				ThreadInfo *t = (ThreadInfo*) calloc(1, sizeof(ThreadInfo));
				if (!t) {
					numThreads = i;
					ReleaseSemaphore(&thread_sem);
					return EAGAIN;
				}
				_threads[i] = t;
			}
			numThreads = n;
		}
	}

	// prepare the ThreadInfo structure
	inf = GetThreadInfo(threadnew);
	memset(inf, 0, sizeof(ThreadInfo));
	D(bug("new thread=%ld, inf=%8lx\n", threadnew, inf));
	inf->start = start;
	inf->arg = arg;
#ifdef __AMIGA__
	inf->parent = SysBase->ThisTask;
#else
	inf->parent = FindTask(NULL);
#endif
	if (attr)
		inf->attr = *attr;
	else
		pthread_attr_init(&inf->attr);
	NEWLIST((struct List* )&inf->cleanup);
	inf->cancelstate = PTHREAD_CANCEL_ENABLE;
	inf->canceltype = PTHREAD_CANCEL_DEFERRED;

	// let's trick CreateNewProc into allocating a larger buffer for the name
	snprintf(name, sizeof(name), "pthread thread #%ld", (long)threadnew);
	oldlen = strlen(name);
	memset(name + oldlen, ' ', sizeof(name) - oldlen - 1);
	name[sizeof(name) - 1] = '\0';

#ifdef __baserel__
	{
		register void *a4 __asm__("a4");
		__asm__ ("" : "=r" (a4));	// a4 as it is now, in the creating thread
		inf->a4 = a4;
		inf->self = threadnew;
	}
#endif
	// start the child thread
	inf->task = (struct Task*) CreateNewProcTags(NP_Entry, (IPTR) StarterFunc,
#ifdef __MORPHOS__
		NP_CodeType, CODETYPE_PPC,
		(inf->attr.stackaddr == NULL && inf->attr.stacksize > 0) ? NP_PPCStackSize : TAG_IGNORE, inf->attr.stacksize,
		inf->attr.stacksize68k > 0 ? NP_StackSize : TAG_IGNORE, inf->attr.stacksize68k,
#else
			(inf->attr.stackaddr == NULL && inf->attr.stacksize > 0) ? NP_StackSize : TAG_IGNORE, inf->attr.stacksize,
#endif
#if defined(__AMIGA__) && defined(__baserel__)
			NP_Input, (IPTR) inf,
#elif defined(__AMIGA__)
			NP_Input, (IPTR) threadnew,
#else
		NP_UserData, threadnew,
#endif
			NP_Name, (IPTR) name, TAG_DONE);

	{
		struct Task *started = inf->task;
		if (!started)
			inf->parent = NULL;
		ReleaseSemaphore(&thread_sem);

		D(bug("new thread=%ld, task=%8lx\n", threadnew, started));

		if (!started)
			return EAGAIN;
	}

	*thread = threadnew;

	return 0;
}

int pthread_detach(pthread_t thread) {
	ThreadInfo *inf;

	D(bug("%s(%ld)\n", __FUNCTION__, thread));

	ObtainSemaphore(&thread_sem);
	inf = GetThreadInfo(thread);
	if (inf == NULL || !inf->task) {
		ReleaseSemaphore(&thread_sem);
		return ESRCH;
	}
	if (inf->attr.detachstate == PTHREAD_CREATE_DETACHED || inf->joiner) {
		ReleaseSemaphore(&thread_sem);
		return EINVAL;
	}
	inf->attr.detachstate = PTHREAD_CREATE_DETACHED;
	if (inf->finished)
		inf->task = NULL;	// it has ended already: its slot is free now
	ReleaseSemaphore(&thread_sem);
	return 0;
}

int pthread_join(pthread_t thread, void **value_ptr) {
	ThreadInfo *inf;
	struct Task * task = SysBase->ThisTask;
	BYTE signal;

	pthread_testcancel();

	D(bug("%s(%ld, %8lx)\n", __FUNCTION__, thread, value_ptr));
	if (!thread || pthread_self() == thread)
		return EDEADLK;

	ObtainSemaphore(&thread_sem);
	inf = GetThreadInfo(thread);
	D(bug("%s(%ld, inf=%8lx, fini=%ld, detach=%ld, started=%ld)\n", __FUNCTION__, thread, inf, (int)(inf ? inf->finished : 0), (int)(inf ? inf->attr.detachstate : 0), inf->started));
	if (inf == NULL || !inf->task) {
		ReleaseSemaphore(&thread_sem);
		return ESRCH;
	}

	if (inf->attr.detachstate) {
		ReleaseSemaphore(&thread_sem);
		return EINVAL;
	}
	if (inf->joiner) {
		ReleaseSemaphore(&thread_sem);
		return EINVAL;
	}

	// Use a private signal when possible. If none is free, poll completion without
	// touching the application's Ctrl-F. The child and joiner synchronize through
	// thread_sem, so completion cannot be missed between the test and the wait.
	signal = inf->finished ? -1 : AllocSignal(-1);
	if (signal != -1)
		SetSignal(0, 1UL << signal);
	inf->joiner = task;
	inf->joinmask = signal == -1 ? 0 : 1UL << signal;
	while (!inf->started || !inf->finished) {
		ReleaseSemaphore(&thread_sem);
		if (signal == -1)
			Delay(1);
		else
			Wait(1UL << signal);
		ObtainSemaphore(&thread_sem);
		inf = GetThreadInfo(thread);
	}
	// the thread has ended. If it signalled while this task wasn't in Wait (it was taking
	// thread_sem), its private signal is still pending: clear it before reuse.
	if (signal != -1)
		SetSignal(0, 1UL << signal);
	inf->joiner = NULL;
	inf->joinmask = 0;

	D(bug("%s finished after wait (%ld, %ld)\n", __FUNCTION__, thread, (int)inf->finished));
	if (value_ptr)
		*value_ptr = inf->ret;

	inf->task = NULL;
	ReleaseSemaphore(&thread_sem);
	if (signal != -1)
		FreeSignal(signal);
	return 0;
}


//
// Scheduling functions
//

int pthread_setschedparam(pthread_t thread, int policy, const struct sched_param *param) {
	ThreadInfo *inf;

	D(bug("%s(%ld, %ld, %8lx)\n", __FUNCTION__, thread, policy, param));

	if (param == NULL)
		return EINVAL;

	ObtainSemaphore(&thread_sem);
	inf = GetThreadInfo(thread);

	if (inf)
		SetTaskPri(inf->task, param->sched_priority);
	ReleaseSemaphore(&thread_sem);

	if (inf == NULL)
		return ESRCH;

	return 0;
}

//
// Non-portable functions
//
int pthread_setname_np(pthread_t thread, const char *name) {
	ThreadInfo *inf;
	char *currentname;
	size_t namelen;

	D(bug("%s(%ld, %s)\n", __FUNCTION__, thread, name));

	if (name == NULL)
		return ERANGE;

	ObtainSemaphore(&thread_sem);
	inf = GetThreadInfo(thread);

	if (inf != NULL) {
		currentname = inf->task->tc_Node.ln_Name;

		if (inf->parent == NULL)
			namelen = strlen(currentname) + 1;
		else
			namelen = NAMELEN;

		if (strlen(name) + 1 > namelen)
			inf = NULL;
		else
			strncpy(currentname, name, namelen);
	}
	ReleaseSemaphore(&thread_sem);
	if (inf == NULL)
		return ERANGE;

	return 0;
}

int pthread_getname_np(pthread_t thread, char *name, size_t len) {
	ThreadInfo *inf;
	char *currentname;

	D(bug("%s(%ld, %s, %ld)\n", __FUNCTION__, thread, name, len));

	if (name == NULL || len == 0)
		return ERANGE;

	ObtainSemaphore(&thread_sem);
	inf = GetThreadInfo(thread);

	if (inf) {
		currentname = inf->task->tc_Node.ln_Name;

		if (strlen(currentname) + 1 > len)
			inf = NULL;
		else
			// length check passed - strcpy is ok.
			strcpy(name, currentname);
	}
	ReleaseSemaphore(&thread_sem);
	if (inf == NULL)
		return ERANGE;

	return 0;
}


//
// Signalling
//

int pthread_kill(pthread_t thread, int sig) {
	D(bug("%s(%ld, %ld) not implemented\n", __FUNCTION__, thread, sig));

	return EINVAL;
}

