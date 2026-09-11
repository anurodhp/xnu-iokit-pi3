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
#	dnsinfo/dnsinfo_copy.c				`scutil --dns`
#	libSystemConfiguration/libSystemConfiguration_client.c
#							the generic XPC
#							connect-and-send helper
#							dnsinfo_copy.c and
#							network_information.c both
#							call. Apple compiles all
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
#	Libc/uuid/uuidsrc/clear.c			real uuid_clear(3), which
#							SCProxies.c calls. Its
#							canonical home is
#							libsystem_c.dylib (which
#							already owns the rest of
#							this family -- copy/parse/
#							unparse/pack); clear.c is
#							simply the one member that
#							has never been compiled
#							anywhere in this project.
#							Compiled locally rather
#							than rebuilding the dylib
#							every other image sits on
#							top of; filed separately.
#	xnu/libsyscall/mach/{mach_error,mach_error_string,fprintf_stderr,
#	                     string}.c			real mach_error()
#
# WHY dnsinfo_copy.c IS LINKED IN DIRECTLY (and not taken from libresolv)
# ======================================================================
# libresolv.dylib does export _dns_configuration_copy, but that copy is
# libresolv_dnsinfo_compat.c's stand-in, which unconditionally returns
# NULL. That was correct and honest when it was written (2026-08-2x):
# this project had no XPC, no launchd and no configd, so Apple's own
# "DNS configuration server not available" branch was the only reachable
# one. It is now STALE -- configd is resident, and the real dnsinfo XPC
# SERVER half (dnsinfo/dnsinfo_server.c + dnsinfo_create.c +
# dnsinfo_flatfile.c + libSystemConfiguration_server.c) is really linked
# into configd.macho by build_configd_plugins.sh. Binding scutil's
# `--dns` to the NULL stub would make this ticket's own verification
# report a fabricated "no DNS configuration" every time.
#
# So scutil compiles the real client, dnsinfo/dnsinfo_copy.c, into its
# own image: an object file on the link line satisfies the reference
# locally and no bind against libresolv is emitted at all (verified with
# `nm -m scutil.macho | grep dns_configuration`). libresolv keeps its own
# copy for its own res_init.c caller and is untouched -- deliberately, so
# this ticket does not rebuild a dylib that sits underneath
# libSystem.B.dylib and is boot-proven. Fixing libresolv for real is
# filed separately.
#
# STAND-INS (five files, each with its own full justification)
# ============================================================
#	scutil_editline_compat.c	libedit not built (real histedit.h
#					IS used, from the libedit-55 pin);
#					drives scutil's own real non-editing
#					fgets() input path
#	scutil_nc_compat.c		`--nc` VPN family; NetworkExtension
#					never open-sourced
#	scutil_reachability_compat.c	`-r`; SCNetworkReachability.c needs
#					libnetwork's never-open-sourced
#					<nw/private.h> types
#	scutil_migration_compat.c	`--net`'s `migrate`;
#					SCNetworkMigration.c compiles here but
#					needs 20 __private_extern__ internals
#					of libSystemConfiguration.dylib, i.e.
#					it belongs inside that dylib
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
    $CONFIGD/dnsinfo/dnsinfo_copy.c \
    $CONFIGD/libSystemConfiguration/libSystemConfiguration_client.c \
    $LIBC/stdlib/FreeBSD/getopt.c \
    $LIBC/stdlib/FreeBSD/getopt_long.c \
    $LIBC/gen/FreeBSD/signal.c \
    scutil_editline_compat.c \
    scutil_nc_compat.c \
    scutil_reachability_compat.c \
    scutil_migration_compat.c \
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

# Libc's uuid family needs one extra define the rest of this build does
# not: uuidP.h falls back to <uuid/uuid_types.h> (a header that exists in
# no SDK or pin here) unless HAVE_STDINT_H is set. Given its own compile
# step rather than adding -DHAVE_STDINT_H=1 to the shared CFLAGS above,
# so it cannot change how any of the 35 configd/Libc sources compile.
# Same define build_copyfile.sh already uses for this same family.
if "$CLANG" "${CFLAGS[@]}" -DHAVE_STDINT_H=1 "$LIBC/uuid/uuidsrc/clear.c" -o "$OBJ/clear.o" 2>"$OBJ/clear.err"; then
    ok=$((ok + 1))
    rm -f "$OBJ/clear.err"
else
    fail=$((fail + 1))
    echo "  gap: clear -- $(grep -m1 'error:' "$OBJ/clear.err")"
fi

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

# Real check that the "--dns is not silently bound to libresolv's
# always-NULL stand-in" reasoning in this script's header actually holds
# in the produced binary, rather than only in the comment.
if nm -m scutil.macho 2>/dev/null | grep -q "undefined.*_dns_configuration_copy"; then
    echo "error: scutil.macho binds _dns_configuration_copy to a dylib --" >&2
    echo "       expected the real dnsinfo_copy.c object to satisfy it locally." >&2
    echo "       'scutil --dns' would report a fabricated empty DNS configuration." >&2
    exit 1
fi

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
