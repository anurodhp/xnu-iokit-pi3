/* Neutralises the iPhoneOS SDK's __IOS_PROHIBITED marker for ONE
 * translation unit of the ssh(1) client build (build_sshd.sh compiles
 * exactly clientloop.c with -include of this file).
 *
 * The SDK annotates system(3) (stdlib.h:188-190,
 * `__swift_unavailable_on(...) __API_AVAILABLE(macos(10.0))
 * __IOS_PROHIBITED ... int system(const char *)`), and clientloop.c:384
 * calls it for `ssh -o PermitLocalCommand`. That marker is Apple's App
 * Store POLICY annotation, not a statement that the function is missing:
 * Apple ships the real implementation, and this project compiles Apple's
 * own real Libc/stdlib/FreeBSD/system.c into the same binary (see
 * build_sshd.sh), built on the real posix_spawn wrapper it also
 * compiles. Without the call site this port would be silently dropping a
 * documented ssh_config option from unmodified upstream source.
 *
 * Why a force-included header and not just -D__IOS_PROHIBITED= on the
 * command line: Availability.h:257 defines that macro with a PLAIN
 * #define (no #ifndef guard), so it overwrites any -D. Including
 * Availability.h here first, then #undef'ing, works because that header
 * has a real include guard -- stdlib.h's own later #include of it is a
 * no-op, so this definition is the one in effect at the declaration.
 * Nothing else in the SDK is altered: __IOS_UNAVAILABLE and every
 * __API_* macro keep their real meaning, and this file is used for no
 * other source in this build.
 */
#ifndef _SSHD_IOS_PROHIBITED_OVERRIDE_H_
#define _SSHD_IOS_PROHIBITED_OVERRIDE_H_
#include <Availability.h>
#undef __IOS_PROHIBITED
#define __IOS_PROHIBITED
#endif
