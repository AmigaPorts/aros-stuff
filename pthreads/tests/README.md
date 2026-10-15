# pthread regression programs

These programs come from the OpenAmigaGCC pthread fixes. Build them with a
toolchain containing the updated libpthread, then run them on AmigaOS; a host
process or `qemu-m68k` user mode cannot emulate Exec tasks and signals.

```sh
m68k-amigaos-gcc -m68020 -O2 -Wall -o joinsig joinsig.c -lpthread
m68k-amigaos-g++ -m68020 -m68881 -O2 -std=gnu++23 \
  -o t3race t3race.cpp -lpthread -latomic -lstdc++
m68k-amigaos-g++ -m68020 -m68881 -O2 -std=gnu++23 \
  -o t3w_warm t3w_warm.cpp -lpthread -latomic -lstdc++
```

`joinsig` must finish with `joinsig OK`. The C++ programs must finish with
`t3 OK` and `t3w OK`; repeat them to exercise the thread table growth race.
The C++ tests are GPL-3.0-or-later, while `joinsig.c` follows the pthreads
zlib licence as stated in its source header.
