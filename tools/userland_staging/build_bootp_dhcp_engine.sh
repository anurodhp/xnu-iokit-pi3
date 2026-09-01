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
# THIS SCRIPT builds only the two cleanest files as the real first
# compile-only slice: bootp.c and bootp_session.c (BOOTP/DHCP packet
# construction + the BPF/socket session engine that sends/receives them)
# plus their real bootplib/ helper dependencies. Deliberately excludes
# for this pass (each a real, checked reason, not a scope guess):
#   - dhcp.c (the actual DISCOVER/OFFER/REQUEST/ACK state machine) --
#     real source is otherwise clean but #includes <IOKit/IOMessage.h>,
#     <IOKit/IOKitLib.h>, <IOKit/pwr_mgt/IOPMLib.h>, <IOKit/pwr_mgt/
#     IOPMLibPrivate.h> for sleep/wake handling. IOMessage.h/IOKitLib.h
#     are reachable via the already-pinned third_party/IOKitUser: the
#     two IOPMLib headers are NOT (real apple-oss-distributions
#     PowerManagement repo is cataloged in docs/apple-oss-catalog.md but
#     not yet pinned/cloned). Next real step for dhcp.c specifically:
#     either pin PowerManagement for those two headers, or (faster) a
#     narrow forwarding stub matching this project's existing
#     cf_compat_headers/gethostuuid.h pattern, since sleep/wake handling
#     is not needed for a first real DHCP lease.
#   - arp_session.c, ifutil.c, DHCPLease.c -- real source, zero
#     SCDynamicStore refs, but DHCPLease.c and arp_session.c pull in
#     <SystemConfiguration/SCValidation.h>/<SystemConfiguration/
#     SCPrivate.h> for real CFDictionary-based lease serialization / IPv4LL
#     ACD helpers. Left out of THIS pass's file list for scope, not
#     because of a hard blocker -- see the real SC-header finding below,
#     which applies equally to them.
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
# but a real userspace-appropriate compat shim for it (matching this
# project's existing os_log_compat.c/os/assumes.h precedent, likely just
# enough of os_log_create/os_log_error/os_log_t's declarations for
# SCPrivate.h to parse, almost certainly NOT functionally called by
# bootp.c/bootp_session.c) is not yet written. That shim is the
# concrete next step before this script reaches 9/9 compiled with zero
# gaps. Current real state: 5/9 files compile clean (dhcplib.c,
# dhcp_options.c, util.c, dynarray.c, in_cksum.c); bootp.c,
# bootp_session.c, interfaces.c, udp_transmit.c all fail on this one
# exact <os/log_private.h> gap (see libc_build/bootp_dhcp_engine/*.err).
#
# Real dependency ordering: needs libCoreFoundation.a's cf_headers
# staging dir (built by build_corefoundation.sh) and libc_locale.a/
# libmalloc.a/libplatform.a already built, same as every other tier.
#
# This is a compile-only (`-c`) first pass, matching build_libc_regex.sh's
# per-file-loop/gap-report template -- NOT a working DHCP client. No
# smoketest link is attempted yet: there is no real executable target
# for these two files alone (they're real protocol-engine building
# blocks a future custom orchestrator links against, not a standalone
# program). See DAR-170 for the full ordered plan and what comes next.
set -e
cd "$(dirname "$0")"

BOOTP="../../third_party/bootp"
CONFIGD="../../third_party/configd"
LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
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

# Real flags: same freestanding Libc-tier recipe as build_libc_regex.sh/
# build_libc_locale.sh, plus this tier's own -I paths -- bootplib/ first
# (dhcplib.c/dhcp_options.c/util.c/interfaces.c/dynarray.c's own home,
# and where bootp.c/bootp_session.c's local #include "x.h" resolve from
# since the real Makefile puts bootplib/ on the search path), then
# IPConfiguration.bproj itself (bootp_session.h etc), then the CF header
# staging dir for the two transitive CFString.h/CFDate.h pulls.
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__va_list=__builtin_va_list \
    -I $OUT/compat_include -I $OUT/cf_headers \
    -I $BOOTP/bootplib -I $BOOTP/IPConfiguration.bproj \
    -I $OUT/sc_headers \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall \
    -I ../../generated"

FILES="$BOOTP/IPConfiguration.bproj/bootp.c $BOOTP/IPConfiguration.bproj/bootp_session.c \
    $BOOTP/bootplib/dhcplib.c $BOOTP/bootplib/dhcp_options.c $BOOTP/bootplib/util.c \
    $BOOTP/bootplib/interfaces.c $BOOTP/bootplib/dynarray.c $BOOTP/bootplib/udp_transmit.c \
    $BOOTP/bootplib/in_cksum.c"

ok=0
fail=0
for f in $FILES; do
    base=$(basename "$f" .c)
    out="$OUT/bootp_dhcp_engine/${base}.o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>"$OUT/bootp_dhcp_engine/${base}.err"; then
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
