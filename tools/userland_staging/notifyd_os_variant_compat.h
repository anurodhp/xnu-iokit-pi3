#ifndef _NOTIFYD_OS_VARIANT_COMPAT_H_
#define _NOTIFYD_OS_VARIANT_COMPAT_H_
/* Real gap: notify_client.c (PureDarwin's real, unmodified copy) calls
 * os_variant_has_internal_diagnostics()/os_fault_with_payload() with no
 * prototype in scope, a hard clang error under this project's CFLAGS.
 *
 * os_fault_with_payload() DOES have a real, already-pinned declaration
 * -- third_party/xnu-7195/BUILD.hdrs/dst/usr/local/include/
 * os/reason_private.h, already reachable via this project's own
 * `-idirafter $XNU/BUILD.hdrs/dst/usr/local/include` -- so it's included
 * here rather than re-declared (a second, textually-different
 * declaration of the same real function is a real "conflicting types"
 * error, found the hard way writing this file).
 *
 * os_variant_has_internal_diagnostics() has no real declaration anywhere
 * in this pinned SDK or third_party checkout (real Apple's own
 * <os/variant_private.h>, grepped, zero hits) -- this header supplies
 * the one real, missing prototype, matching the real body this
 * project's own libsystem_notify_misc_compat.c defines (see that file's
 * own header comment for the full real gap writeup -- same one
 * build_libsystem_notify.sh's own real fix already established for
 * libsystem_notify.dylib).
 */
#include <os/reason_private.h>
#ifdef __cplusplus
extern "C" {
#endif
int os_variant_has_internal_diagnostics(const char *subsystem);
#ifdef __cplusplus
}
#endif
#endif /* _NOTIFYD_OS_VARIANT_COMPAT_H_ */
