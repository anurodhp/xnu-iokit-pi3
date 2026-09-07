#!/bin/bash
# Builds libc_build/bootp_dhcp_engine.a -- Linear DAR-170's Tier-0 "narrow
# path" first slice: real, unmodified Apple DHCP/BOOTP protocol-engine
# source from third_party/bootp (bootp-413.80.1, pinned in
# setup_third_party.sh 2026-09-01), compiled standalone WITHOUT configd/
# SCDynamicStore.
#
# ============================================================
# WHY THIS FILE SET, HONEST SCOPE STATEMENT -- read this first
# ============================================================
# DAR-170 originally proposed porting real `configd` wholesale (real
# SCDynamicStore + InterfaceNamer + the IPConfiguration bundle all
# together) as the path to a real DHCP-assigned IP on en0. A prior
# analysis (2026-08-22 memory, `project_xnu_rpi3_dhcp_ipconfiguration_
# analysis`) concluded the opposite -- skip Apple's client, port FreeBSD
# dhclient instead -- specifically because IPConfiguration.bproj's
# ipconfigd.c/sysconfig.c are soaked in SCDynamicStore.
#
# Re-verified 2026-09-01 against the real pinned tag (not by name/date
# proximity -- real `grep -c SCDynamicStore` per file): the SC coupling
# is real, but it is NOT spread evenly across the whole bundle. It's
# concentrated in two files: IPConfiguration.bproj/ipconfigd.c (112
# refs -- the daemon main loop AND the per-service DHCP-state-machine
# *driver*) and sysconfig.c (13 refs). The actual DHCP/BOOTP wire
# protocol engine has ZERO SCDynamicStore/SCPreferences references:
# dhcp.c, bootp.c, bootp_session.c, arp_session.c, DHCPLease.c, ifutil.c
# (confirmed by grep of the real cloned tree). dhcp.c itself delegates
# interface configuration to service_set_address()/service_remove_
# address() -- real, but the *implementations* of those two functions
# live in the SC-coupled ipconfigd.c, not in dhcp.c. That's the real
# seam: replace ipconfigd.c's orchestration with a new, small, honestly-
# documented custom driver loop (this project's source-priority rule's
# "last resort" tier, but a far smaller footprint than either porting
# all of configd first or replacing all of Apple's client with FreeBSD's
# dhclient) that calls the real protocol engine directly and writes the
# lease via real BSD ioctls instead of SCDynamicStore publishes.
#
# UPDATE 2026-09-01 (same-day follow-up pass): the user made an explicit
# "no shortcuts" decision on DAR-170 -- skip Tier 0's custom
# orchestration-loop bypass entirely, go straight to Tier 1 (real
# configd/SCDynamicStore/InterfaceNamer/IPConfiguration). This script's
# job stays the same either way: get the real, unmodified DHCP/BOOTP
# protocol-engine files compiling clean, since Tier 1's real
# IPConfiguration bundle links against this exact same protocol engine.
# All 13 real target files below now compile with ZERO gaps (verified by
# this script's own exit code) -- dhcp.c, arp_session.c, ifutil.c, and
# DHCPLease.c (the four files the paragraph below originally deferred)
# are now IN, using dhcp_iopm_gap_compat.h's narrow, honestly-documented
# forwarding stub for the two IOPMRequestSysWake()/
# kIOPMPowerEventLeewayKey symbols this pinned IOKitUser tag's headers
# are missing (real version-skew, not a design gap -- see that header's
# own comment) plus real IOKitLib.h/IOPMLib.h/IOPMLibPrivate.h/
# IOPowerSources.h header staging (below) for everything else. No
# PowerManagement clone was actually needed for this -- IOKitUser
# (already pinned) turned out to carry the real pwr_mgt.subproj/ps.subproj
# headers dhcp.c needs; PowerManagement-1132.81.1 is pinned in
# setup_third_party.sh anyway per docs/apple-oss-catalog.md (real future
# use: BCM2837CPU::quiesceCPU()/haltCPU(), backlog P3 item 4), just not
# consumed by this script.
#
# REAL FINDING, 2026-09-01: SystemConfiguration.framework is NOT its own
# apple-oss-distributions repo -- it doesn't exist as a standalone
# project at all (confirmed: 404/empty search on GitHub). Its real source
# (SystemConfiguration.fproj/, incl. SCValidation.h/SCPrivate.h) ships
# INSIDE the `configd` repo itself, alongside the actual daemon
# (configd.tproj/) and the real client-side IPC library
# (libSystemConfiguration/libSystemConfiguration_client.c, distinct from
# _server.c). So `configd` (configd-1109.60.2, matches the real
# macos-112 manifest) is pinned+cloned here too -- but only as the
# header/source home, NOT because the daemon needs to run for this
# script. This sharpens both DAR-170's framing (configd-the-daemon is
# NOT actually a prerequisite for compiling bootp's protocol engine) and
# the 2026-08-22 memory's (the real wall is narrower than "needs a
# running configd").
#
# Even bootp.c/bootp_session.c themselves (chosen specifically for zero
# *direct* SC/IOKit includes) turned out to transitively need
# SystemConfiguration/SCPrivate.h via bootplib/IPConfigurationLog.h's own
# logging macros -- not caught by a source-level include scan, only by
# actually trying to compile. Real header staging added below (same
# symlink-into-a-framework-shaped-dir pattern build_corefoundation.sh
# uses for CoreFoundation.h) gets past that, but surfaces one more real,
# precisely-diagnosed gap, NOT YET CLOSED in this pass: SCPrivate.h
# #includes <os/log_private.h>, which XNU's own libkern/os/log_private.h
# provides for KERNEL context (real firehose/tracepoint_private.h
# COMM_PAGE machinery) -- not appropriate for a userspace consumer, and
# this project has already deliberately avoided that exact real
# machinery elsewhere (see build_libc_locale.sh's os/assumes.h stand-in
# comment: "this project doesn't have [the COMM_PAGE-based logging
# machinery]... unused include, empty stand-in"). Narrowly staging just
# libkern/OSTypes.h (needed for CFBase.h) instead of exposing all of
# $XNU/libkern stops the wrong header from winning, which correctly
# turns the error into a plain "file not found" for <os/log_private.h> --
# CLOSED 2026-09-01: a real userspace-appropriate compat shim for
# <os/log_private.h> (matching this project's existing os_log_compat.c/
# os/assumes.h precedent) is now written inline below, plus three more
# real gaps found and closed one real compile at a time past that first
# one: <os/variant_private.h> (same never-open-sourced/unused-callsite
# class, one function), <ne_session.h> (NetworkExtension private header,
# never open-sourced, one unused type in one unused function decl),
# SCPreferencesKeychainPrivate.h's real `SecKeychainRef` (a genuine
# macOS-only Keychain Services type this project's pinned iPhoneOS SDK
# marks hard-unavailable for iOS -- empty stand-in, unused by any target
# file here, same class as os/assumes.h), and finally two real BSD
# extended-interface constants (IFEF_EXPENSIVE, SIOCGIFEFLAGS) that
# turned out to just need `-DPRIVATE` (this project's existing
# build_ifconfig.sh precedent for the same net/if.h `#ifdef PRIVATE`
# guard). Final real state: 13/13 files compile clean, zero gaps --
# bootp_dhcp_engine.a written. See each fix's own comment below for the
# full sourced reasoning.
#
# Real dependency ordering: needs libCoreFoundation.a's cf_headers
# staging dir (built by build_corefoundation.sh) and libc_locale.a/
# libmalloc.a/libplatform.a already built, same as every other tier.
#
# This is a compile-only (`-c`) pass, matching build_libc_regex.sh's
# per-file-loop/gap-report template -- NOT yet a working DHCP client. No
# smoketest link is attempted here: there is no real executable target
# for the protocol engine alone (these are real building blocks the real
# IPConfiguration bundle links against, per the "no shortcuts" decision
# above -- not a custom orchestrator). See DAR-170 for the full ordered
# plan and what comes next (porting libSystemConfiguration_client.c,
# then real configd.tproj itself).
set -e
cd "$(dirname "$0")"

