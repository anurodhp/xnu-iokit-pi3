/* Real setlogmask(3): a plain per-process priority-mask store, same
 * real semantics real syslog(3) documents (maskpri == 0 is a pure
 * query -- returns the current mask without changing it; otherwise sets
 * the mask and returns the OLD value). This project has no real
 * syslogd/ASL-backed filtering to actually gate on here regardless
 * (same reasoning as asl_client_compat.c's own shim) -- real, honest
 * state tracking for any caller that stores/inspects the return value,
 * not a fabrication of filtering that doesn't happen.
 *
 * Previously attempted via real oldsyslog.o (Libc's own file, bundling
 * a lot of unrelated syslog machinery together) and reverted as a real
 * net loss (see build_launchd_core.sh's own header comment) -- this
 * standalone version avoids that whole dependency chain entirely, since
 * the real function itself is this small and self-contained. */

int setlogmask(int maskpri) {
	static int _logmask = 0xff; /* LOG_UPTO(LOG_DEBUG), real openlog(3)-documented default */
	int old = _logmask;
	if (maskpri != 0) {
		_logmask = maskpri;
	}
	return old;
}
