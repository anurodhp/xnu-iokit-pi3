/* __isthreaded: real Apple global, consulted by
 * third_party/Libc/gen/FreeBSD/readdir.c and friends to decide whether
 * locking is even worth doing. Starts at 0 (real Apple initial value,
 * matching a process that hasn't created a second thread yet) --
 * pthread_create() (pthread_main_thread_bootstrap.c) now flips it to 1
 * on real success, alongside the separate real `__is_threaded` global
 * (pthread_compat_extras.c) libpthread-side code consults.
 *
 * REAL BUG FOUND AND FIXED 2026-09-03 (DAR-227): this comment used to
 * claim 0 was the permanently-correct answer because "this target has no
 * libpthread port and is provably single-threaded" -- true when
 * bsdthread_create(2) still returned ENOTSUP, stale since Phase 7.5 step
 * 1 (2026-08-20). Apple's own pthread_create() flips BOTH __is_threaded
 * and __isthreaded when it spawns a real second thread -- this project's
 * port was only updating the former, so FreeBSD-derived Libc consumers
 * of __isthreaded (readdir.c and friends) kept taking their no-locking
 * fast path even in a genuinely multi-threaded process. Fixed at the
 * single real call site rather than here, since this is just the
 * definition -- see pthread_main_thread_bootstrap.c's pthread_create(). */
int __isthreaded = 0;