BOOTP="../../third_party/bootp"
CONFIGD="../../third_party/configd"
LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
IOKITUSER="../../third_party/IOKitUser"
OUT="libc_build"
mkdir -p "$OUT/bootp_dhcp_engine"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -d "$BOOTP" ]; then
    echo "error: $BOOTP not found -- run setup_third_party.sh first (bootp pin added 2026-09-01)" >&2
    exit 1
fi
for a in libc_locale libmalloc libplatform; do
    if [ ! -f "$OUT/$a.a" ]; then
        echo "error: $OUT/$a.a not found -- run build_${a}.sh first" >&2
        exit 1
    fi
done
if [ ! -d "$OUT/cf_headers/CoreFoundation" ]; then
    echo "error: $OUT/cf_headers/CoreFoundation not found -- run build_corefoundation.sh first" >&2
    exit 1
fi
if [ ! -d "$CONFIGD" ]; then
    echo "error: $CONFIGD not found -- run setup_third_party.sh first (configd pin added 2026-09-01)" >&2
    exit 1
fi
if [ ! -d "$IOKITUSER" ]; then
    echo "error: $IOKITUSER not found -- run setup_third_party.sh first (IOKitUser already pinned for build_iokituser.sh)" >&2
    exit 1
fi

# Real header-staging step, same symlink-into-a-framework-shaped-dir
# pattern build_corefoundation.sh uses for CoreFoundation.h -- bootp's own
# real headers (cfutil.h, IPConfigurationLog.h) #include
# <SystemConfiguration/SCValidation.h>/<SystemConfiguration/SCPrivate.h>
# with that directory prefix, but configd's real source tree has them
# directly under SystemConfiguration.fproj/ with no such subdirectory.
rm -rf "$OUT/sc_headers/SystemConfiguration"
mkdir -p "$OUT/sc_headers/SystemConfiguration"
find "$CONFIGD/SystemConfiguration.fproj" -maxdepth 1 -iname "*.h" -exec ln -sf "$(pwd)/{}" "$OUT/sc_headers/SystemConfiguration/" \;

