#!/bin/bash
# DAR-283: real Apple scutil(8) -- the SCDynamicStore/SCPreferences
# read-back tool.
#
# WHY THIS EXISTS
# ===============
# DAR-170 got a real DHCP lease onto en0 under QEMU and proved it with
# real `ipconfig getpacket en0` (a real ACK carrying
# `domain_name_server (ip_mult): {10.0.2.3}`). What it could NOT prove is
# the next link in the chain: that configd's real IPMonitor plugin
# actually publishes that DNS server into the dynamic store.
#
# /etc/resolv.conf is legitimately absent on this target and is NOT the
# mechanism to check. Real Apple's Plugins/IPMonitor/ip_plugin.c guards
# the flat-file write:
#
#	#if	!TARGET_OS_IPHONE
#		/* update /etc/resolv.conf */
#		...
#	#endif	/* !TARGET_OS_IPHONE */
#
#		/* update State:/Network/Global/DNS */
#		keyChangeListSetValue(keys, S_state_global_dns, new_dict);
#
# and this port builds SDKROOT=iphoneos/PLATFORM=iPhoneOS, so
# TARGET_OS_IPHONE is 1: real Apple publishes DNS ONLY to the dynamic
# store here. Writing a resolv.conf would be inventing behaviour Apple
# does not ship on this target. scutil is Apple's own answer for reading
# the store back -- `scutil --dns`, or `show State:/Network/Global/DNS`
# from its command loop -- and it had simply never been ported.
#
# It is also a genuinely useful permanent diagnostic: every future
# configd/SCDynamicStore ticket (DAR-239, DAR-251, ...) gets easier with
# a real store browser on the image.
#
# TOOLCHAIN
# =========
# Same setup as every other tools/userland_staging/build_*.sh: Xcode 12's
# iPhoneOS SDK, arm64-apple-ios14.4, clang invoked directly. The final
# link uses the daily-driver Xcode's `ld` (like build_configd.sh and
# build_ipconfig.sh) because the Xcode-12 ld64 predates -fixup_chains.
#
# This is a real DYNAMIC executable (LC_LOAD_DYLINKER + real dylibs), NOT
# one of the four deliberately-static binaries (pwtool/bash/login/getty).
# It has to be: it links real libCoreFoundation.dylib and real
# libSystemConfiguration.dylib.
#
# REAL FILE LIST
# ==============
# The scutil.tproj file set is taken from real Apple's own
# `scutil-Embedded` PBXNativeTarget in configd.xcodeproj/project.pbxproj
# (the embedded/iOS-shaped target, which is the one whose TARGET_OS_IPHONE
# story matches this port), not invented:
#
#	scutil.c commands.c dictionary.c session.c cache.c notifications.c
#	tests.c prefs.c net.c net_interface.c net_protocol.c net_service.c
#	net_set.c nc.c + Plugins/common/IPMonitorControlPrefs.c
#
# 14 of those 15 are compiled here for real, unmodified, and all 14
# compile clean. `nc.c` is the one exception and is a real, provable
# architectural wall, not a shortcut -- see scutil_nc_compat.c's own
# header (NetworkExtension.framework's NEHelperClient.h has never been
# open-sourced; confirmed by a real `git ls-remote`, which 404s).
#
# Real supporting Apple source linked alongside, all unmodified:
#	SystemConfiguration.fproj/SCProxies.c		`scutil --proxy`
#	SystemConfiguration.fproj/scprefs_observer.c	real dependency of
#							IPMonitorControlPrefs.c
#	nwi/network_information.c			`scutil --nwi`
#	  (nwi/network_state_information_priv.c is deliberately NOT in the
#	  list: real Apple's nwi/network_state_information_logging.h
#	  `#include`s that .c file directly, so tests.c already carries its
#	  every symbol. Compiling it separately produces 12 real duplicate-
#	  symbol link errors -- found by a real trial link, and the reason
#	  real Apple's own scutil target does not list it either.)
#	libSystemConfiguration/libSystemConfiguration_client.c
#							the generic XPC
#							connect-and-send helper
#							network_information.c
#							(`scutil --nwi`) calls.
#							Apple compiles all
#							three into one dylib
#							(libsystem_configuration);
#							this project's
#							libSystemConfiguration.dylib
#							has it but keeps it hidden
#							(the file marks every entry
#							point __private_extern__,
#							so only the three
#							_libSC_info_fork_* handlers
#							are exported), so each
#							client image needs its own
#							copy -- confirmed with
#							`nm -gU`, not assumed.
#	Libc/stdlib/FreeBSD/getopt.c			real optarg/optind/optopt/
#							opterr/optreset. Needed
#							ALONGSIDE getopt_long.c,
#							which defines them only
#							under REPLACE_GETOPT --
#							a `#if 0`'d-out option in
#							Apple's own copy ("we
#							prefer to keep our
#							getopt(3)"). Same pairing
#							build_configd.sh already
#							uses.
#	Libc/stdlib/FreeBSD/getopt_long.c		real getopt_long(3)
#	Libc/gen/FreeBSD/signal.c			real signal(3)
#	xnu/libsyscall/mach/{mach_error,mach_error_string,fprintf_stderr,
#	                     string}.c			real mach_error()
#
# WHERE `scutil --dns` GETS dns_configuration_copy() FROM
# =======================================================
# From libresolv.dylib, for real, as of DAR-285 (2026-09-11).
#
# HISTORY, because the shape of this link looks odd without it: DAR-283
# originally compiled the real client, dnsinfo/dnsinfo_copy.c, into
# scutil.macho itself. libresolv.dylib did export _dns_configuration_copy
# back then, but that copy was libresolv_dnsinfo_compat.c's stand-in,
# which unconditionally returned NULL -- honest when written (no XPC, no
# launchd, no configd existed) and stale by the time configd was
# resident. Binding `--dns` to it would have reported a fabricated "no
# DNS configuration" every time, and rebuilding a dylib that sits
# underneath libSystem.B.dylib was deliberately out of that ticket's
# scope, so a private local copy was the right call then.
#
# DAR-285 put the real client into libresolv.dylib where it belongs (see
# build_libresolv_dylib.sh's own header), so the private duplicate is
# gone and scutil now binds _dns_configuration_copy to libresolv like any
# other consumer -- exactly one real implementation in the system. The
# gate further down asserts that direction, and would fail if this ever
# silently reverted to a local copy.
#
# libSystemConfiguration_client.c is still compiled in locally, and that
# is NOT the same situation: it is not a duplicate of anything reachable.
# Every one of its entry points except the three _libSC_info_fork_*
# atfork handlers is __private_extern__, so neither
# libSystemConfiguration.dylib nor libresolv.dylib exports it (confirmed
# with `nm -gU`); each client image genuinely needs its own copy, and
# scutil's `--nwi` path (nwi/network_information.c) really calls it.
#
# LINE EDITING: REAL libedit, AS OF DAR-286
# =========================================
# scutil.h #includes <histedit.h> unconditionally and scutil.c's
# interactive command loop really calls 9 libedit entry points. Until
# 2026-09-11 those were satisfied by scutil_editline_compat.c, a no-op
# stand-in that returned NULL/-1 so scutil took its OWN real,
# Apple-authored teardown branch and fell back to fgets() -- real, but
# with no arrow-key editing, no history and no ~/.editrc.
#
# DAR-286 built the real library (build_libedit.sh, real libedit-55
# source at full Apple WIDECHAR parity), so that file is GONE and this
# links real libedit.dylib -- exactly as real Apple does: configd.
# xcodeproj's own `scutil-Embedded` target's Frameworks build phase is
# CoreFoundation.framework + SystemConfiguration.framework +
# libedit.dylib. The gate at the end of this script asserts scutil binds
# _el_init to libedit and carries no private copy, so a silent revert to
# a local stand-in fails the build.
#
# `migrate` AND uuid_clear: REAL, AS OF DAR-287
# =============================================
# Two things this build used to carry privately now come from the dylibs
# that own them, and neither is a stand-in any more.
#
# SCNetworkMigration.c (the `--net` `migrate` command, plus the
# _SCNetworkConfigurationCheckValidity* family) is real, pinned Apple
# source that compiles fine here but cannot LINK here: a real trial link
# during DAR-283 found 20 undefined symbols, all __private_extern__
# internals of libSystemConfiguration.dylib's own sibling files. That is
# Apple's architecture, not a defect -- the file is private to the
# framework it lives in. DAR-283 therefore shipped
# scutil_migration_compat.c, whose two functions returned NULL/FALSE.
# DAR-287 put the real file inside the dylib
# (build_libsystemconfiguration_client.sh's FILES list), so that stand-in
# is deleted and scutil binds the real implementations.
#
# uuid_clear(3) (SCProxies.c's only use of the uuid family) was compiled
# here from Libc/uuid/uuidsrc/clear.c, because that one member of the
# family had never been built anywhere in this project -- libsystem_c.dylib
# already exported uuid_copy/uuid_pack/uuid_parse/uuid_unparse{,_lower,
# _upper} and nothing else defined _uuid_clear at all. DAR-287 added it
# to build_libsystem_c.sh, its real canonical home, so the private copy
# is gone too.
#
# Both directions are asserted by gates at the end of this script, for
# the same reason the _dns_configuration_copy and libedit gates exist: a
# reintroduced local copy would link and run perfectly happily while
# silently shadowing the real implementation.
#
# STAND-INS (three files, each with its own full justification)
# ============================================================
#	scutil_nc_compat.c		`--nc` VPN family; NetworkExtension
#					never open-sourced
#	scutil_reachability_compat.c	`-r`; SCNetworkReachability.c needs
#					libnetwork's never-open-sourced
#					<nw/private.h> types
#	scutil_misc_compat.c		one NetworkExtension AOVPN predicate
#					(which BUYS the real SCProxies.c) +
#					a _write$UNIX2003 name bridge
#
# Plus three pre-existing compat objects reused as-is:
#	configd_sigaction_compat.c		DAR-263's real sigaction trampoline
#	configd_plugin_log_scpreferences_compat.c   __log_SCPreferences
#	configd_cfurl_filesize_compat.c		REQUIRED for every binary that
#						loads libSystemConfiguration.dylib
#						(two -Wl,-U flat-namespace
#						undefineds; see
#						build_ipconfig.sh's own writeup
#						-- none of the three link-time
#						audits catch this one)
set -e
cd "$(dirname "$0")"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE)" >&2
    exit 1
