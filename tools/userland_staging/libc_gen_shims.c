/* __isthreaded: real Apple global (normally flipped to 1 once
 * libpthread's bsdthread_create() has actually spawned a second thread),
 * consulted by third_party/Libc/gen/FreeBSD/readdir.c and friends to
 * decide whether locking is even worth doing. This target has no
 * libpthread port and is provably single-threaded (see
 * libc_stdio_shims.c's identical reasoning for the pthread mutex/once
 * no-ops) - 0 is the real, correct answer for this process, not a
 * placeholder, and it's also the more honest choice than always taking
 * the (no-op) locking path: it makes the real control-flow difference
 * visible in the source read, rather than hiding "there is no locking
 * here" behind a mutex implementation that always no-ops. */
int __isthreaded = 0;
