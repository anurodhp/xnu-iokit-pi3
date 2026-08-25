/* Real global (pthread.c:122) - libpthread source outside pthread.c
 * itself (pthread_atfork.c:158, and prototypes_internal.h's own
 * extern declaration) reference it directly. Starts at 0 (real Apple
 * initial value, matching a process that hasn't created a second
 * thread yet); pthread_create() (pthread_main_thread_bootstrap.c) now
 * flips it to 1 on real success, matching real pthread.c's own
 * behavior - this comment used to claim it stays permanently 0 because
 * "no second thread can ever actually start running", which was true
 * when the kernel-side bsdthread_create was still ENOTSUP but has been
 * stale since Phase 7.5 step 1 (2026-08-20) made it real; corrected
 * here rather than left to rot. */
int __is_threaded = 0;

/* _simple_asl_log: pthread_tsd.c's rare destructor-error diagnostic path
 * (dladdr-based "which library owns this leaked TSD destructor" logging)
 * calls this same real symbol libmalloc_dispatch.c already defines (real
 * stderr-redirect body, see that file's own header comment) -- NOT
 * redefined here. A duplicate no-op body used to live in this file too;
 * removed (real duplicate-symbol link error confirmed, not guessed) now
 * that any consumer of this file also always links libmalloc.a. */
