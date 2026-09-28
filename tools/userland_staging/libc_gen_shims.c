/* __isthreaded: consulted by FreeBSD-derived Libc code (gen/FreeBSD/
 * readdir.c, stdio's FLOCKFILE, ...) to decide whether to lock.
 *
 * DAR-416: always 1. That is Apple's own value: Libc compiles those
 * FreeBSD files with third_party/Libc/fbsdcompat/_fbsd_compat_.h, whose
 * line 38 is `#define __isthreaded 1`, so Apple always locks. This port
 * compiles some of them without that header, so they reference this
 * variable instead. It used to start at 0 and be set to 1 by this port's
 * hand-written pthread_create() (DAR-227); Apple's real pthread_create()
 * (now in libsystem_pthread.dylib) sets only its own __is_threaded, so
 * the 0 would have stayed forever and stdio/readdir would have stopped
 * locking in every multithreaded process. */
int __isthreaded = 1;