# Real, narrow fix for a real collision: configd's own SCPrivate.h
# #includes <os/log_private.h>, which this project needs reachable for
# libkern/OSTypes.h (CFBase.h's own real dependency, see build_
# corefoundation.sh) via a plain `-I $XNU/libkern`. But that same -I also
# makes plain `<os/log_private.h>` resolve to XNU's KERNEL-internal
# libkern/os/log_private.h (real firehose/tracepoint_private.h COMM_PAGE
# machinery meant for kernel context, not userspace) instead of a
# userspace-appropriate header -- this project already deliberately
# avoids that same real machinery elsewhere (see build_libc_locale.sh's
# os/assumes.h stand-in comment: "this project doesn't have [the
# COMM_PAGE-based logging machinery]"). Stage OSTypes.h alone into its
# own dir instead of exposing all of $XNU/libkern, so <os/log_private.h>
# does NOT resolve to the kernel one.
rm -rf "$OUT/sc_headers/libkern"
mkdir -p "$OUT/sc_headers/libkern"
ln -sf "$(pwd)/$XNU/libkern/libkern/OSTypes.h" "$OUT/sc_headers/libkern/OSTypes.h"

# Real userspace compat stand-in for <os/log_private.h> itself, closing
# the gap this script's header comment above flagged as the concrete
# next step (2026-09-01). Confirmed via a real WebSearch pass + a full
# grep of every pinned apple-oss-distributions repo in this project:
# os/log_private.h has never been open-sourced anywhere (it lives only
# inside the closed Libtrace/os_log runtime) -- unlike the *public*
# <os/log.h>, which is real and already present in this project's
# pinned iPhoneOS14.4 SDK (os_log_t/os_log_type_t/os_log_create/
# os_log_type_enabled all come from there, untouched, already used by
# build_corefoundation.sh). This project also has no os_log(3) delivery
# daemon at all (see os_log_compat.c's own header comment) -- real,
# unmodified configd source (SCD.c's __SC_log_enabled()) already checks
# os_log_type_enabled() first, and this project's real
# os_log_type_enabled() always returns false, so every real SC_log()/
# os_log_pack_*() call site is unreachable at runtime regardless of what
# this header declares. This only needs to let real, unmodified Apple
# source PARSE (this pass) and eventually LINK (a future pass, once
# sc_log_pack_compat.c is wired into a real link) -- it is NOT a
# reimplementation of Apple's real, never-open-sourced, ABI-undocumented
# firehose pack-buffer wire format.
#
# struct os_log_pack_s's two named members below (olp_continuous_time/
# olp_wall_time) are the ONLY fields any real pinned source in this
# project actually dereferences (SCD.c's __SC_log_send(), print-only
# path: `pack->olp_continuous_time`/`pack->olp_wall_time`) -- confirmed
# by grep, not guessed at Apple's full private struct layout. Reserved
# padding is included since this project always allocates instances of
# this struct itself (os_log_pack_decl below) and never receives one
# from a real Apple binary, so exact upstream size/layout doesn't matter
# for correctness here.
rm -rf "$OUT/compat_include/os/log_private.h"
mkdir -p "$OUT/compat_include/os"
# os/log_private.h: installed from the ONE checked-in copy at
# os_log_compat_headers/os/log_private.h, not written inline here.
#
# This used to be a `cat >` heredoc in this script AND a second,
# independently-maintained heredoc in build_libsystemconfiguration_client.sh, both writing this exact
# path, with neither script depending on the other and build_configd.sh
# reusing the directory rather than regenerating it -- so whichever ran
# last silently won. They diverged in two ways that both mattered (an
# 8-byte struct layout skew that became a real out-of-bounds stack
# write, and a declaration present in only one copy); see that header's
# own comment for the full write-up (DAR-263).
#
# The `cmp` is the point: it makes a future divergence a build failure
# here instead of a silent layout skew at runtime.
mkdir -p "$OUT/compat_include/os"
cp os_log_compat_headers/os/log_private.h "$OUT/compat_include/os/log_private.h"
cmp -s os_log_compat_headers/os/log_private.h "$OUT/compat_include/os/log_private.h" || {
    echo "error: failed to install os_log_compat_headers/os/log_private.h" >&2
    exit 1
}

