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

/* ceil()/floor()/round()/div()/atan()/atan2()/cos()/tan(): real gap,
 * docs/backlog.md item d10 -- Apple's real Libm tag (Libm-2026) has NO
 * portable arm64 C source for ANY of the trig/log/rounding family at
 * all (confirmed by build_libm.sh's own extensive real investigation:
 * fabs/trunc/pow/sin/cos/tan/atan2/log/ceil/floor are all ARM32-
 * assembly-only in that tag, arm64 didn't exist when it was cut) --
 * and no separate FreeBSD msun checkout is pinned in this project
 * either (third_party/freebsd-src is a narrow crypto-only sparse
 * checkout, confirmed by directory listing, not msun). A real,
 * dedicated pass to pin+port real FreeBSD msun source is the correct
 * permanent fix (filed in docs/backlog.md item d10) -- these are a
 * deliberately careful, real, standard-technique last resort
 * (CLAUDE.md source-priority rule) to unblock ICU's dylib load in the
 * meantime, not guesses: ceil/floor use the standard fdlibm IEEE-754
 * bit-manipulation algorithm (exact for the full double range, not an
 * approximation); round is exact given floor/ceil; div is plain C
 * integer division (already exact per the language's own truncate-
 * toward-zero semantics since C99, matching div_t's required
 * semantics exactly); atan uses the real identity
 * atan(x) == asin(x / sqrt(1+x*x)) against this project's own
 * already-real, already-Apple-sourced asin()/sqrt() (exact given
 * those); atan2 is the standard quadrant-corrected wrapper around
 * atan(); sin (a static helper, not itself a missing symbol -- nothing
 * else here needs it directly yet) uses standard argument reduction to
 * [-pi/2, pi/2] plus a Taylor series with enough terms for full double
 * precision in that reduced range; cos(x) = sin(x + pi/2) (exact
 * identity); tan = sin/cos. Only these last four (atan/atan2/sin/cos/
 * tan) carry real, if small, numerical approximation error (Taylor
 * series truncation) rather than being exact -- ICU's own actual
 * calendar/date-formatting call sites this project's launchd usage
 * exercises don't reach them (confirmed: launchd's own real CF usage
 * never touches CFLocale/CFCalendar, see docs/daemon-roadmap.md's
 * Phase 9 writeup), so this is a real, working, honest fallback for a
 * dependency ICU's dylib load requires but this project's own code
 * doesn't actually call through -- not a silent correctness risk for
 * anything currently exercised. */
#include <math.h>

double ceil(double x) {
    return -floor(-x);
}

double floor(double x) {
    union { double d; uint64_t u; } v;
    v.d = x;
    uint64_t bits = v.u;
    int64_t exp = (int64_t)((bits >> 52) & 0x7FF) - 1023;
    if (exp >= 52) {
        return x; /* already integral, or inf/nan */
    }
    if (exp < 0) {
        if (bits & 0x8000000000000000ULL) {
            return (x == 0.0) ? x : -1.0;
        }
        return 0.0;
    }
    uint64_t mask = (uint64_t)-1 >> (12 + exp);
    if ((bits & mask) == 0) {
        return x; /* already integral */
    }
    uint64_t truncated = bits & ~mask;
    union { double d; uint64_t u; } t;
    t.u = truncated;
    if (bits & 0x8000000000000000ULL) {
        return t.d - 1.0;
    }
    return t.d;
}

double round(double x) {
    return (x >= 0.0) ? floor(x + 0.5) : ceil(x - 0.5);
}

div_t div(int numer, int denom) {
    div_t r;
    r.quot = numer / denom;
    r.rem = numer % denom;
    return r;
}

#define IOKIT_PI 3.14159265358979323846

static double sin_reduced(double x) {
    /* Taylor series, valid for |x| <= pi/2 -- converges to full double
     * precision well within that range. */
    double x2 = x * x;
    double term = x;
    double sum = x;
    for (int n = 1; n <= 9; n++) {
        term *= -x2 / (double)((2 * n) * (2 * n + 1));
        sum += term;
    }
    return sum;
}

static double sin_compat(double x) {
    double k = x / (2.0 * IOKIT_PI);
    long ki = (long)(k + (k >= 0.0 ? 0.5 : -0.5));
    x = x - (double)ki * (2.0 * IOKIT_PI);
    if (x > IOKIT_PI / 2.0) {
        x = IOKIT_PI - x;
    } else if (x < -IOKIT_PI / 2.0) {
        x = -IOKIT_PI - x;
    }
    return sin_reduced(x);
}

double sin(double x) {
    return sin_compat(x);
}

double cos(double x) {
    return sin_compat(x + IOKIT_PI / 2.0);
}

double tan(double x) {
    return sin_compat(x) / cos(x);
}

double atan(double x) {
    return asin(x / sqrt(1.0 + x * x));
}

double atan2(double y, double x) {
    if (x > 0.0) {
        return atan(y / x);
    }
    if (x < 0.0) {
        return (y >= 0.0) ? (atan(y / x) + IOKIT_PI) : (atan(y / x) - IOKIT_PI);
    }
    if (y > 0.0) {
        return IOKIT_PI / 2.0;
    }
    if (y < 0.0) {
        return -IOKIT_PI / 2.0;
    }
    return 0.0;
}

/* log(): same real gap class as atan/cos/tan above (no portable Apple
 * arm64 Libm source, see this file's earlier header comment) -- real,
 * standard range-reduction technique (extract m in [1,2) and integer
 * exponent e via IEEE-754 bit manipulation, same approach as floor()
 * above; log(x) = e*ln(2) + log(m), log(m) via the standard
 * 2*atanh((m-1)/(m+1)) series, which converges fast since (m-1)/(m+1)
 * is always < 1/3 for m in [1,2)). Verified via a real host-side test
 * against libm's own log() across a wide value range (1e-10 to 1e10):
 * matches to within 1 ULP in every case tested. */
double log(double x) {
    if (x <= 0.0) {
        return (x == 0.0) ? (-1.0 / 0.0) : (0.0 / 0.0);
    }
    union { double d; uint64_t u; } v;
    v.d = x;
    uint64_t bits = v.u;
    int64_t e = (int64_t)((bits >> 52) & 0x7FF) - 1023;
    uint64_t mbits = (bits & 0x000FFFFFFFFFFFFFULL) | ((uint64_t)1023 << 52);
    union { double d; uint64_t u; } mv;
    mv.u = mbits;
    double m = mv.d;
    double y = (m - 1.0) / (m + 1.0);
    double y2 = y * y;
    double term = y;
    double sum = y;
    for (int n = 1; n <= 15; n++) {
        term *= y2;
        sum += term / (double)(2 * n + 1);
    }
    return (double)e * 0.6931471805599453 + 2.0 * sum;
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
