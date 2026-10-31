/* Absolute condition deadline and interval validation regressions.
 * A watchdog signals the condition after two seconds so the old unsigned
 * borrow bug fails instead of leaving this test asleep for a huge interval.
 * Distributed under the pthread library's zlib license. */
#define _DEFAULT_SOURCE 1
#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <sys/time.h>
#include <proto/dos.h>

static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t mutex;
static unsigned failures;

static void check(int ok, const char* description)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", description);
    if (!ok)
        ++failures;
}

static void* watchdog(void* unused)
{
    (void)unused;
    Delay(100);
    pthread_cond_signal(&condition);
    return NULL;
}

static int timeout_case(const struct timespec* deadline, const char* description)
{
    pthread_t thread;
    struct timeval before, after;
    unsigned previous = failures;
    if (pthread_create(&thread, NULL, watchdog, NULL))
        return 0;
    pthread_mutex_lock(&mutex);
    gettimeofday(&before, NULL);
    int result = pthread_cond_timedwait(&condition, &mutex, deadline);
    gettimeofday(&after, NULL);
    long elapsed = (after.tv_sec - before.tv_sec) * 1000000L
        + (long)after.tv_usec - (long)before.tv_usec;
    check(result == ETIMEDOUT, description);
    check(elapsed >= 0 && elapsed < 1500000L, "deadline is bounded");
    /* An error-checking mutex rejects unlock if wait did not reacquire it. */
    check(!pthread_mutex_unlock(&mutex), "mutex reacquired after timeout");
    check(!pthread_join(thread, NULL), "watchdog joined before next case");
    return previous == failures;
}

int main(void)
{
    struct timeval now;
    struct timespec deadline;
    pthread_mutexattr_t attributes;
    setvbuf(stdout, NULL, _IONBF, 0);
    pthread_mutexattr_init(&attributes);
    pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_ERRORCHECK);
    if (pthread_mutex_init(&mutex, &attributes))
        return 20;
    pthread_mutexattr_destroy(&attributes);

    /* Ensure a nonzero microsecond field before constructing the next
     * whole-second deadline, so normalization must borrow one second. */
    do {
        gettimeofday(&now, NULL);
        if (now.tv_usec >= 10000 && now.tv_usec <= 800000)
            break;
        Delay(1);
    } while (1);
    deadline.tv_sec = now.tv_sec + 1;
    deadline.tv_nsec = 0;
    if (!timeout_case(&deadline, "cross-second absolute timeout"))
        return 20;

    gettimeofday(&now, NULL);
    deadline.tv_sec = now.tv_sec - 1;
    deadline.tv_nsec = now.tv_usec * 1000L;
    if (!timeout_case(&deadline, "expired absolute timeout"))
        return 20;

    pthread_mutex_lock(&mutex);
    deadline.tv_nsec = -1;
    check(pthread_cond_timedwait(&condition, &mutex, &deadline) == EINVAL,
        "negative absolute nanoseconds rejected");
    deadline.tv_nsec = 1000000000L;
    check(pthread_cond_timedwait(&condition, &mutex, &deadline) == EINVAL,
        "one billion absolute nanoseconds rejected");
    deadline.tv_sec = -1;
    deadline.tv_nsec = 0;
    check(pthread_cond_timedwait_relative_np(&condition, &mutex, &deadline) == EINVAL,
        "negative relative seconds rejected");
    deadline.tv_sec = 0;
    deadline.tv_nsec = 1000000000L;
    check(pthread_cond_timedwait_relative_np(&condition, &mutex, &deadline) == EINVAL,
        "one billion relative nanoseconds rejected");
    deadline.tv_nsec = 20000000L;
    check(pthread_cond_timedwait_relative_np(&condition, &mutex, &deadline) == ETIMEDOUT,
        "valid relative timeout preserved");
    check(!pthread_mutex_unlock(&mutex), "mutex retained after invalid intervals");
    check(!pthread_cond_destroy(&condition), "condition destroyed");
    check(!pthread_mutex_destroy(&mutex), "mutex destroyed");
    printf("condtime %s (%u failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 20 : 0;
}