# Same real gap class, one header later in SCPrivate.h's own #include
# block: <os/variant_private.h> is also never open-sourced (confirmed by
# the same WebSearch + full pinned-repo grep pass above), and this
# project already has a real, honest precedent for its sibling SPI --
# os_variant_has_internal_diagnostics() in syslogd_oslog_compat.c /
# libsystem_notify_misc_compat.c, both documented "real, always-safe
# default answer -- this is not an internal Apple diagnostics build".
# SCPrivate.h's own real source (SCD.c:820) only calls the ONE function
# this header declares (os_variant_has_internal_content(), the
# "...content" not "...diagnostics" sibling -- confirmed by grep, not
# guessed) to gate an extra verbose-log-content decision, same honest
# "false" default applies.
rm -rf "$OUT/compat_include/os/variant_private.h"
mkdir -p "$OUT/compat_include/os"
cat > "$OUT/compat_include/os/variant_private.h" <<'EOF'
#ifndef _OS_VARIANT_PRIVATE_H_COMPAT_
#define _OS_VARIANT_PRIVATE_H_COMPAT_
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
bool os_variant_has_internal_content(const char *subsystem);
#ifdef __cplusplus
}
#endif
#endif /* _OS_VARIANT_PRIVATE_H_COMPAT_ */
EOF

# Real gap, different repo class: <ne_session.h> is NetworkExtension.
# framework's private session-status header -- NetworkExtension has
# never been open-sourced at all (no apple-oss-distributions repo of
# that name exists; confirmed by the same audit pass as the os/log and
# os/variant gaps above). SCNetworkConnectionPrivate.h's own real,
# unmodified source uses exactly one symbol from it,
# ne_session_status_t, in exactly one function declaration
# (SCNetworkConnectionGetStatusFromNEStatus(), line ~497) -- confirmed
# by grep that none of this pass's 8 target files call that function or
# reference the type, so its exact enum values are unused dead weight
# here, same class as dhcp_iopm_gap_compat.h's IOPMRequestSysWake().
# A plain, documented-as-unverified placeholder typedef is enough to let
# real Apple source parse without fabricating functional behavior no
# call site in this project actually exercises.
rm -f "$OUT/compat_include/ne_session.h"
cat > "$OUT/compat_include/ne_session.h" <<'EOF'
#ifndef _NE_SESSION_H_COMPAT_
#define _NE_SESSION_H_COMPAT_
/* NetworkExtension.framework internals -- never open-sourced. Only
 * consumer in this project's pinned source (SCNetworkConnectionPrivate.h)
 * declares a function using this type but never calls it (confirmed by
 * grep) -- placeholder int typedef, real enum values unverified/unneeded. */
