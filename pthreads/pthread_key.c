/*
 Copyright (C) 2014 Szilard Biro
 Copyright (C) 2018 Harry Sintonen
 Copyright (C) 2019/20 Stefan "Bebbo" Franke - AmigaOS 3 port / bug fixes

 Altered in 2026: initialize TLS slots and protect their lookup.

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
// Thread specific data functions
//

int pthread_key_create(pthread_key_t *key, void (*destructor)(void*)) {
	TLSKey *tls;
	int i;

	D(bug("%s(%ld, %ld)\n", __FUNCTION__, key, destructor));

	if (key == NULL)
		return EINVAL;

	ObtainSemaphore(&tls_sem);

	for (i = 0; i < numTlskeys; i++) {
		if (_tlskeys[i].used == FALSE)
			break;
	}

	if (i == PTHREAD_KEYS_MAX) {
		ReleaseSemaphore(&tls_sem);
		return EAGAIN;
	}

	if (i == numTlskeys) {
		// resize the thread's storage.
		unsigned n = numTlskeys + numTlskeys + 2;
		if (n > PTHREAD_KEYS_MAX)
			n = PTHREAD_KEYS_MAX;

		TLSKey *t = (TLSKey*) realloc(_tlskeys, n * sizeof(TLSKey));
		if (!t) {
			ReleaseSemaphore(&tls_sem);
			return EAGAIN;
		}

		memset(t + numTlskeys, 0, (n - numTlskeys) * sizeof(*t));
		_tlskeys = t;
		numTlskeys = n;
	}

	tls = &_tlskeys[i];
	tls->used = TRUE;
	tls->destructor = destructor;

	ReleaseSemaphore(&tls_sem);

	*key = i;

	return 0;
}

int pthread_key_delete(pthread_key_t key) {
	TLSKey *tls;

	D(bug("%s(%ld)\n", __FUNCTION__, key));

	ObtainSemaphore(&tls_sem);

	if (key >= numTlskeys) {
		ReleaseSemaphore(&tls_sem);
		return EINVAL;
	}

	tls = &_tlskeys[key];

	if (tls->used == FALSE) {
		ReleaseSemaphore(&tls_sem);
		return EINVAL;
	}

	tls->used = FALSE;
	tls->destructor = NULL;

	ReleaseSemaphore(&tls_sem);

	return 0;
}

int pthread_setspecific(pthread_key_t key, const void *value) {
	pthread_t thread;
	ThreadInfo *inf;
	TLSKey *tls;

	D(bug("%s(%ld)\n", __FUNCTION__, key));

	thread = pthread_self();
	ObtainSemaphoreShared(&tls_sem);

	if (key >= numTlskeys) {
		ReleaseSemaphore(&tls_sem);
		return EINVAL;
	}

	tls = &_tlskeys[key];

	if (tls->used == FALSE) {
		ReleaseSemaphore(&tls_sem);
		return EINVAL;
	}

	ReleaseSemaphore(&tls_sem);

	ObtainSemaphore(&thread_sem);
	inf = GetThreadInfo(thread);
	inf->tlsvalues[key] = (void*) value;
	ReleaseSemaphore(&thread_sem);

	return 0;
}

void* pthread_getspecific(pthread_key_t key) {
	pthread_t thread;
	ThreadInfo *inf;
	void *value = NULL;

	D(bug("%s(%ld)\n", __FUNCTION__, key));

	if (key >= PTHREAD_KEYS_MAX)
		return NULL;

	thread = pthread_self();

	ObtainSemaphore(&thread_sem);
	inf = GetThreadInfo(thread);
	value = inf->tlsvalues[key];
	ReleaseSemaphore(&thread_sem);

	return value;
}

//
