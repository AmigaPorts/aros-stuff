# POSIX Threads for AROS and MorphOS

This library implements a subset of the POSIX Threads standard on top of the native Amiga APIs (SignalSemaphores, signals, processes, etc.). 

## Limitations

Due to underlying API limitations detached threads are not supported. 

## Benaphores

On m68k-amigaos, mutexes and condition variables are built on gcc's
`<benaphore.h>`, whose out-of-line half is in libgcc.  gcc 16.2 has both.
For an older gcc, libpthread builds its own copy from `benaphore/`:
`benaphore.h` from gcc's `gcc/config/m68k/` and the `amigaos-benaphore*`
files from `libgcc/config/m68k/` (AmigaPorts/gcc, branch amiga16.2), under
the zlib license by their author.  They differ from gcc's only in what gcc 6
and 13 need: `<stdbool.h>`, the regparm attribute repeated on the slow
paths' definitions, and no warning about `-Wdangling-pointer`, which they
don't know.  A change to one copy belongs in the other.

## License

The library is availabe under the zlib license.

## Website
[http://bszili.morphos.me](http://bszili.morphos.me)

## Acknowledgements

This library is not directly based on any existing one, but it was inspired by the following projects:

* [AROS' thread.library](http://aros.sourceforge.net/documentation/developers/autodocs/thread.php) by Rob Norris
* [Amiga SDL](http://aminet.net/package/dev/misc/SDL-Amiga) by Gabriele Greco
* [Pthreads-w32](https://sourceware.org/pthreads-win32) by Ross Johnson
* [winpthreads](http://locklessinc.com/articles/pthreads_on_windows) by Lockless Inc.
