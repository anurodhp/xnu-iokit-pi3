/* Minimal, honestly-scoped stand-ins for the rlimit/fd-table accessors
 * third_party/Libc/stdio/FreeBSD/findfp.c calls - real behavior would need
 * a real getrlimit(2) plumbed through, which this target's raw-syscall
 * layer doesn't have yet (see docs/roadmap.md Phase 4 for what's ported
 * so far). Same "real missing subsystem, fixed documented constant, not a
 * silently-wrong guess" pattern already used by getenv()/issetugid() in
 * libc_locale_env_stubs.c.
 *
 *   sysconf(_SC_STREAM_MAX): __sinit() uses this to size __stream_max,
 *     the ceiling f_prealloc()/moreglue() enforce for "streams counted
 *     against STREAM_MAX". FOPEN_MAX (third_party/Libc/include/stdio.h's
 *     own real minimum) is the correct conservative answer for a target
 *     with no real per-process fd-limit query yet - it's what stdio
 *     itself already assumes as its baseline, not an arbitrary number.
 *   getdtablesize(): only reachable via the deliberately-unused
 *     f_prealloc() (see build_libc_stdio.sh's header comment - not
 *     called by anything in this project's smoketest or shell), but
 *     still part of findfp.o as a whole linked object, so needs SOME
 *     real symbol. Returns the same FOPEN_MAX-based constant for
 *     consistency with the sysconf stand-in above.
 */

#include <stdio.h>  /* FOPEN_MAX */
#include <unistd.h> /* real _SC_STREAM_MAX value */

long sysconf(int name) {
    if (name == _SC_STREAM_MAX) {
        return FOPEN_MAX;
    }
    return -1;
}

int getdtablesize(void) {
    return FOPEN_MAX;
}
