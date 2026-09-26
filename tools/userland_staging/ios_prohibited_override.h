/* Same real gap, same real fix as regex_availability_override.h (see
 * that file's own header comment for the full story of why this
 * project's uniform `-target arm64-apple-ios14.4` build trips real
 * Apple App-Store-review-time "unavailable on iOS" policy annotations
 * on public API that is, on this non-App-Store embedded target, fully
 * present and functional).
 *
 * This project's Libc/include/stdlib.h marks system(3) with
 * `__IOS_PROHIBITED` (Availability.h's real
 * `__OS_AVAILABILITY(ios,unavailable)` expansion) -- a real, but
 * purely-lint-shaped, App Store policy marker (real Apple's own
 * rationale, right there in the same header:
 * `__swift_unavailable_on("Use posix_spawn APIs or NSTask instead.",
 * "Process spawning is unavailable")` -- a policy choice about the iOS
 * App Store sandbox, not a claim that system(3) can't function on this
 * architecture). Calling system() (not just declaring it) under this
 * SDK target is then an error -- same "unavailable the moment it's used,
 * not just seen" shape as the regcomp gap.
 *
 * Shared by every build that calls such a function (DAR-345 merged the
 * per-binary copies): ed's `!cmd` shell escape and less (build_simple_cmds.sh),
 * and ssh's clientloop.c:384 `PermitLocalCommand` (build_sshd.sh), which
 * link Apple's real Libc/stdlib/FreeBSD/system.c. pw's
 * pw_compat_include/stdlib.h does the same neutralization inline because
 * it also renames system() around an #include_next.
 *
 * Fix: neutralize just `__IOS_PROHIBITED` to a no-op, by force-
 * #include'ing Availability.h ourselves first (already the ultimate
 * source of the real definition, tripping its own header guard) and
 * redefining that one macro. Every other real availability annotation
 * in every other header stays fully intact. Used via `-include`.
 *
 * A plain -D__IOS_PROHIBITED= on the command line would not work:
 * Availability.h defines the macro with an unguarded #define, which
 * overwrites any -D.
 */
#ifndef _IOS_PROHIBITED_OVERRIDE_H_
#define _IOS_PROHIBITED_OVERRIDE_H_

#include <Availability.h>

#ifdef __IOS_PROHIBITED
#undef __IOS_PROHIBITED
#endif
#define __IOS_PROHIBITED /* nothing -- see header comment */

#endif /* _IOS_PROHIBITED_OVERRIDE_H_ */
