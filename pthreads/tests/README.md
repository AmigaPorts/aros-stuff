# pthread regression programs

These programs come from the OpenAmigaGCC pthread fixes. Build them with a
toolchain containing the updated libpthread, then run them on AmigaOS; a host
process or `qemu-m68k` user mode cannot emulate Exec tasks and signals.

```sh
m68k-amigaos-gcc -mcrt=nix20 -m68020 -O2 -Wall -o joinsig joinsig.c -lpthread
m68k-amigaos-gcc -mcrt=nix20 -m68020 -O2 -Wall -o names names.c -lpthread
m68k-amigaos-g++ -mcrt=nix20 -m68020 -m68881 -O2 -std=gnu++23 \
  -o t3race t3race.cpp -lpthread -latomic -lstdc++
m68k-amigaos-g++ -mcrt=nix20 -m68020 -m68881 -O2 -std=gnu++23 \
  -o t3w_warm t3w_warm.cpp -lpthread -latomic -lstdc++
```

`joinsig` must finish with `joinsig OK`, `names` with `names OK`. The C++ programs must finish with
`t3 OK` and `t3w OK`; repeat them to exercise the thread table growth race.
Use a libnix crt: the default newlib crt does not provide `gettimeofday()`
for libpthread. `make -f Makefile.gcc6 check` builds the tests for each
multilib; C++ builds are skipped when libstdc++ lacks thread support.
It also links `gthr_link.c` and `create_link.c` with libnix and newlib and
fails if a program that only makes libstdc++'s pthread calls links
`pthread_create`, or one that creates a thread links `snprintf`: these
checks need no Amiga.
The C++ tests are GPL-3.0-or-later, while `joinsig.c` follows the pthreads
zlib licence as stated in its source header.

## Condition deadlines

`make -f Makefile.gcc6 check` also builds `condtime` for each multilib.
Run it on AmigaOS and require `condtime OK` with return code zero. It checks
an absolute deadline crossing a second, an expired deadline, invalid
nanoseconds/relative seconds and a valid relative timeout. An error-checking
mutex verifies that the wait returns with ownership restored.

A watchdog signals the condition after two seconds. With the old unsigned
microsecond subtraction, the first wait returns because of that signal
instead of its deadline, so the regression fails without an indefinite
hang. This validates native timer.device behavior; linking on the host does
not execute the test.

## TLS key allocation and table relocation

`make -f Makefile.gcc6 check` also builds `tlskeys-0`, `tlskeys-1` and
`tlskeys-2` for each multilib. Run each on AmigaOS; all must print
`tlskeys OK` and return zero. Modes exercise allocation of all keys,
set-specific during table growth, and deletion during table growth.

The link-time realloc wrapper affects only this test's TLS table. It
supplies nonzero fresh storage or moves the table and quarantines the old
allocation. The concurrency tests hold the real TLS semaphore and observe
the worker in its Exec waiter queue before triggering growth. This makes
the interleaving deterministic; it does not measure its frequency with an
ordinary allocator. The test includes the internal header intentionally.

For diagnostic comparison with an older archive, compile with
`-DEXPECT_BROKEN=1`; zero exit then means the expected defect was reproduced,
not that the library passed. The ordinary `check` targets expect the fixes.
