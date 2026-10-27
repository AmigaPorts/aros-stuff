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