fi

CONFIGD="../../third_party/configd"
SC="$CONFIGD/SystemConfiguration.fproj"
SCUTIL="$CONFIGD/scutil.tproj"
LIBEDIT="../../third_party/libedit"
IOUSBFAMILY="../../third_party/IOUSBFamily/IOUSBFamily/Headers"
XNU="../../third_party/xnu-7195"
LIBC="../../third_party/Libc"
LIBINFO="../../third_party/Libinfo"
PTHREAD="../../third_party/libpthread"
PLATFORM="../../third_party/libplatform"
DISPATCH="../../third_party/libdispatch"
PD="../../third_party/PureDarwin/src/Libraries/XPC"
CR="../../third_party/PureDarwin/src/Libraries/CrashReporterClient"
OUT="libc_build"
OBJ="$OUT/scutil_obj"

if [ ! -d "$SCUTIL" ]; then
    echo "error: $SCUTIL not found -- run setup_third_party.sh first" >&2
    exit 1
fi
# Real, unmodified Apple <histedit.h> at the macos-112 manifest's own
# libedit-55 tag. scutil.h #includes it unconditionally and the pinned
# Xcode-12 iPhoneOS SDK ships no copy at all, so this is a hard
# prerequisite, not an optimisation.
if [ ! -f "$LIBEDIT/src/histedit.h" ]; then
    echo "error: $LIBEDIT/src/histedit.h not found -- run setup_third_party.sh first" >&2
    exit 1
