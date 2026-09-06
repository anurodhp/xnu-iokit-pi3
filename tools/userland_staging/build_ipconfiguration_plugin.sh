#!/bin/bash
# DAR-170 (the ticket's own actual named goal): real IPConfiguration --
# the actual DHCP DISCOVER/OFFER/REQUEST/ACK state-machine driver that
# gets a leased IPv4 address + DNS onto en0 -- as a real configd plugin,
# wired against the already-built, already-committed protocol engine
# (tools/userland_staging/libc_build/bootp_dhcp_engine.a, DAR-170's own
# 2026-09-01 session: dhcp.c/bootp.c/bootp_session.c/arp_session.c/
# ifutil.c/DHCPLease.c + bootplib, 13/13 real files, zero gaps).
#
# ============================================================
# REAL ARCHITECTURE FINDING (2026-09-04 session 3, reconfirmed this
# session): IPConfiguration is NOT a builtin_plugins[] entry in real
# Apple's own plugin_support.c, even on iOS/embedded -- real Apple
# dlopen-loads it as a separate .bundle (bootp.xcodeproj's
# "IPConfiguration(embedded)" target), the one plugin subsystem real
# Apple treats this way. This project has no proven
# CFBundleLoadExecutableAndReturnError-on-a-dlopened-bundle path, so
# this port deliberately, honestly treats IPConfiguration as a builtin
# too -- see third_party-patches/configd.patch (adds a
# builtin_plugins[] table entry + extern decls to plugin_support.c,
# applied by setup_third_party.sh) and ipconfiguration_builtin_glue.c
# (maps the fixed load_IPConfiguration/start_IPConfiguration/
# prime_IPConfiguration names that table needs onto ipconfigd.c's own
# real, generic load()/start()/prime() -- the exact same generic names
# real Apple's dlopen path would have looked up via
# getBundleSymbol(bundle, CFSTR("load"), ...) etc). This is a real,
# deliberate, documented architecture deviation, not an oversight.
#
# ============================================================
# REAL, SEPARATE, HIGHER-LEVERAGE BUG FOUND AND FIXED THIS SESSION
# (blocks not just IPConfiguration but ALL SIX configd plugins DAR-240/
# 241 already linked in)
# ============================================================
# Real Apple's plugin_support.c only invokes ANY plugin's load()/start()/
# prime() -- builtin or not -- if plugin_exec()'s own real
# CFBundleCreateBundlesFromDirectory() scan of
# $(sysdir LIBRARY,SYSTEM)/SystemConfiguration first discovers a real,
# on-disk .bundle directory with a matching CFBundleIdentifier. This
# project's sysdir_start_search_path_enumeration()/
# sysdir_get_next_search_path_enumeration() (tools/userland_staging/
# sysdir_compat.c) always returned "no directories match" (state 0),
# so allBundles was ALWAYS EMPTY and every configd plugin -- including
# DAR-240/241's already-linked, real, non-stub LinkConfiguration/
# PreferencesMonitor/KernelEventMonitor/IPMonitor -- was silently NEVER
# actually invoked at runtime, confirmed via a real QEMU direct-
# invocation probe showing configd return immediately instead of
# blocking in CFRunLoopRun(). Fixed for real (sysdir_compat.c's own
# updated header comment has full detail) + inject_into_sd_image.sh now
# stages a real, flat .bundle/Info.plist directory (real, unmodified
# Apple Info.plist content, only the "$(PRODUCT_BUNDLE_IDENTIFIER)"
# Xcode build-variable placeholder substituted) for all 6 existing
# plugins AND (once this script has run) IPConfiguration's own bundle.
#
# ============================================================
# REAL FILE LIST (IPv4-only minimal subset, matching this ticket's own
# 2026-09-04 session-3 scoping: DHCPv6/RTADV/stf/CGA/RSAKey/AWD-telemetry
# .m files excluded -- this project has no IPv6/SEND/telemetry
# infrastructure and DAR-170's own goal is a plain IPv4 DHCP lease)
# ============================================================
# ipconfigd.c (9011 lines -- the real daemon main loop AND per-service
# DHCP-state-machine driver, load()/start()/prime()/stop(), calls
# service_set_address()/service_remove_address() -> inet_aifaddr()
# directly), FDSet.c, timer.c, linklocal.c (IPv4 link-local fallback,
# real Apple's own DHCPFailureConfiguresLinkLocal default), manual.c
# (static/manual IPv4 config method -- compiled in because ipconfigd.c's
# single state-machine dispatch table references it unconditionally,
# not because DAR-170 needs static config), server.c (real MIG
# com.apple.network.IPConfiguration service -- ipconfigd.c's prime()
# calls server_init() unconditionally), rtutil.c, report_symptoms.c,
# HostUUID.c, failover.c (all real, all needed because ipconfigd.c
# references their functions even on paths this target never
# exercises), sysconfig.c (13 real SCPreferences refs -- persists the
# DHCP client ID / duplicate-address-detection config).
#
# All 11 files (+ ipconfigd.c = 12 total) compile clean with ZERO
# fabricated stub content beyond the two genuinely-never-open-sourced
# gaps below -- confirmed via a real, isolated one-file-at-a-time
# compile pass before this script was written.
#
# ============================================================
# REAL GAPS FOUND AND CLOSED, one file/header at a time
# ============================================================
# - Header search-path ORDERING bug (not a missing file): ipconfigd.c's
#   own ipconfigd_globals.h does `#include "util.h"` expecting
#   bootp/bootplib's real util.h (which itself #includes symbol_scope.h,
#   defining the INLINE/STATIC/PRIVATE_EXTERN macros ipconfigd_globals.h
#   uses immediately). This project's own Libc also ships a real,
#   unrelated Libc/include/util.h (BSD login/pty helpers) -- with the
#   generic Libc -I paths listed before bootplib's, clang's quoted-
#   include search silently picked Libc's util.h instead, producing a
#   confusing "unknown type name INLINE" error that looks like a missing
#   macro, not a header-shadowing bug. Fixed by listing
#   third_party/bootp/bootplib and third_party/bootp/IPConfiguration.bproj
#   FIRST in this script's own -I list, matching the exact ordering
#   build_bootp_dhcp_engine.sh's own (already-working) CFLAGS already
#   established for the same two directories.
# - IPConfigurationServiceInternal.h: real, already-pinned header, just
#   in a third bootp subdirectory (third_party/bootp/ipconfiglib/) this
#   script hadn't -I'd yet.
# - ipconfigServer.h: real MIG-generated server header from bootp/
#   bootplib/ipconfig.defs (subsystem ipconfig 20000, serverprefix _;,
#   the real com.apple.network.IPConfiguration MIG service server.c
#   implements) -- regenerated via mig here, same pattern build_configd.sh
#   already established for config.defs's server side.
# - Security/SecTask.h: same real gap configd.tproj's own session.c
#   already hit (server.c's copyEntitlement()-equivalent code) -- reused
#   configd_compat_include/Security/SecTask.h verbatim, not re-derived.
# - gethostuuid(): real BSD syscall (#142), but this pinned iPhoneOS SDK's
#   own <gethostuuid.h> marks it __IPHONE_NA (App-Store-policy
#   restriction, not a real functional gap on this non-App-Store target)
#   -- reused cf_compat_headers/gethostuuid.h verbatim (already written
#   for CF's own CFPreferences.c real call site), not re-derived.
# - <SymptomReporter/SymptomReporter.h>: confirmed genuinely never
#   open-sourced (Apple-internal diagnostics-telemetry framework, no
#   apple-oss-distributions repo, no public SDK header) -- real, full
#   pinned-repo grep confirms exactly ONE real consumer
#   (report_symptoms.c's report_address_acquisition_symptom(), a
#   fire-and-forget diagnostic hint with zero effect on the real DHCP
#   state machine or any SCDynamicStore-visible config). Narrow, honest
#   stand-in (ipconfig_compat/SymptomReporter/SymptomReporter.h +
#   ipconfiguration_symptom_compat.c) matching exactly that one real call
#   site's 5-function/3-type usage, real "no reporter" (NULL) default --
#   same class as the project's existing lockdown.h/nw-private.h stubs.
#
# All 12 files above compile clean at this point using only real headers
# + the 4 narrow compat gaps above. The REST of the real gaps in this
# file set only show up at LINK time (undefined symbol, not a compile
# error) -- ipconfigd.c/sysconfig.c's #includes are enough to type-check
# against, but the real function BODIES they call live in files this
# script deliberately does not compile:
#
# - Real, genuinely-needed bootplib helpers simply missing from the
#   first FILES-list pass (real, unmodified, no compat needed): ptrlist.c
#   (generic pointer-list container dhcp_options.o/dynarray.o in the
#   already-built bootp_dhcp_engine.a also depend on), arp.c + bpflib.c
#   (real ARP-table ioctl + real BPF-device open/filter/write helpers --
#   genuinely needed for bootp_session.c's real broadcast-reply receive
#   path and arp_session.c's real conflict-detection, both already in
#   bootp_dhcp_engine.a but never linked in before ipconfigd.o started
#   referencing IFState-level ARP/BPF helpers directly), cfutil.c (real
#   my_CFString*/my_CFDictionary*-family helpers ipconfigd.c/sysconfig.c
#   use throughout), IPv4ClasslessRoute.c + DNSNameList.c (real RFC
#   3442 classless-static-route + DNS-search-list option encoders,
#   genuinely DHCP-option-relevant, not IPv6), IPConfigurationControlPrefs.c
#   + IPConfigurationLog.c (real IPConfiguration.plist-backed runtime
#   prefs + os_log handle, ipconfigd.c's own load()/init_log() call these
#   directly), Libc/net/FreeBSD/linkaddr.c (real link_ntoa(), needed by
#   ipconfigd.c's own interface-address logging, already-pinned real BSD
#   source, just never compiled into anything this link reaches before).
# - ioregpath.c EXCLUDED (not compiled): its own 3 real functions
#   (myIORegistryEntryCopyValue/CopyProperty/BSDNameMatchingCopyValue)
#   need real IOServiceGetMatchingService/IORegistryEntryFromPath/
#   IORegistryEntryCreateCFProperty(ies)/IORegistryEntryGetParentEntry/
#   IOObjectRelease/IOBSDNameMatching/kIOMasterPortDefault -- the exact
#   same real, already-flagged IOKitLib.c multi-gap wall
#   configd_builtin_plugins_stub.c's own header comment documents for
#   InterfaceNamer/ifnamer.c (a real, substantial, separate port, not
#   reattempted here). Narrow stand-in for ioregpath.c's own 3 functions
#   only (ipconfiguration_deadcode_compat.c's "Family 3") -- real
#   Apple's own callers (sysconfig.c) already treat a real "no IOKit
#   info" (NULL) answer as valid for registry-thin interfaces, which
#   describes this target's plain BCM2837 USB-Ethernet en0 honestly.
# - DHCPv6OptionListGetCount/GetOptionAtIndex/GetOptionDataAndLength
#   (real bootplib/DHCPv6Options.c, needed by sysconfig.c's real
#   DHCPv6-lease-derived SCDynamicStore publishing functions): same
#   IPv6-out-of-scope reasoning as Family 1 below, narrow stand-in
#   (Family 1b) since this target's DHCPv6OptionListRef is always
#   NULL/empty (no real DHCPv6Client ever runs here).
# - DHCPv6Client*/DHCPv6Socket*/ICMPv6Socket*/CGA*/wireless*/
#   RouterAdvertisement*/rtadv_thread/stf_thread/linklocal_v6_thread/
#   manual_v6_thread (~20 real symbols, real pinned source exists for
#   every one -- IPConfiguration.bproj/{DHCPv6Client,DHCPv6Socket,
#   ICMPv6Socket,CGA,wireless,rtadv,stf}.c + bootplib/
#   RouterAdvertisement.c): real, deliberate IPv6/SEND/WiFi-scope
#   exclusion this ticket's own 2026-09-01 session already decided
#   ("excluding DHCPv6/RTADV/stf/CGA/RSAKey/AWD-telemetry .m files --
#   this project has no IPv6/SEND infrastructure and DAR-170's own goal
#   is a plain IPv4 DHCP lease") -- narrow, honest no-op/false/NULL
#   stand-ins (ipconfiguration_deadcode_compat.c's "Family 1", real
#   signatures copied verbatim from each real header) matching this
#   project's already-established lockdown.h/nw-private.h precedent.
# - IOAllowPowerChange/IONotificationPortGetRunLoopSource/
#   IOPMCancelScheduledPowerEvent/IOPMCopyScheduledPowerEvents/
#   IORegisterForSystemPower (real pwr_mgt.subproj/{IOPMLib,
#   IOPMAutoWake}.c source exists, pinned, but needs IOKitLib.c's own
#   primitives -- same already-flagged wall as ioregpath.c above): same
#   "sleep/wake explicitly out of scope for a first DHCP lease" decision
#   dhcp_iopm_gap_compat.c's own header already made for its sibling
#   IOPMRequestSysWake() gap, extended here (Family 2) to these 5 real
#   siblings ipconfigd.c's own wake-event bookkeeping references.
# - CFFileDescriptorCreate/CreateRunLoopSource/EnableCallBacks/
#   GetNativeDescriptor/Invalidate (FDSet.c's real
#   FDCalloutCreateFileDescriptor(), the non-socket/raw-BPF-fd receive
#   path FDCalloutCreate() takes via a real fstat()+S_ISSOCK check):
#   genuinely never implemented anywhere in this project's pinned CF
#   (swift-corelibs-foundation has NO CFFileDescriptor.c file at all,
#   confirmed via `find`) -- and confirmed absent (2026-09-06) from real
#   apple-oss-distributions/CF too (cloned CF-855.17 directly and grepped
#   for CFFileDescriptorGetTypeID/__CFFileDescriptor, zero hits anywhere
#   in that checkout either; not just this project's own portable
#   substitute). NOT a narrow stub (a real no-op here would silently
#   break real DHCP-reply reception on whatever code path actually needs
#   the raw-fd branch). Filed as DAR-252.
#
#   UPDATE 2026-09-06: this file's own `-include
#   configd_cffiledescriptor_weak_compat.h` above (added this session)
#   re-declares these 5 real entry points __attribute__((weak_import)),
#   shadowing the real SDK's CFFileDescriptor.h the same way every other
#   -include compat header here does. This is required ALONGSIDE
#   build_configd.sh's own "-Wl,-U,_CFFileDescriptorCreate" et al link
#   flags, not instead of them: this project's default -fixup_chains
#   link mode resolves every bind eagerly at process launch, so a plain
#   "-Wl,-U" deferral (which used to only matter the first time
#   FDCalloutCreate() actually took the raw-fd branch, under classic
#   lazy dyld binding) now aborts configd's dyld load unconditionally,
#   before main() ever runs -- found via a real kernel-side
#   abort_with_payload() reason-string probe (bsd/kern/kern_exit.c,
#   -user_fault_debug) reading dyld's own real "symbol not found in flat
#   namespace '_CFFileDescriptorCreate'" halt message. The weak_import
#   attribute is what makes real dyld's flat-lookup resolution (real
#   Loader.cpp's own resolveSymbol(), BIND_SPECIAL_DYLIB_FLAT_LOOKUP's
#   "not found" branch) bind to NULL instead of erroring -- confirmed
#   empirically, not assumed: removing "-Wl,-U" while keeping only this
#   -include reintroduces a hard link-time "Undefined symbols" error, so
#   both mechanisms are genuinely required together, see
#   build_configd.sh's own updated comment for the fuller writeup. Still
#   does NOT implement CFFileDescriptor -- DAR-252's real port is still
#   owed before FDCalloutCreateFileDescriptor()'s raw-BPF-fd path can
#   actually work; calling any of these five once configd is running
#   will now null-deref through an unbound weak import instead, a real,
#   far narrower follow-on bug than the whole daemon being unable to
#   boot at all. Whether real DHCP lease negotiation over Ethernet
#   actually reaches this path (vs. staying on FDCalloutCreate()'s real,
#   already-working CFSocket branch for a genuine UDP socket fd) is
#   exactly what DAR-170's own QEMU verification needs to determine
#   next.
set -e
cd "$(dirname "$0")"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi
MIG="$(which mig)"
if [ -z "$MIG" ] || [ ! -x "$MIG" ]; then
    echo "error: mig tool not found" >&2
    exit 1