typedef int ne_session_status_t;
#endif
EOF

# Real gap, same "unused, empty stand-in" class as build_libc_locale.sh's
# os/assumes.h precedent: SCPrivate.h's own real, unmodified source
# #includes <SystemConfiguration/SCPreferencesKeychainPrivate.h>
# (line 59), whose real declarations (_SCSecKeychainCopySystemKeychain()
# etc) reference `SecKeychainRef` -- a real macOS-only Keychain Services
# type this project's pinned iPhoneOS14.4 SDK's own Security.framework
# marks `unavailable` for iOS at the TYPE level (confirmed by a direct
# standalone compile: `error: 'SecKeychainRef' is unavailable: not
# available on iOS`, SecBase.h:135 -- a hard, non-suppressible clang
# error, not a `-Wno-availability`-controllable warning, confirmed by
# testing that flag first). This is a genuine SDK-target mismatch (this
# project always builds arm64-apple-ios14.4 as its real, if unusual,
# stand-in freestanding Darwin/arm64 target -- see every build script's
# own CFLAGS -- not because it wants actual iOS semantics), not a bug in
# configd's real source. Confirmed by grep that none of this pass's 8
# target files reference any Keychain-preferences symbol -- real, empty,
# unused stand-in, not a stub of logic anything here calls.
#
# Written into a BOOTP-PRIVATE include dir, not the shared
# $OUT/compat_include, 2026-09-07 (DAR-263). It used to live in
# $OUT/compat_include/SystemConfiguration/, which
# build_libsystemconfiguration_client.sh also compiles against -- and
# because that script lists -I $OUT/compat_include BEFORE
# -I $OUT/sc_headers, this empty override silently shadowed the REAL
# SCPreferencesKeychainPrivate.h that its own DAR-240 work had
# deliberately stopped overriding. The visible cost was two real objects
# vanishing from libSystemConfiguration.a:
#
#   with this file in the shared dir: 59 compiled, 2 gaps
#     SCNetworkConfigurationInternal.c -- implicit declaration of
#       '_SCPreferencesSystemKeychainPasswordItemCopy'
#     SCNetworkInterface.c -- implicit declaration of
#       '_SCPreferencesSystemKeychainPasswordItemExists'
#   with it scoped here:             61 compiled, 0 gaps
#
# Nothing in that script's own run wrote the file, so the breakage
# appeared only after this script had ever run, and persisted until
# someone deleted the file by hand -- a cross-script ordering hazard
# with no ordering contract, exactly like os/log_private.h above.
# Keeping it private means this script still gets its empty override
# (which it genuinely needs -- SecKeychainRef is unavailable on this
# SDK target, see the write-up above) without being able to affect any
# sibling build.
mkdir -p "$OUT/bootp_compat_include/SystemConfiguration"
rm -f "$OUT/bootp_compat_include/SystemConfiguration/SCPreferencesKeychainPrivate.h"
cat > "$OUT/bootp_compat_include/SystemConfiguration/SCPreferencesKeychainPrivate.h" <<'EOF'
#ifndef	_SCPREFERENCESKEYCHAINPRIVATE_H
#define	_SCPREFERENCESKEYCHAINPRIVATE_H
#endif
EOF
# Belt and braces: if an older run of this script left the override in
# the shared directory, remove it, so a tree that has ever built the old
# way heals itself rather than silently keeping the 2-gap build.
rm -f "$OUT/compat_include/SystemConfiguration/SCPreferencesKeychainPrivate.h"