fi
# DAR-286: real libedit.dylib behind that header. Named separately from
# the generic dylib loop below on purpose -- gen_build_order.py only
# reads the literal "run build_X.sh first" idiom, and the loop's generic
# message does not use it.
if [ ! -f "$OUT/system/libedit.dylib" ]; then
    echo "error: $OUT/system/libedit.dylib not found -- run build_libedit.sh first" >&2
    exit 1
fi
for d in sc_headers sc_iokit_headers sc_dispatch_compat sc_compat_include compat_include cf_headers configd_plugin_iokit_headers; do
    if [ ! -d "$OUT/$d" ]; then
        echo "error: $OUT/$d not found -- run build_libsystemconfiguration_client.sh" >&2
        echo "       and build_configd_plugins.sh first" >&2
        exit 1
    fi
done
if [ ! -f "$OUT/chk_fail.o" ]; then
    echo "error: $OUT/chk_fail.o not found -- run build_libmalloc.sh first" >&2
    exit 1
fi
for d in libsystem_kernel libsystem_platform libsystem_blocks libsystem_pthread \
         libsystem_malloc libsystem_c libcopyfile libremovefile libinfo \
         libCoreFoundation libSystemConfiguration libdispatch libxpc libdyld \
         libsystem_notify libsystem_m libresolv; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- build the dylib stack first" >&2
        exit 1
    fi