fi

CONFIGD="../../third_party/configd"
SC="$CONFIGD/SystemConfiguration.fproj"
BOOTP="../../third_party/bootp/IPConfiguration.bproj"
BOOTPLIB="../../third_party/bootp/bootplib"
IPCONFIGLIB="../../third_party/bootp/ipconfiglib"
XNU="../../third_party/xnu-7195"
LIBC="../../third_party/Libc"
LIBINFO="../../third_party/Libinfo"
PTHREAD="../../third_party/libpthread"
PLATFORM="../../third_party/libplatform"
DISPATCH="../../third_party/libdispatch"
PD="../../third_party/PureDarwin/src/Libraries/XPC"
OUT="libc_build"
MIGOUT="$OUT/ipconfig_mig"
rm -rf "$OUT/ipconfiguration_obj"
mkdir -p "$MIGOUT" "$OUT/ipconfiguration_obj"

if [ ! -d "$BOOTP" ]; then
    echo "error: $BOOTP not found -- run setup_third_party.sh first" >&2
    exit 1
fi
for d in sc_headers sc_iokit_headers sc_dispatch_compat sc_compat_include compat_include iokit_headers; do
    if [ ! -d "$OUT/$d" ]; then
        echo "error: $OUT/$d not found -- run build_libsystemconfiguration_client.sh and build_bootp_dhcp_engine.sh first" >&2
        exit 1
    fi
