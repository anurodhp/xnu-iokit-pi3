/* Real Apple libc global (declared extern in various FreeBSD-derived
 * Libc files, e.g. gen/FreeBSD/ttyname.c, stdlib/FreeBSD/atexit.c) used
 * as a performance optimization: skip locking when the process is known
 * to still be single-threaded. This project's curated libc stack never
 * defined it (no consumer needed it before atexit.c/glob.c). Real, safe
 * default: always report "threaded" -- this project's pthread support
 * is real (pthread_create works), so always taking the lock path is
 * always correct, just not maximally optimized for the single-thread
 * case. Never wrong, unlike guessing 0.
 */
int __isthreaded = 1;