done
# Named explicitly, separately from the loop above, because as of DAR-285
# this is a HARD prerequisite with a real ordering requirement, not just
# "some dylib must exist": scutil no longer carries its own copy of
# dnsinfo_copy.c, and the gate at the end of this script fails unless
# _dns_configuration_copy really binds libresolv. gen_build_order.py only
# reads the literal "run build_X.sh first" idiom, which the loop's own
# generic message above does not use.
if [ ! -f "$OUT/system/libresolv.dylib" ]; then
    echo "error: $OUT/system/libresolv.dylib not found -- run build_libresolv_dylib.sh first" >&2
    exit 1
fi

rm -rf "$OBJ"
mkdir -p "$OBJ"

# Same real recipe as build_configd_plugins.sh / build_ipconfig.sh (those
# scripts' headers document each -D/-I). scutil-specific additions:
#   -I $LIBEDIT/src            real histedit.h (see above)
#   -I $SCUTIL                 scutil's own headers
#   -I $CONFIGD/Plugins/IPMonitor
#                              configAgentDefines.h, which nwi/
#                              network_information.c really #includes and
#                              which really lives under Plugins/IPMonitor/
#                              in the pinned tree (found by a real
#                              filesystem search, not guessed)
#   -I $CONFIGD/Plugins/common IPMonitorControlPrefs.h (prefs.c)
#   -I $LIBINFO/lookup.subproj netdb_async.h (tests.c)
#   -I configd_plugin_compat   nw/private.h types-only stand-in, which
#                              tests.c reaches via
#                              SCNetworkReachabilityInternal.h
#   -include IOKit/usb/USBSpec_ifnamer_compat.h
#                              kIOUSBAppleVendorID etc, which
#                              SCNetworkMigration.c really uses (same
#                              -include build_libsystemconfiguration_client.sh
#                              already needs for the same reason)
#   -include scutil_ne_aovpn_compat.h
#                              SCProxies.c's NetworkExtension predicate
CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__va_list=__builtin_va_list \
    -D__API_UNAVAILABLE_PLATFORM_bridgeos=bridgeos,unavailable \
    -D__API_AVAILABLE_PLATFORM_bridgeos\(x\)=ios,introduced=x \
    -DPRIVATE \
    -include "sys/fileport.h" \
    -include "IOKit/usb/USBSpec_ifnamer_compat.h" \
    -include "scutil_ne_aovpn_compat.h" \
    `# DAR-263's real _sigaction() trampoline declaration -- Libc's own
    # gen/FreeBSD/signal.c (real signal(3), which tests.c uses for its
    # -W watch loops) calls _sigaction() directly, exactly as
    # build_configd.sh already needs for the same real reason.` \
    -include "configd_sigaction_compat.h" \
    -I "scutil_compat" \
    -I "configd_plugin_compat" \
    -I "$OUT/configd_plugin_iokit_headers" \
    -I "launchd_compat_headers" \
    -I "$PD/libxpc/include" \
    -I "$LIBEDIT/src" \
    -I "$SCUTIL" \
    -I "$OUT/sc_compat_include" -I "$OUT/compat_include" -I "$OUT/cf_headers" \
    -I "$SC" -I "$SC/helper" -I "$CONFIGD/IPMonitorControl" -I "$CONFIGD/IPMonitorControl/AWD" \
    -I "$CONFIGD/Plugins/common" -I "$CONFIGD/Plugins/IPMonitor" \
    -I "$CONFIGD/dnsinfo" -I "$CONFIGD/nwi" -I "$CONFIGD/libSystemConfiguration" -I "$CONFIGD/config-agent-info" \
    -I "$LIBINFO/lookup.subproj" \
    -I "$OUT/sc_headers" -I "$OUT/sc_iokit_headers" -I "$IOUSBFAMILY" -I "$XNU/iokit" \
    -I "$PD/launchd/System-compat" \
    -I "$OUT/sc_dispatch_compat" -I "$DISPATCH" -I "$DISPATCH/private" \
    -I "$CR/include" \
    -I "$LIBC/include" -I "$LIBC/darwin" -I "$LIBC/fbsdcompat" -I "$LIBC/locale" -I "$LIBC/locale/FreeBSD" \
    -I "$LIBC/stdtime/FreeBSD" \
    -I "$PTHREAD/include" -I "$PTHREAD/private" \
    -I "$PLATFORM/private" -I "$PLATFORM/include" \
    -I ../../generated \
    -I "$XNU/libsyscall/mach" -I "$XNU/libsyscall" -I "$XNU/osfmk" -I "$XNU/bsd")

