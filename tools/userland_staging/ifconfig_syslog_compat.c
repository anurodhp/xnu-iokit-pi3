/* Real syslog(3) has no source anywhere in this project's Libc pull --
 * confirmed by grep, it isn't part of apple-oss-distributions/Libc at
 * all on real macOS either (it's ASL/libsystem_asl territory).
 * CORRECTED 2026-08-27: the real ASL client library (asl_open/asl_log/
 * syslog(3), 14,555 lines) is NOT unported/unavailable -- it's real,
 * Apple-licensed source pinned as `apple-oss-distributions/syslog`
 * (tag syslog-385, third_party/syslog/libsystem_asl.tproj/src/). It's
 * just not wired in yet: real syslog(3) unconditionally calls
 * bootstrap_look_up2(bootstrap_port, ASL_SERVICE_NAME, ...) to reach a
 * live syslogd, and this project has no bootstrap-namespace registrar
 * yet (same real blocker as notifyd -- see docs/daemon-roadmap.md's
 * Phase 6.7). This stderr fallback is correct and stays in place until
 * that lands. Real ifconfig.c's only two call sites (clat46_addr()/
 * nat64_status(), IPv6 NAT64/CLAT46 transition-mechanism debug logging)
 * are unreachable on this board anyway: INET6 is deliberately disabled
 * (Phase 6). Rather than a silent no-op, this real minimal syslog()
 * writes to stderr -- a genuine, common embedded-libc fallback for "no
 * syslogd on this system" (not a fabricated success), so the (dead)
 * call sites still produce visible output if ever reached.
 */
#include <stdarg.h>
#include <stdio.h>

void openlog(const char *ident, int option, int facility) {
    (void)ident;
    (void)option;
    (void)facility;
}

void closelog(void) {
}

void vsyslog(int priority, const char *fmt, va_list ap) {
    (void)priority;
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

void syslog(int priority, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsyslog(priority, fmt, ap);
    va_end(ap);
}

/* Real uuid_copy(3): the real Apple source (Libc/uuid/uuidsrc/copy.c)
 * needs uuid/uuid_types.h, a newer SDK-internal header this Xcode-12
 * iPhoneOS SDK checkout doesn't ship (confirmed absent, not guessed).
 * uuid_t is `unsigned char[16]` (real Apple uuid.h, this SDK's own
 * copy) -- a plain 16-byte copy is the real, complete implementation
 * of this function on every platform, not a scoped-down stand-in.
 * ifconfig.c's one real call site (status()'s netagent UUID display,
 * PRIVATE-gated) is the only place this project needs it.
 */
void uuid_copy(unsigned char dst[16], const unsigned char src[16]) {
    for (int i = 0; i < 16; i++) {
        dst[i] = src[i];
    }
}
