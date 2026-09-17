/* Real date(1) has two features gated behind actually SETTING the
 * system clock (`date <newdate>` as root, not the ordinary display/
 * format path this tier's own QEMU verification exercises):
 *
 * 1. netsettime() (real third_party/shell_cmds/date/netdate.c) --
 *    broadcasts the new time to other hosts via the ancient `timed`
 *    UDP protocol (getservbyname/socket/bind/connect/send/select, a
 *    real resolver+socket stack this project's Phase 6 networking
 *    hasn't started). `timed` itself has been dead/unshipped on every
 *    real macOS release for decades regardless -- this is real,
 *    vendored legacy-compat source Apple itself still carries, but
 *    date.c only calls it when neither -n was passed NOR does the
 *    caller want to skip network notification, i.e. only on an actual
 *    privileged clock-set. An honest "no time daemon reachable" failure
 *    (return nonzero) is the same real outcome this call already has on
 *    every modern real system, not a fabricated success. DAR-149
 *    (2026-09-17) re-triaged this and left it exactly as-is: this isn't
 *    an unimplemented gap, it's already the correct, permanent answer
 *    -- there is no real system left to make this succeed against.
 *
 * 2. pututxline() (real Libc/gen/utmpx-darwin.c, 1193 lines) -- records
 *    an OLD_TIME/NEW_TIME utmpx entry. DAR-149 (2026-09-17) RESOLVED
 *    this for real: date(1) now compiles the exact same real
 *    third_party/Libc/gen/NetBSD/utmpx.c + gen/utmpx-darwin.c pair
 *    build_login.sh already proved out for login.macho (DAR-164),
 *    backed by the same already-established honest ASL no-op compat
 *    shims (asl_client_compat.c/login_asl_extra_compat.c) -- see
 *    build_simple_cmds.sh's own "date)" cmd_cflags/extra_libs case
 *    comments for the full wiring. The real primary utmpx-file write
 *    (__pututxline() -> a real fopen("w+")/fwrite() of _PATH_UTMPX,
 *    /var/run/utmpx) needs no daemon at all; only the SECONDARY wtmp/
 *    lastlog trail (_utmpx_asl()) degrades to the project's existing
 *    honest ASL no-op (matches build_login.sh's own header finding: no
 *    bootstrap-namespace registrar to answer a real asl_open() lookup
 *    yet). This is what "worth re-triaging" in the ticket actually
 *    found -- not that syslogd unblocked it (it didn't; ASL stays a
 *    no-op regardless), but that login.macho had already independently
 *    solved the identical problem and date(1) had simply never been
 *    pointed at that solution.
 *
 * Neither item affects date(1)'s ordinary, overwhelmingly common
 * behavior at all: displaying/formatting the current time, -u (UTC),
 * -v (vary offsets), -j (don't set the clock), custom +format strings,
 * and TZ handling are all real, unmodified date.c/vary.c, untouched by
 * this file. */

#include <stddef.h>

/* DAR-164 (2026-09-01) UPDATE, further updated by DAR-149 (2026-09-17):
 * build_login.sh (and, as of DAR-149, build_simple_cmds.sh's own "date"
 * entry) compile the REAL third_party/Libc/gen/NetBSD/utmpx.c +
 * gen/utmpx-darwin.c pair (real pututxline(), backed by this project's
 * own honest ASL/bootstrap compat shims, see build_login.sh's own
 * header for the full writeup), so this stand-in would be a genuine
 * duplicate-symbol conflict for either consumer -- both define
 * LOGIN_REAL_UTMPX_PUTUTXLINE in their own CFLAGS specifically to opt
 * out of it here. Any FUTURE consumer of this file that does NOT
 * define that macro still gets this exact honest stub unchanged. */
#ifndef LOGIN_REAL_UTMPX_PUTUTXLINE
struct utmpx;

struct utmpx *
pututxline(const struct utmpx *utx)
{
	(void)utx;
	return (struct utmpx *)0;
}
#endif /* !LOGIN_REAL_UTMPX_PUTUTXLINE */

typedef long date_compat_time_t;

int
netsettime(date_compat_time_t tval)
{
	(void)tval;
	return 1;
}

/* Real settimeofday(2) -- bsd/kern/syscalls.master #122, NO_SYSCALL_STUB
 * (2-pointer-arg real kernel trap), same real carry-flag raw-asm pattern
 * as cp_mv_syscalls.c's lchown/sigaction/setattrlist. Only reached from
 * date.c's -d/-t (set kernel DST flag / minutes-west) and the (jflag==0)
 * actual-clock-set path -- both real, rare, privileged uses, but cheap
 * to make genuinely real rather than stubbed: this implements the
 * PUBLIC `settimeofday` symbol directly via the raw syscall, skipping
 * Apple's real Libc/sys/settimeofday.c wrapper's own notify_post()/
 * _simple_asl_log() side effects (real notifyd/ASL calls this project
 * has neither of, same class of gap as pututxline's own comment above)
 * -- same "implement the real public wrapper name directly, skip a
 * private-service indirection this project doesn't have" precedent
 * cp_mv_syscalls.c's own header comment documents for lchown/sigaction.
 * The actual clock-set/timezone-set behavior is real and correct; only
 * the cosmetic notify/logging side effects are absent. */