FILES="$SCUTIL/scutil.c \
    $SCUTIL/commands.c \
    $SCUTIL/dictionary.c \
    $SCUTIL/session.c \
    $SCUTIL/cache.c \
    $SCUTIL/notifications.c \
    $SCUTIL/tests.c \
    $SCUTIL/prefs.c \
    $SCUTIL/net.c \
    $SCUTIL/net_interface.c \
    $SCUTIL/net_protocol.c \
    $SCUTIL/net_service.c \
    $SCUTIL/net_set.c \
    $CONFIGD/Plugins/common/IPMonitorControlPrefs.c \
    $SC/SCProxies.c \
    $SC/scprefs_observer.c \
    $CONFIGD/nwi/network_information.c \
    $CONFIGD/libSystemConfiguration/libSystemConfiguration_client.c \
    $LIBC/stdlib/FreeBSD/getopt.c \
    $LIBC/stdlib/FreeBSD/getopt_long.c \
    $LIBC/gen/FreeBSD/signal.c \
    scutil_nc_compat.c \
    scutil_reachability_compat.c \
    scutil_misc_compat.c \
    configd_sigaction_compat.c \
    configd_plugin_log_scpreferences_compat.c \
    configd_cfurl_filesize_compat.c"

ok=0
fail=0
for f in $FILES; do
    base=$(basename "$f" .c)
    if "$CLANG" "${CFLAGS[@]}" "$f" -o "$OBJ/${base}.o" 2>"$OBJ/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OBJ/${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OBJ/${base}.err")"
    fi