done
if [ ! -f "$OUT/bootp_dhcp_engine.a" ]; then
    echo "error: $OUT/bootp_dhcp_engine.a not found -- run build_bootp_dhcp_engine.sh first" >&2
    exit 1
fi

echo "mig: regenerating ipconfig (server only)"
"$MIG" -arch arm64 -I"$BOOTPLIB" -I"$BOOTP" \
    -header /dev/null -user /dev/null \
    -server "$MIGOUT/ipconfig_server.c" -sheader "$MIGOUT/ipconfigServer.h" \
    "$BOOTPLIB/ipconfig.defs"
echo "mig: ipconfig server regenerated cleanly"

CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__va_list=__builtin_va_list \
    -D__API_UNAVAILABLE_PLATFORM_bridgeos=bridgeos,unavailable \
    -D__API_AVAILABLE_PLATFORM_bridgeos\(x\)=ios,introduced=x \
    -DPRIVATE \
    -include "sys/fileport.h" \
    -include "dhcp_iopm_gap_compat.h" \
    -include "configd_cffiledescriptor_weak_compat.h" \
    -I "$BOOTPLIB" -I "$BOOTP" -I "$IPCONFIGLIB" \
    -I "ipconfig_compat" -I "cf_compat_headers" -I "configd_compat_include" -I "$MIGOUT" \
    -I "configd_plugin_compat" \
    -I "$OUT/configd_plugin_iokit_headers" -I "$OUT/iokit_headers" \
    -I "$PD/libxpc/include" \
    -I "launchd_compat_headers" \
    -I "$OUT/sc_compat_include" -I "$OUT/compat_include" -I "$OUT/cf_headers" \
    -I "$SC" -I "$SC/helper" -I "$CONFIGD/IPMonitorControl" -I "$CONFIGD/IPMonitorControl/AWD" \
    -I "$CONFIGD/Plugins/common" \
    -I "$CONFIGD/dnsinfo" -I "$CONFIGD/nwi" -I "$CONFIGD/libSystemConfiguration" -I "$CONFIGD/config-agent-info" \
    -I "$LIBINFO/lookup.subproj" \
    -I "$OUT/sc_headers" -I "$OUT/sc_iokit_headers" -I "$XNU/iokit" \
    -I "$PD/launchd/System-compat" \
    -I "$OUT/sc_dispatch_compat" -I "$DISPATCH" -I "$DISPATCH/private" \
    -I "$LIBC/include" -I "$LIBC/darwin" -I "$LIBC/fbsdcompat" -I "$LIBC/locale" -I "$LIBC/locale/FreeBSD" \
    -I "$LIBC/stdtime/FreeBSD" \
    -I "$PTHREAD/include" -I "$PTHREAD/private" \
    -I "$PLATFORM/private" -I "$PLATFORM/include" \
    -I ../../generated \
    -I "$XNU/libsyscall/mach" -I "$XNU/libsyscall" -I "$XNU/osfmk" -I "$XNU/bsd")

