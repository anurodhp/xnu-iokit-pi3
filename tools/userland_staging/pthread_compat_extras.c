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

/* Real call site: pthread_tsd.c's rare destructor-error diagnostic path
 * (dladdr-based "which library owns this leaked TSD destructor"
 * logging). Real Apple implementation (libplatform/src/simple/asl.c) is
 * a full ASL (Apple System Log) client - genuinely pointless to port
 * here regardless of fidelity: this project has no syslogd/ASL daemon
 * anywhere in its process model (no launchd, see project memory), so
 * there is no real destination for this log message to reach even with
 * a byte-perfect port. Documented no-op, same reasoning already
 * established for os/assumes.h's stand-in (build_libc_locale.sh). */
void _simple_asl_log(int level, const char *label, const char *msg) {
	(void)level;
	(void)label;
	(void)msg;
}
