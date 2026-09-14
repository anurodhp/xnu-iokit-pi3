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
 * os_variant_has_internal_diagnostics(): CORRECTED 2026-09-14 -- the
 * previous version of this comment claimed "no real declaration
 * anywhere in this pinned SDK or third_party checkout... grepped, zero
 * hits", which was wrong (an unverified assumption, never actually
 * caught until this file was compiled for real for the first time):
 * third_party/Libc/os/variant_private.h DOES declare it for real,
 * returning `bool`, and is reachable from this compile. Re-declaring
 * it here with a different (`int`) return type was the real
 * "conflicting types" bug. Fixed by including the real header instead
 * of hand-rolling a second, wrong-typed prototype -- matching the real
 * body this project's own libsystem_notify_misc_compat.c defines (see
 * that file's own header comment; also fixed to return `_Bool` to
 * match).
 */
#include <os/reason_private.h>
#include <os/variant_private.h>
#endif /* _NOTIFYD_OS_VARIANT_COMPAT_H_ */
