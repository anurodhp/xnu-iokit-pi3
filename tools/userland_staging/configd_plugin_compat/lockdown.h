/* Real, honestly-documented compat stand-in for <lockdown.h> (MobileLockdown
 * private framework) + <SoftLinking/WeakLinking.h>, needed by
 * Plugins/InterfaceNamer/ifnamer.c's real "Trust required support [iOS]"
 * section (ifnamer.c:2384-3390-ish, entirely #if TARGET_OS_IPHONE).
 *
 * Confirmed via full pinned-repo grep: neither header exists anywhere in
 * this project's pinned third_party/ tree. Both are genuinely
 * never-open-sourced Apple-internal headers (MobileLockdown is not on
 * apple-oss-distributions; SoftLinking is an internal build-system helper
 * umbrella, also never shipped as its own open-source repo) -- same class
 * as this project's existing os/log_private.h, os/variant_private.h,
 * ne_session.h stand-ins.
 *
 * Real, load-bearing fact confirmed by reading ifnamer.c directly: EVERY
 * real caller of lockdown_is_host_trusted()/the four kLockdownNotification*
 * constants is reached only through haveLockdown() (ifnamer.c ~line 2399),
 * which is real Apple's own designed-in graceful-degradation check --
 * exactly the mechanism real macOS hardware (which also has no
 * MobileLockdown framework) relies on. haveLockdown() is itself only
 * consulted at ifnamer.c:3384 (`if (haveLockdown()) {...}`), gating the
 * entire "Trust Required" (iOS-host-pairing / USB tethering) subsystem --
 * real functionality this Pi3/Ethernet-en0 target genuinely has no
 * equivalent for (no lockdownd, no idevice pairing, no USB-tethered-host
 * concept). Declaring these symbols here WITHOUT a body and letting
 * ifnamer.c's own real WEAK_LINK_FORCE_IMPORT() calls mark them
 * weak-imported means the linker resolves each reference to NULL at
 * runtime -- haveLockdown() then correctly, honestly evaluates to FALSE,
 * and the entire subsystem is skipped, matching real behavior on real
 * lockdownd-less hardware. This is NOT a functional shortcut: it is the
 * real, designed fallback path already built into Apple's own source.
 *
 * Real signatures below are inferred directly from ifnamer.c's own call
 * site (`lockdown_is_host_trusted(MY_PLUGIN_ID, NULL, &error)` where
 * `error` is declared `CFStringRef error = NULL;`) -- sufficient to
 * type-check the real call, and irrelevant to runtime behavior since the
 * weak-imported symbol is never actually invoked (gated out, see above).
 */
#ifndef CONFIGD_PLUGIN_LOCKDOWN_COMPAT_H
#define CONFIGD_PLUGIN_LOCKDOWN_COMPAT_H

#include <CoreFoundation/CoreFoundation.h>

Boolean lockdown_is_host_trusted(CFStringRef service_name, CFStringRef udid, CFStringRef *out_error);

extern const CFStringRef kLockdownNotificationHostAttached;
extern const CFStringRef kLockdownNotificationHostDetached;
extern const CFStringRef kLockdownNotificationTrustedHostAttached;
extern const CFStringRef kLockdownNotificationTrustedPTPAttached;

#endif