done

# DAR-287 (2026-09-11): Libc/uuid/uuidsrc/clear.c used to be compiled
# here, with its own -DHAVE_STDINT_H=1 step, because _uuid_clear (which
# SCProxies.c really calls) existed nowhere in this project. It now lives
# in libsystem_c.dylib, which already owned every other member of that
# family, so the private copy is gone and scutil binds the real one. The
# gate further down asserts that direction.

# xnu's own libsyscall sources need a REDUCED flag set, not the one above
# -- identical reasoning to build_ipconfig.sh's own MACH_CFLAGS block:
# mach_error.c does `#include "string.h"` meaning libsyscall's own private
# string.h (which declares _mach_snprintf), and the main CFLAGS above
# -include <sys/fileport.h>, which drags in the SDK's real <string.h>,
# defines _STRING_H_, and makes libsyscall's same-named header compile to
# nothing.
MACH_CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin \
    -fno-stack-protector -O0 -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -DPRIVATE \
    -I "$XNU/libsyscall/mach" -I "$XNU/libsyscall" -I "$XNU/osfmk" -I "$LIBC/include")
for f in "$XNU/libsyscall/mach/mach_error.c" \
         "$XNU/libsyscall/mach/mach_error_string.c" \
         "$XNU/libsyscall/mach/fprintf_stderr.c" \
         "$XNU/libsyscall/mach/string.c"; do
    base=$(basename "$f" .c)
    if "$CLANG" "${MACH_CFLAGS[@]}" "$f" -o "$OBJ/${base}.o" 2>"$OBJ/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OBJ/${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OBJ/${base}.err")"
    fi
done

echo "scutil: $ok compiled, $fail gaps (see $OBJ/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi

rm -f scutil.macho
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o scutil.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OBJ"/*.o \
    "$OUT/chk_fail.o" \
    "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_blocks.dylib" \
    "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_c.dylib" \
    `# libcopyfile/libremovefile carry this project's real bcmp/rename` \
    `# (see each dylib's own build script)` \
    "$OUT/system/libcopyfile.dylib" "$OUT/system/libremovefile.dylib" \
    "$OUT/system/libinfo.dylib" \
    "$OUT/system/libCoreFoundation.dylib" \
    "$OUT/system/libSystemConfiguration.dylib" \
    "$OUT/system/libdispatch.dylib" "$OUT/system/libxpc.dylib" \
    "$OUT/system/libdyld.dylib" \
    "$OUT/system/libsystem_notify.dylib" \
    "$OUT/system/libsystem_m.dylib" "$OUT/system/libresolv.dylib" \
    `# DAR-286: real libedit, listed LAST on purpose. Its own transitive` \
    `# LC_LOAD_DYLIB closure includes libncurses/libinfo/libcopyfile, so` \
    `# listing it earlier would let ld64 resolve an unrelated libc symbol` \
    `# THROUGH libedit and record "bind libedit/<sym>" -- which real` \
    `# dyld-940 cannot resolve, since libedit plain-links those rather` \
    `# than LC_REEXPORT_DYLIBing them (CLAUDE.md's bind_target_audit.sh` \
    `# writeup; DAR-217 is the same bug one dylib over). Nothing else` \
    `# here defines el_*/history_*, so last is both safe and correct.` \
    "$OUT/system/libedit.dylib" \
    2>"$OUT/scutil_link.err" \
    && echo "real link: 0 undefined symbols -- scutil.macho fully closed" \
    || { echo "real link failed:"; grep '^  "' "$OUT/scutil_link.err" | sed 's/,.*//' | sort -u; tail -40 "$OUT/scutil_link.err" >&2; exit 1; }

otool -l scutil.macho | grep -q LC_LOAD_DYLINKER || \
    { echo "error: scutil.macho has no LC_LOAD_DYLINKER -- it would not load" >&2; exit 1; }

