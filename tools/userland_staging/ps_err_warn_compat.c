/* Real BSD err(3)/warn(3)/verr(3)/vwarn(3): declared in real
 * third_party/Libc/include/err.h but their bodies are absent from this
 * checkout entirely -- only errc/warnc/errx/warnx/verrc/vwarnc
 * (third_party/Libc/gen/FreeBSD/err.c) actually ship, confirmed by grep
 * across the whole tree. Same class of gap as build_libc_string.sh's
 * missing memcpy/memset (Apple's checkout doesn't include a portable
 * implementation for this either) -- last resort per CLAUDE.md's source-
 * priority rule, but trivial and unambiguous: real BSD err(3)'s own man
 * page defines err(eval,fmt,...) as exactly errc(eval,errno,fmt,...),
 * and warn(fmt,...) as exactly warnc(errno,fmt,...) -- using the errno
 * value at the time of the call, which is also real errc/warnc's own
 * first-class documented parameter, not reimplemented logic. */
#include <errno.h>
#include <stdarg.h>

extern void errc(int eval, int code, const char *fmt, ...);
extern void warnc(int code, const char *fmt, ...);
extern void verrc(int eval, int code, const char *fmt, va_list ap);
extern void vwarnc(int code, const char *fmt, va_list ap);

/* verr()/vwarn() themselves turned out to already be real, present in
 * third_party/Libc/gen/FreeBSD/err.c (an earlier grep pass missed them --
 * only err()/warn() are genuinely absent) -- only those two are defined
 * here, avoiding a real duplicate-symbol link error against the real
 * ones. */
void err(int eval, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    verrc(eval, errno, fmt, ap);
    va_end(ap);
}

void warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vwarnc(errno, fmt, ap);
    va_end(ap);
}

/* flsl(3): also declared-nowhere/defined-nowhere in this checkout
 * (needed by real stdlib/FreeBSD/qsort.c's DEPTH() macro) -- same class
 * of gap, real documented semantics (index of highest set bit + 1, 0 for
 * x==0), trivial and unambiguous. */
/* pthread_testcancel(3): real termios.c's tcdrain() calls it as a real
 * POSIX cancellation checkpoint. This project's libpthread.a build
 * doesn't include real Apple's cancellation-support file (out of scope
 * for the single-thread-correct primitives tier, see
 * build_libpthread.sh) -- and there IS no real thread-cancellation
 * subsystem on this target regardless (no real pthread_create, see
 * that build's own outcome-B finding). A no-op here is the honest,
 * correct answer: "no cancellation request is ever pending" is
 * unconditionally true when no mechanism to request one exists, not a
 * fabricated success. */
void pthread_testcancel(void) {
}

int flsl(long x) {
    int i = 0;
    unsigned long ux = (unsigned long)x;
    while (ux != 0) {
        i++;
        ux >>= 1;
    }
    return i;
}