# Real IOKit header staging for dhcp.c (real, unmodified Apple source,
# 2026-09-01 DAR-170 "no shortcuts" decision: port real configd/
# SCDynamicStore/IPConfiguration, not a custom bypass -- but sleep/wake
# power-event scheduling specifically stays a narrow, honestly-documented
# forwarding stub, see dhcp_iopm_gap_compat.h). dhcp.c's own
# `#include <IOKit/...>` lines need a real IOKit/-shaped directory:
# IOMessage.h/IOKitKeys.h/IOTypes.h/OSMessageNotification.h are real and
# already live under that exact shape in third_party/xnu-7195/iokit/IOKit
# (shared kernel<->user headers -- reachable via a plain -I), but
# IOKitLib.h and pwr_mgt.subproj/IOPMLib.h+IOPMLibPrivate.h+IOPMKeys.h
# are IOKitUser-only (real user-framework headers, no IOKit/ wrapper
# directory in that repo's own source layout -- confirmed by ls, see
# build_iokituser.sh's own precedent for staging IOKit/-shaped compat
# dirs for the same reason). dhcp.c itself never calls any IOKitLib.h
# function directly (confirmed by grep) -- it only needs the header to
# satisfy IOPMLib.h's own `#include <IOKit/IOKitLib.h>`, and IOKitLib.h's
# own declarations only need real types (io_connect_t/io_object_t/
# io_service_t from IOKit/IOTypes.h, already reachable) plus the real
# public <dispatch/dispatch.h> headers this project's pinned SDK ships
# (declarations only -- no libdispatch .a is linked or needed since
# dhcp.c never calls a dispatch-taking IOKitLib function).
rm -rf "$OUT/iokit_headers/IOKit"
mkdir -p "$OUT/iokit_headers/IOKit/pwr_mgt" "$OUT/iokit_headers/IOKit/ps"
ln -sf "$(pwd)/$IOKITUSER/IOKitLib.h" "$OUT/iokit_headers/IOKit/IOKitLib.h"
for h in IOPMLib.h IOPMLibPrivate.h IOPMKeys.h; do
    ln -sf "$(pwd)/$IOKITUSER/pwr_mgt.subproj/$h" "$OUT/iokit_headers/IOKit/pwr_mgt/$h"
done
# IOPMLibPrivate.h also #includes <IOKit/pwr_mgt/IOPMLibDefs.h> and
# <IOKit/pwr_mgt/IOPM.h> -- both real, shared kernel/user headers that
# live in xnu-7195's iokit tree (not IOKitUser), already reachable via
# the plain `-I $XNU/iokit` in CFLAGS below -- and
# <IOKit/ps/IOPowerSources.h>, a real IOKitUser-only header (ps.subproj,
# 344 real lines, only #includes sys/cdefs.h + CoreFoundation.h, no
# heavier deps) staged the same way as IOPMLib.h above.
ln -sf "$(pwd)/$IOKITUSER/ps.subproj/IOPowerSources.h" "$OUT/iokit_headers/IOKit/ps/IOPowerSources.h"

# Real flags: same freestanding Libc-tier recipe as build_libc_regex.sh/
# build_libc_locale.sh, plus this tier's own -I paths -- bootplib/ first
# (dhcplib.c/dhcp_options.c/util.c/interfaces.c/dynarray.c's own home,
# and where bootp.c/bootp_session.c's local #include "x.h" resolve from
# since the real Makefile puts bootplib/ on the search path), then
# IPConfiguration.bproj itself (bootp_session.h etc), then the CF header
# staging dir for the two transitive CFString.h/CFDate.h pulls.
#
# -D__API_UNAVAILABLE_PLATFORM_bridgeos=bridgeos,unavailable: real, narrow
# SDK-version-skew fix, same class as build_libc_locale.sh's mach-o/
# dyld_priv.h stand-in comment ("real dyld_priv.h is from a newer
# checkout whose __API_UNAVAILABLE(...) invocations this project's
# Xcode-12/iPhoneOS14.4 Availability.h doesn't support"). Real, unmodified
# configd source (SCDynamicStoreCopySpecificPrivate.h) uses
# `API_UNAVAILABLE(ios, tvos, watchos, bridgeos)` -- a real, valid
# 4-platform invocation on a current SDK. This pinned Xcode-12 SDK's own
# AvailabilityInternal.h defines the sibling ios/tvos/watchos unavailable-
# platform macros (`__API_UNAVAILABLE_PLATFORM_ios ios,unavailable` etc,
# confirmed by grep) but is missing bridgeos's (bridgeOS support was
# added to Apple's Availability headers after this SDK's vintage) --
# leaving `__API_U(bridgeos)` token-paste to an undefined macro, which
# clang's availability-attribute parser can't parse ("expected ','").
# Defining it here, using the exact same `platform,unavailable` shape the
# other three already use, is a real, honest match for what a newer SDK's
# own header actually says -- not a fabricated value.
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__va_list=__builtin_va_list \
    -D__API_UNAVAILABLE_PLATFORM_bridgeos=bridgeos,unavailable \
    -DPRIVATE \
    `# bootp_compat_include FIRST: this script's own private overrides` \
    `# (SCPreferencesKeychainPrivate.h), deliberately not in the shared` \
    `# compat_include where they would shadow headers for sibling` \
    `# scripts -- see the DAR-263 write-up at that file's generation.` \
    -I $OUT/bootp_compat_include \
    -I $OUT/compat_include -I $OUT/cf_headers \
    -I $BOOTP/bootplib -I $BOOTP/IPConfiguration.bproj \
    -I $OUT/sc_headers -I $OUT/iokit_headers -I $XNU/iokit \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall \
    -I ../../generated"

