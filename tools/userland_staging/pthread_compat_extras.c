/* Real global (pthread.c:122) - libpthread source outside pthread.c
 * itself (pthread_atfork.c:158, and prototypes_internal.h's own
 * extern declaration) reference it directly. Always 0 here: this
 * target's kernel-side pthread stand-in (bsd_pthread_stub.c)
 * unconditionally returns ENOTSUP for bsdthread_create, so no second
 * thread can ever actually start running - see
 * pthread_main_thread_bootstrap.c's own header comment. */
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