real_gaps=$(nm -m scutil.macho 2>/dev/null | grep undefined | grep -v "(from " || true)
if [ -n "$real_gaps" ]; then
    echo "error: scutil.macho has undefined symbols with no owning dylib:" >&2
    echo "$real_gaps" >&2
    exit 1
fi

# DAR-285 inverted this gate. It used to assert the OPPOSITE (that
# _dns_configuration_copy was satisfied locally, never bound to a dylib),
# because libresolv.dylib's copy was an always-NULL stand-in at the time.
# libresolv now carries the real Apple dnsinfo XPC client, so the correct
# invariant is that scutil binds to it and carries no private duplicate.
# Both halves are checked: a local definition would silently shadow the
# real one again, and no bind at all would mean the reference vanished.
if nm -gU scutil.macho 2>/dev/null | grep -q " T _dns_configuration_copy\$"; then
    echo "error: scutil.macho DEFINES _dns_configuration_copy locally -- DAR-285 moved" >&2
    echo "       the real dnsinfo client into libresolv.dylib; a private copy here is" >&2
    echo "       a duplicate that shadows it (and carries its own XPC connection state)." >&2
    exit 1
fi
if ! nm -m scutil.macho 2>/dev/null | grep -q "undefined.*_dns_configuration_copy"; then
    echo "error: scutil.macho has no _dns_configuration_copy reference at all --" >&2
    echo "       'scutil --dns' cannot be reading the real DNS configuration." >&2
    exit 1
fi
if ! nm -m scutil.macho 2>/dev/null | grep "_dns_configuration_copy" | grep -q "libresolv"; then
    echo "error: scutil.macho's _dns_configuration_copy does not bind libresolv --" >&2
    echo "       real dyld-940 searches only the listed image (see CLAUDE.md's" >&2
    echo "       bind_target_audit.sh writeup); it would abort at load." >&2
    nm -m scutil.macho | grep "_dns_configuration_copy" >&2
    exit 1
fi

# DAR-286 gate, same shape as the _dns_configuration_copy one above: the
# 9 libedit entry points scutil calls must bind the real libedit.dylib
# and must NOT be satisfied by a local definition. A reintroduced
# scutil_editline_compat.c would link and run perfectly happily while
# silently disabling line editing again -- that is exactly the state this
# ticket removed, so it is checked rather than trusted.
for s in _el_init _el_end _el_gets _el_set _el_get _el_source \
         _history_init _history_end _history; do
    if nm -gU scutil.macho 2>/dev/null | grep -qE "^[0-9a-f]+ T $s\$"; then
        echo "error: scutil.macho DEFINES $s locally -- DAR-286 moved the real" >&2
        echo "       implementation into libedit.dylib; a private copy shadows it" >&2
        echo "       and puts scutil back on its no-editing fgets() fallback." >&2
        exit 1
    fi
    if ! nm -m scutil.macho 2>/dev/null | grep -F "external $s (from " | grep -q "libedit"; then
        echo "error: scutil.macho's $s does not bind libedit.dylib:" >&2
        nm -m scutil.macho 2>/dev/null | grep -F "external $s (from " >&2
        exit 1
    fi
done
echo "real libedit: all 9 entry points bind /usr/lib/libedit.3.dylib"