struct timeval;
struct timezone;

extern int cerror_nocancel(int err);

static inline long date_compat_syscall2(long num, long a0, long a1) {
	register long x0 asm("x0") = a0;
	register long x1 asm("x1") = a1;
	register long x16 asm("x16") = num;
	long is_error;
	asm volatile("svc #0x80\n"
	             "cset %1, cs"
	             : "+r"(x0), "=r"(is_error)
	             : "r"(x1), "r"(x16)
	             : "memory", "cc");
	if (is_error) {
		return cerror_nocancel((int)x0);
	}
	return x0;
}

#define DATE_COMPAT_SYS_settimeofday 122

int
settimeofday(const struct timeval *tp, const struct timezone *tzp)
{
	return (int)date_compat_syscall2(DATE_COMPAT_SYS_settimeofday, (long)tp, (long)tzp);
}

/* Real crt_externs.h _NSGetArgv() -- oldsyslog.c's vsyslog() dereferences
 * it to get argv[0] as openlog()'s default log tag (cosmetic; the
 * message is never actually delivered anywhere given cat_socket_stub.c's
 * honest socket()==ENOSYS, see this file's own top-of-file comment).
 * Real, not fabricated: third_party/Csu/crt.c already populates the real
 * NXArgv global from the real argv this process was actually invoked
 * with (same global real crt_externs()/_NSGetArgv() forward to on a
 * normal dyld-based macOS -- this project has no dyld, but the
 * underlying data this call is supposed to expose is the same real,
 * correctly-populated pointer, just reached directly instead of via a
 * dyld-provided indirection). */
extern const char **NXArgv;

char ***
_NSGetArgv(void)
{
	return (char ***)&NXArgv;
}

/* DAR-203 (2026-09-04): this file used to carry its own honest
 * ENOSYS send()/writev() stubs here, aliased to _send$UNIX2003/
 * _writev$UNIX2003 by build_simple_cmds.sh's date-only link_aliases
 * case. Real investigation (not a blind relink, see DAR-203's own
 * ticket writeup) found the previous header comment's claim above --
 * "both call sites are only reached AFTER a successful connect()" --
 * was only half right:
 *
 *   - writev(2) in vsyslog() (oldsyslog.c) has two call sites, both
 *     gated on _sl_LogStat bits (LOG_PERROR / LOG_CONS) that date.c
 *     never sets (date.c calls the bare syslog(LOG_AUTH|LOG_NOTICE,
 *     "date set by %s", p) with no preceding openlog(), so
 *     _sl_LogStat stays whatever vsyslog()'s own internal
 *     openlog(_sl_LogTag, _sl_LogStat|LOG_NDELAY, 0) leaves it --
 *     LOG_NDELAY only). Genuinely dead code from date's own call
 *     graph, not merely "protected by a failing connect()".
 *
 *   - send(2) in vsyslog() is NOT gated on a successful connect() --
 *     it's called unconditionally after the openlog() attempt. It
 *     IS a live, reached call site (date.c's own setthetime(), the
 *     real actually-set-the-clock path taken whenever `date` is
 *     invoked without -j) -- but always with _sl_LogFile == -1,
 *     because cat_socket_stub.c's own socket() unconditionally fails
 *     with ENOSYS first (no syslogd, Phase 5.5, still not built;
 *     cat_socket_stub.c itself is untouched by this ticket, shared
 *     with cat -u). A real send(-1, ...) syscall is honestly rejected
 *     by the kernel with EBADF before touching any networking code --
 *     same "always fails, never actually talks to a socket" outcome
 *     as the old local ENOSYS stub, just via the real dylib's real
 *     syscall wrapper instead of a hand-written one.
 *
 * Conclusion: swapping to libsystem_kernel.dylib's real
 * _send$UNIX2003/_writev$UNIX2003 changes date's runtime behavior not
 * at all (writev: never called; send: always called with an invalid
 * fd, always fails, just with a different real errno). Local stubs
 * and their link_aliases case removed; date now links
 * libsystem_kernel.dylib directly (see build_simple_cmds.sh's own
 * date extra_libs case) so the two symbols resolve for real instead
 * of relying on a transitive reexport, matching this project's own
 * established bind-resolution precedent (CLAUDE.md's DAR-217 note;
 * build_simple_cmds.sh's own libsystem_platform.dylib/libcopyfile
 * .dylib/libremovefile.dylib comments above its LINK_DYLIBS
 * definition document the same real reexport-vs-direct-link
 * hazard). */