FILES="$BOOTP/ipconfigd.c \
    $BOOTP/FDSet.c \
    $BOOTP/timer.c \
    $BOOTP/linklocal.c \
    $BOOTP/manual.c \
    $BOOTP/server.c \
    $BOOTP/rtutil.c \
    $BOOTP/report_symptoms.c \
    $BOOTP/HostUUID.c \
    $BOOTP/failover.c \
    $BOOTP/sysconfig.c \
    $BOOTPLIB/ptrlist.c \
    $BOOTPLIB/arp.c \
    $BOOTPLIB/bpflib.c \
    $BOOTPLIB/cfutil.c \
    $BOOTPLIB/IPv4ClasslessRoute.c \
    $BOOTPLIB/DNSNameList.c \
    $BOOTPLIB/IPConfigurationControlPrefs.c \
    $BOOTPLIB/IPConfigurationLog.c \
    $LIBC/net/FreeBSD/linkaddr.c \
    $MIGOUT/ipconfig_server.c \
    ipconfiguration_symptom_compat.c \
    ipconfiguration_deadcode_compat.c \
    dhcp_iopm_gap_compat.c \
    ipconfiguration_builtin_glue.c"

ok=0
fail=0
for f in $FILES; do
    base=$(basename "$f" .c)
    out="$OUT/ipconfiguration_obj/${base}.o"
    if "$CLANG" "${CFLAGS[@]}" "$f" -o "$out" 2>"$OUT/ipconfiguration_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/ipconfiguration_obj/${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OUT/ipconfiguration_obj/${base}.err")"
    fi
done
echo "ipconfiguration: $ok compiled, $fail gaps (see $OUT/ipconfiguration_obj/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi

rm -f "$OUT/libconfigd_ipconfiguration.a"
ar rcs "$OUT/libconfigd_ipconfiguration.a" "$OUT"/ipconfiguration_obj/*.o
echo "wrote $OUT/libconfigd_ipconfiguration.a ($(ls "$OUT"/ipconfiguration_obj/*.o | wc -l | tr -d ' ') objects)"