# DAR-286: libedit.dylib carries exactly ONE flat-namespace deferral,
# _fork (see build_libedit.sh's header for the two real reasons it cannot
# live inside a dylib here). An unresolved flat-namespace bind really
# does abort dyld-940 at load -- a first attempt at this ticket deferred
# six symbols and scutil died with
#   dyld[10]: symbol not found in flat namespace '_execlp'
# on a real boot. So this is checked rather than assumed: some image
# scutil actually loads must really export _fork.
# Matched by real LC_ID_DYLIB install_name, not by filename: several
# dylibs here ship a versioned install_name whose basename is NOT their
# built filename (libncurses.dylib -> /usr/lib/libncurses.5.4.dylib,
# libresolv.dylib -> /usr/lib/libresolv.9.dylib), so a basename compare
# would silently skip them and could report a false "no owner".
SCUTIL_LOADS=$(otool -L scutil.macho | tail -n +2 | awk '{print $1}')
FORK_OWNER=""
for cand in "$OUT"/system/*.dylib; do
    iname=$(otool -D "$cand" 2>/dev/null | tail -n +2)
    [ -n "$iname" ] || continue
    echo "$SCUTIL_LOADS" | grep -qxF "$iname" || continue
    if nm -gU "$cand" 2>/dev/null | grep -qE "^[0-9a-f]+ T _fork\$"; then
        FORK_OWNER="$iname"
        break
    fi
done
if [ -z "$FORK_OWNER" ]; then
    echo "error: no dylib scutil.macho loads exports _fork, but libedit.dylib" >&2
    echo "       defers it to the flat namespace -- scutil would abort at dyld" >&2
    echo "       load with \"symbol not found in flat namespace '_fork'\"." >&2
    exit 1
fi
echo "real libedit: its one _fork flat-namespace deferral resolves via $FORK_OWNER"

# DAR-287 gates, same shape as the two above. Both symbol families used
# to be satisfied by a private local copy in this binary; each now has to
# come from the dylib that owns it, and a silent revert to a local
# definition has to fail the build rather than pass it.
#
#   _uuid_clear                            -> libsystem_c.dylib
#   __SCNetworkConfigurationPerformMigration,
#   __SCNetworkMigrationAreConfigurationsIdentical
#                                          -> libSystemConfiguration.dylib
# (two leading underscores: the C names are _SCNetworkConfiguration... --
# already underscore-prefixed by Apple -- so the Mach-O symbols carry a
# second one.)
#
# The libSystemConfiguration pair is checked by real name, not by the
# stand-in's filename, because scutil_migration_compat.c defined exactly
# these two and nothing else -- so "defined locally" is precisely the
# failure state DAR-287 removed.
check_owner() {
    local sym="$1" owner="$2"
    if nm -gU scutil.macho 2>/dev/null | grep -qE "^[0-9a-f]+ T ${sym}\$"; then
        echo "error: scutil.macho DEFINES $sym locally -- DAR-287 moved the real" >&2
        echo "       implementation into $owner; a private copy shadows it." >&2
        exit 1
    fi
    if ! nm -m scutil.macho 2>/dev/null | grep -F "external $sym (from " | grep -q "$owner"; then
        echo "error: scutil.macho's $sym does not bind $owner:" >&2
        nm -m scutil.macho 2>/dev/null | grep -F "external $sym (from " >&2
        exit 1
    fi
}
check_owner _uuid_clear libsystem_c
check_owner __SCNetworkConfigurationPerformMigration libSystemConfiguration
check_owner __SCNetworkMigrationAreConfigurationsIdentical libSystemConfiguration
echo "real uuid_clear + real SCNetworkMigration: all 3 bind their owning dylib"

# Hard gates, same as build_syslogd.sh/build_configd.sh/build_ipconfig.sh:
# a clean link and a clean nm do NOT prove a two-level bind resolves at
# runtime (CLAUDE.md).
./bind_target_audit.sh scutil.macho || {
    echo "error: scutil.macho has an unresolvable two-level bind -- it would abort at dyld load" >&2
    exit 1
}
./dead_stub_audit.sh scutil.macho || {
    echo "error: scutil.macho reaches a mach_ipc_dead_stubs.o stand-in" >&2
    exit 1
}

echo "wrote scutil.macho ($(wc -c < scutil.macho) bytes)"
echo "  install: tools/init_binary/inject_into_sd_image.sh puts it at /usr/sbin/scutil"
