/* iokit project: real gaps found booting launchd as PID 1 -- ICU's own
 * dylibs (libicuuc.66.dylib/libicui18n.66.dylib) reference these
 * directly, expected from libSystem (docs/backlog.md item d10).
 *
 * ___chkstk_darwin: real arm64 stack-probe helper ld64 auto-inserts
 * calls to for large stack frames. Same real no-op body as
 * build_libdispatch_dylib.sh's own stack_chk_compat.c (that file's own
 * header has the full real citation: no functional effect either way,
 * purely a defense-in-depth guard-page-touch this project's targets
 * don't need). Duplicated here rather than shared because libdispatch
 * and libsystem_c are separate dylib images -- real Apple ships this in
 * Libplatform, closer to where this copy lives conceptually, but the
 * two-level-namespace binding is what actually matters, not which
 * physical dylib "owns" it architecturally.
 *
 * strstr(3): no real Apple/FreeBSD source exists anywhere in this
 * project's pinned Libc checkout (confirmed via grep, not assumed) --
 * a real, if surprising, gap (already worked around per-consumer via
 * ncurses's own bundled copy, see build_launchd_core.sh's
 * launchd_libc_extras.a narrowing). Plain, well-known, textbook naive
 * substring search -- no numerical precision concerns (unlike the
 * still-open real libm gaps, see docs/backlog.md item d10's own
 * writeup), a legitimate last resort per CLAUDE.md's source-priority
 * rule.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

long __stack_chk_guard = 0x0BADC0DEDEADBEEFL;
void __stack_chk_fail(void) { extern void abort(void); abort(); }
void __chkstk_darwin(void) {}

char *strstr(const char *haystack, const char *needle) {
    if (*needle == '\0') {
        return (char *)haystack;
    }
    for (; *haystack != '\0'; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h != '\0' && *n != '\0' && *h == *n) {
            h++;
            n++;
        }
        if (*n == '\0') {
            return (char *)haystack;
        }
    }
    return NULL;
}

/* div(): plain C integer division, exact (C99 truncates toward zero,
 * which is div_t's required semantics).
 *
 * The math functions that used to be here (ceil, floor, round, sin, cos,
 * tan, atan, atan2, log -- hand-written, and sin/cos/tan/atan/atan2
 * approximations) were removed by DAR-447: libsystem_m.dylib, their Apple
 * owner, now builds the real FreeBSD msun sources for them
 * (build_libm_dylib.sh). */
#include <stdlib.h>

div_t div(int numer, int denom) {
    div_t r;
    r.quot = numer / denom;
    r.rem = numer % denom;
    return r;
}

/* asctime()/asctime_r(): the hand-rolled, locale-independent stand-in
 * that used to live here (and still lives in
 * launchd_audit_syscalls_compat.c's own separate copy, real gap docs/
 * backlog.md item d11) was REMOVED 2026-08-31 (static-linking cleanup,
 * locale/timezone-string consolidation batch, closing DAR-134). Real
 * Apple/FreeBSD source (`stdtime/FreeBSD/asctime.c`) only needed
 * `strftime_l()` to format the 4-digit year -- the one real wall this
 * ticket was blocked on -- and this same build script now compiles a
 * real `strftime.o` (see its own "strftime/strftime_l" comment) for the
 * unrelated locale/timezone-string consolidation batch. Re-tested here
 * (same "atol/strtoll/strtoul just work once retried" pattern DAR-134's
 * own description predicted) and confirmed via a real trial compile:
 * asctime.c's only undefined refs (`___error`/`_sprintf`/`_strcpy`/
 * `_strftime_l$UNIX2003`/`_strlen`) are ALL already resident in this
 * dylib. See build_libsystem_c.sh's own asctime.c compile line for
 * where the real source is pulled in now instead.
 *
 * (__isthreaded is NOT defined here -- already real, in libc_gen.a's
 * own libc_gen_shims.o, confirmed via a real duplicate-symbol link
 * error when a first attempt also defined it here.) */
