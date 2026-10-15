/* joinsig: pthread_join and the program's own Ctrl-F.
 * pthread_join waits on a signal of its own; a Ctrl-F pending when it starts, or sent
 * to the program while it waits, is still pending when it returns; a thread that ends
 * sends nobody Ctrl-F; and with no signal free, the Ctrl-F fallback still waits for the
 * thread's real end and puts the program's Ctrl-F back. Prints PASS or FAIL per case
 * and "joinsig OK" (rc 0) or "joinsig FAILED" (rc 20).
 *
 *   m68k-amigaos-gcc -m68020 -O2 -Wall -o joinsig joinsig.c -lpthread
 *
 * Copyright (c) 2026 Dalsin Limited. zlib licence, as aros-stuff's pthreads. */
#include <pthread.h>
#include <stdio.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#define CTRL_F SIGBREAKF_CTRL_F
#define USER_SIGNALS 0xffff0000UL   /* the bits AllocSignal hands out */

static struct Task *main_task;
static volatile int ended;
static int fails;

static void check(int ok, const char *how, const char *what)
{
    printf("%s %s: %s\n", ok ? "PASS" : "FAIL", how, what);
    if (!ok) fails++;
}
static ULONG pending(void) { return SetSignal(0, 0); }

/* About half a second; with poke, it sends the main task Ctrl-F a fifth of a second in,
 * while the main task is in pthread_join. */
static void *worker(void *arg)
{
    int poke = (int)(long)arg;
    Delay(10);
    if (poke) Signal(main_task, CTRL_F);
    Delay(15);
    ended = 1;
    return (void *)42;
}
static void *quick(void *arg) { (void)arg; return (void *)7; }

/* One join of worker: pre, the program's Ctrl-F already pending; poke, one sent during it. */
static void join_case(const char *how, int pre, int poke)
{
    pthread_t t;
    void *ret = NULL;
    int rc;
    SetSignal(0, CTRL_F);
    ended = 0;
    if (pthread_create(&t, NULL, worker, (void *)(long)poke)) { check(0, how, "pthread_create"); return; }
    if (pre) SetSignal(CTRL_F, CTRL_F);
    rc = pthread_join(t, &ret);
    check(rc == 0 && ended && ret == (void *)42, how, "the join returns 0 once the thread has ended, with its value");
    if (pre || poke) check((pending() & CTRL_F) != 0, how, "the program's Ctrl-F is still pending after the join");
    else check(!(pending() & CTRL_F), how, "no Ctrl-F after the join");
    SetSignal(0, CTRL_F);
}

/* A thread that has ended before its join: no Ctrl-F from it, before or after the join. */
static void ended_case(const char *how)
{
    pthread_t t;
    void *ret = NULL;
    int quiet;
    SetSignal(0, CTRL_F);
    if (pthread_create(&t, NULL, quick, NULL)) { check(0, how, "pthread_create"); return; }
    Delay(10);
    quiet = !(pending() & CTRL_F);
    check(quiet && pthread_join(t, &ret) == 0 && ret == (void *)7 && !(pending() & CTRL_F), how,
          "a thread that ended before its join sends no Ctrl-F, and the join returns its value");
    SetSignal(0, CTRL_F);
}

static int free_signals(void)
{
    BYTE got[32];
    int n = 0, i;
    BYTE s;
    while (n < 32 && (s = AllocSignal(-1)) != -1) got[n++] = s;
    for (i = 0; i < n; i++) FreeSignal(got[i]);
    return n;
}

int main(void)
{
    BYTE held[32];
    int nheld = 0, before, i, ok = 1;
    BYTE s;
    main_task = FindTask(NULL);

    join_case("own signal", 0, 0);
    join_case("own signal", 1, 0);
    join_case("own signal", 0, 1);
    ended_case("own signal");

    before = free_signals();
    for (i = 0; i < 50 && ok; i++) {
        pthread_t t;
        void *ret = NULL;
        ok = !pthread_create(&t, NULL, quick, NULL) && !pthread_join(t, &ret) && ret == (void *)7;
    }
    check(ok && free_signals() == before && !(pending() & (USER_SIGNALS | CTRL_F)), "own signal",
          "50 joins: every signal given back, none left pending");

    while (nheld < 32 && (s = AllocSignal(-1)) != -1) held[nheld++] = s;   /* none free: the Ctrl-F fallback */
    printf("(%d signals held, none free)\n", nheld);
    join_case("no signal free", 0, 0);
    join_case("no signal free", 1, 0);
    join_case("no signal free", 0, 1);
    ended_case("no signal free");
    for (i = 0; i < nheld; i++) FreeSignal(held[i]);

    puts(fails ? "joinsig FAILED" : "joinsig OK");
    return fails ? 20 : 0;
}
