/* Deterministic TLS key allocation and relocation regressions.
 *
 * The allocator wrapper returns legal nonzero new storage, or relocates
 * the TLS table while a worker is blocked on its real semaphore. Retired
 * storage is quarantined so a stale read becomes an API error, not a crash.
 * This is library-internal instrumentation, not an application example.
 * Distributed under the pthread library's zlib license. */
#define _DEFAULT_SOURCE 1
#include "../pthread_intern.h"
#include <exec/semaphores.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef KEY_TEST_MODE
#define KEY_TEST_MODE 0 /* 0: uninitialized slots, 1: set race, 2: delete race */
#endif
#ifndef EXPECT_BROKEN
#define EXPECT_BROKEN 0
#endif

extern void* __real_realloc(void*, size_t);

static int instrument;
static unsigned relocations;
static void* retired[PTHREAD_KEYS_MAX];
static unsigned retiredCount;
static pthread_key_t workerKey;
static volatile int workerError;

void* __wrap_realloc(void* previous, size_t bytes)
{
    unsigned oldCount = __pthread_numTlskeys;
    unsigned newCount = oldCount * 2 + 2;
    if (newCount > PTHREAD_KEYS_MAX) newCount = PTHREAD_KEYS_MAX;
    if (!instrument || previous != __pthread_tlskeys
        || bytes != newCount * sizeof(TLSKey))
        return __real_realloc(previous, bytes);
    TLSKey* next = malloc(bytes);
    if (!next) return NULL;
    if (previous) memcpy(next, previous, oldCount * sizeof(*next));
    memset(next + oldCount, 0, (newCount - oldCount) * sizeof(*next));
    if (KEY_TEST_MODE == 0) {
        /* Fresh realloc bytes have unspecified contents. Nonzero used bits
         * with null destructor pointers are legal allocator output. */
        for (unsigned i = oldCount; i < newCount; ++i) next[i].used = 1;
    }
    if (previous) {
        memset(previous, 0, oldCount * sizeof(*next));
        retired[retiredCount++] = previous;
        ++relocations;
    }
    return next;
}

static void* worker(void* unused)
{
    (void)unused;
    if (KEY_TEST_MODE == 1) {
        workerError = pthread_setspecific(workerKey, (void*)(uintptr_t)0x12345678);
        if (!workerError && pthread_getspecific(workerKey) != (void*)(uintptr_t)0x12345678)
            workerError = EIO;
    } else
        workerError = pthread_key_delete(workerKey);
    return NULL;
}

static int waitUntilBlocked(void)
{
    /* Holding the semaphore pins the worker at the actual library's lock
     * acquisition. Observe its real Exec waiter queue, not a guessed delay. */
    for (unsigned tick = 0; tick < 250; ++tick) {
        Forbid();
        int waiting = __pthread_tls_sem.ss_WaitQueue.mlh_Head
            != (struct MinNode*)&__pthread_tls_sem.ss_WaitQueue.mlh_Tail;
        Permit();
        if (waiting) return 1;
        Delay(1);
    }
    return 0;
}

int main(void)
{
    pthread_key_t keys[PTHREAD_KEYS_MAX];
    unsigned count = 0;
    int observed = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("pthread key diagnostic mode=%d expected_broken=%d initial_capacity=%u\n",
        KEY_TEST_MODE, EXPECT_BROKEN, __pthread_numTlskeys);
    if (__pthread_numTlskeys) {
        puts("INCONCLUSIVE: other code allocated keys before this isolated test");
        return 20;
    }
    instrument = 1;
    if (KEY_TEST_MODE == 0) {
        int error = 0;
        while (count < PTHREAD_KEYS_MAX) {
            error = pthread_key_create(&keys[count], NULL);
            if (error) break;
            for (unsigned i = 0; i < count; ++i)
                if (keys[i] == keys[count]) {
                    puts("Unexpected duplicate key");
                    return 20;
                }
            ++count;
        }
        printf("Allocated %u of %u keys; final_error=%d table_capacity=%u relocations=%u\n",
            count, PTHREAD_KEYS_MAX, error, __pthread_numTlskeys, relocations);
        observed = count < PTHREAD_KEYS_MAX && error == EAGAIN;
        if (!observed && count != PTHREAD_KEYS_MAX) return 20;
    } else {
        /* Fill the first allocation so the next key must grow the table. */
        do {
            if (pthread_key_create(&keys[count++], NULL)) return 20;
        } while (count < __pthread_numTlskeys);
        workerKey = keys[0];
        pthread_t thread;
        ObtainSemaphore(&__pthread_tls_sem);
        if (pthread_create(&thread, NULL, worker, NULL)) {
            ReleaseSemaphore(&__pthread_tls_sem);
            return 20;
        }
        if (!waitUntilBlocked()) {
            ReleaseSemaphore(&__pthread_tls_sem);
            pthread_join(thread, NULL);
            puts("INCONCLUSIVE: worker did not enter the TLS semaphore queue");
            return 20;
        }
        unsigned before = relocations;
        int error = pthread_key_create(&keys[count], NULL);
        if (!error) ++count;
        int moved = relocations > before;
        ReleaseSemaphore(&__pthread_tls_sem);
        if (pthread_join(thread, NULL) || error || !moved) {
            puts("INCONCLUSIVE: could not force relocation while worker blocked");
            return 20;
        }
        printf("Worker blocked before relocation; table moved=%d; worker_error=%d\n", moved, workerError);
        observed = workerError == EINVAL;
        if (!observed && workerError) return 20;
    }
    instrument = 0;
    for (unsigned i = 0; i < count; ++i)
        if (!(KEY_TEST_MODE == 2 && !observed && keys[i] == workerKey))
            pthread_key_delete(keys[i]);
    for (unsigned i = 0; i < retiredCount; ++i) free(retired[i]);
    printf("TLS defect observed=%d expected=%d\n", observed, EXPECT_BROKEN);
    if (observed != EXPECT_BROKEN) return 20;
    puts(EXPECT_BROKEN ? "tlskeys expected baseline failure" : "tlskeys OK");
    return 0;
}
