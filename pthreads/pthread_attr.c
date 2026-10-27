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
// Thread attribute functions
//

int pthread_attr_init(pthread_attr_t *attr) {
	struct Task *task;

	D(bug("%s(%8lx)\n", __FUNCTION__, attr));

	if (attr == NULL)
		return EINVAL;

	memset(attr, 0, sizeof(pthread_attr_t));
	// inherit the priority and stack size of the parent thread
#ifdef __AMIGA__
	task = SysBase->ThisTask;
#else
	task = FindTask(NULL);
#endif
	attr->param.sched_priority = task->tc_Node.ln_Pri;
#ifdef __MORPHOS__
	NewGetTaskAttrs(task, &attr->stacksize68k, sizeof(attr->stacksize68k), TASKINFOTYPE_STACKSIZE_M68K, TAG_DONE);
	NewGetTaskAttrs(task, &attr->stacksize, sizeof(attr->stacksize), TASKINFOTYPE_STACKSIZE, TAG_DONE);
#else
	attr->stacksize = (UBYTE*) task->tc_SPUpper - (UBYTE*) task->tc_SPLower;
#endif
	D(bug("%s(%8lx, detach=%ld)\n", __FUNCTION__, attr, (int)attr->detachstate));

	return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr) {
	D(bug("%s(%8lx)\n", __FUNCTION__, attr));

	if (attr == NULL)
		return EINVAL;

	memset(attr, 0, sizeof(pthread_attr_t));

	return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate) {
	D(bug("%s(%8lx, %8lx)\n", __FUNCTION__, attr, detachstate));

	if (attr == NULL)
		return EINVAL;

	if (detachstate != NULL)
		*detachstate = attr->detachstate;

	return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate) {
	D(bug("%s(%8lx, %ld)\n", __FUNCTION__, attr, detachstate));

	if (attr == NULL || detachstate != PTHREAD_CREATE_JOINABLE)
		return EINVAL;

	attr->detachstate = detachstate;

	return 0;
}

int pthread_attr_getstack(const pthread_attr_t *attr, void **stackaddr, size_t *stacksize) {
	D(bug("%s(%8lx, %8lx, %8lx)\n", __FUNCTION__, attr, stackaddr, stacksize));

	if (attr == NULL)
		return EINVAL;

	if (stackaddr != NULL)
		*stackaddr = attr->stackaddr;

	if (stacksize != NULL)
		*stacksize = attr->stacksize;

	return 0;
}

int pthread_attr_setstack(pthread_attr_t *attr, void *stackaddr, size_t stacksize) {
	D(bug("%s(%8lx, %8lx, %ld)\n", __FUNCTION__, attr, stackaddr, stacksize));

	if (attr == NULL || (stackaddr != NULL && stacksize == 0))
		return EINVAL;

	attr->stackaddr = stackaddr;
	attr->stacksize = stacksize;

	return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize) {
	D(bug("%s(%8lx, %8lx)\n", __FUNCTION__, attr, stacksize));

	return pthread_attr_getstack(attr, NULL, stacksize);
}

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize) {
	D(bug("%s(%8lx, %ld)\n", __FUNCTION__, attr, stacksize));

	return pthread_attr_setstack(attr, NULL, stacksize);
}

int pthread_attr_getschedparam(const pthread_attr_t *attr, struct sched_param *param) {
	D(bug("%s(%8lx, %8lx)\n", __FUNCTION__, attr, param));

	if (attr == NULL)
		return EINVAL;

	if (param != NULL)
		*param = attr->param;

	return 0;
}

int pthread_attr_setschedparam(pthread_attr_t *attr, const struct sched_param *param) {
	D(bug("%s(%8lx, %8lx)\n", __FUNCTION__, attr, param));

	if (attr == NULL || param == NULL)
		return EINVAL;

	attr->param = *param;

	return 0;
}