# Real file set, extended 2026-09-01 past the original 9-file protocol-
# engine slice per DAR-170's "no shortcuts" plan step 1: dhcp.c (the
# real DISCOVER/OFFER/REQUEST/ACK state machine -- needs the IOKit/IOPM
# staging + dhcp_iopm_gap_compat.h narrow stub above, see that header's
# own comment) plus arp_session.c/ifutil.c/DHCPLease.c (flagged as "same
# os/log_private.h class of gap, no new blocker anticipated, but verify
# rather than assume" -- verified true by this pass, no new gap found).
FILES="$BOOTP/IPConfiguration.bproj/bootp.c $BOOTP/IPConfiguration.bproj/bootp_session.c \
    $BOOTP/bootplib/dhcplib.c $BOOTP/bootplib/dhcp_options.c $BOOTP/bootplib/util.c \
    $BOOTP/bootplib/interfaces.c $BOOTP/bootplib/dynarray.c $BOOTP/bootplib/udp_transmit.c \
    $BOOTP/bootplib/in_cksum.c $BOOTP/IPConfiguration.bproj/dhcp.c \
    $BOOTP/IPConfiguration.bproj/arp_session.c $BOOTP/IPConfiguration.bproj/ifutil.c \
    $BOOTP/IPConfiguration.bproj/DHCPLease.c"

ok=0
fail=0
for f in $FILES; do
    base=$(basename "$f" .c)
    out="$OUT/bootp_dhcp_engine/${base}.o"
    extra=""
    if [ "$base" = "dhcp" ]; then
        # Real, narrow forwarding stub for IOPMRequestSysWake()/
        # kIOPMPowerEventLeewayKey -- see dhcp_iopm_gap_compat.h's own
        # header comment for the full sourced reasoning. Force-included
        # only for dhcp.c since it's the one real caller.
        extra="-include dhcp_iopm_gap_compat.h"
    fi
    if "$CLANG" $CFLAGS $extra "$f" -o "$out" 2>"$OUT/bootp_dhcp_engine/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/bootp_dhcp_engine/${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OUT/bootp_dhcp_engine/${base}.err")"
    fi
done
echo "bootp_dhcp_engine: $ok compiled, $fail gaps (see libc_build/bootp_dhcp_engine/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi

rm -f "$OUT/bootp_dhcp_engine.a"
ar rcs "$OUT/bootp_dhcp_engine.a" "$OUT"/bootp_dhcp_engine/*.o
echo "wrote $OUT/bootp_dhcp_engine.a"

echo "real undefined symbols this archive still needs (informs the next"
echo "real step -- either already-built archives to link against, or the"
echo "new custom orchestrator that replaces ipconfigd.c's SC-coupled"
echo "service_set_address()/service_remove_address()/etc):"
nm -u "$OUT"/bootp_dhcp_engine/*.o | sort -u
