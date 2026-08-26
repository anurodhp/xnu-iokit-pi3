/* Curated body for the real, unmodified third_party/Libc/os/assumes.h
 * (used directly by build_libxpc.sh as of 2026-08-26 -- this project's
 * OWN earlier hand-written 38-line os/assumes.h stub, previously at
 * launchd_compat_headers/os/assumes.h, was written on the mistaken
 * premise that real Apple's header was "missing entirely" from this
 * checkout; it was actually sitting at third_party/Libc/os/assumes.h the
 * whole time (966 real lines across assumes.h+assumes.c), just never
 * found. Deleted in favor of the real header -- see that commit's log
 * for the fuller writeup.
 *
 * This file provides the 7 real extern functions the real header's
 * macros call in its default (non-`OS_CRASH_ENABLE_EXPERIMENTAL_LIBTRACE`)
 * configuration -- confirmed by reading the header directly, that macro
 * is never defined by this project's build, so `_os_crash_fmt` (the
 * os_log-pack-based variadic path) is provably unreachable and
 * deliberately NOT implemented here.
 *
 * Curated rather than compiling the real assumes.c: that file's real
 * `_os_assert_log_impl`/`_os_assumes_log_impl` decode the failing
 * assertion's `code` via `_simple_asl_msg_new()`/`dladdr()`-based
 * symbolication into a real Apple System Log (ASL) message -- a real,
 * working Darwin syslog transport this project has never ported (no
 * syslogd exists here). The bodies below preserve the REAL, documented
 * semantics real Apple's own header promises callers (fatal assert
 * aborts the process; non-fatal assumes logs and continues) using a
 * plain fprintf(stderr, ...)-plus-abort() implementation instead of the
 * ASL/dladdr chain -- same "real interface, curated real-enough body"
 * precedent as chk_fail_compat.c/mach_msg_server_once_compat.c earlier
 * this session.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*os_crash_callback_t)(const char *);
os_crash_callback_t _os_crash_callback = NULL;

typedef void (*os_log_callout_t)(void *asl_message, void *context, char *buf);

void
_os_crash(const char *message)
{
	if (_os_crash_callback) {
		_os_crash_callback(message);
	}
	fprintf(stderr, "os_crash: %s\n", message ? message : "(no message)");
	abort();
}

void
_os_assumes_log(uint64_t code)
{
	fprintf(stderr, "os_assumes: non-fatal assertion failure (code=0x%llx)\n",
	    (unsigned long long)code);
}

char *
_os_assert_log(uint64_t code)
{
	char buf[64];
	snprintf(buf, sizeof(buf), "assertion failure (code=0x%llx)", (unsigned long long)code);
	return strdup(buf);
}

void
_os_assumes_log_ctx(os_log_callout_t callout, void *ctx, uint64_t code)
{
	(void)callout;
	(void)ctx;
	_os_assumes_log(code);
}

char *
_os_assert_log_ctx(os_log_callout_t callout, void *ctx, uint64_t code)
{
	(void)callout;
	(void)ctx;
	return _os_assert_log(code);
}

void
_os_avoid_tail_call(void)
{
}
