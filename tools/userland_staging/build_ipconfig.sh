#!/bin/bash
# DAR-170: real Apple ipconfig(8) -- the client half of IPConfiguration.
#
# WHY THIS EXISTS
# ===============
# build_ipconfiguration_plugin.sh already builds the real IPConfiguration
# *server* (ipconfigd.c's real DHCP DISCOVER/OFFER/REQUEST/ACK state machine,
# linked into configd.macho as a builtin plugin). Until now there was no way
# to talk to it, and no way to see what it thinks: every SC_log() inside it
# goes to os_log(3), which this project honestly no-ops (os_log_compat.c), and
# nothing on this target ever asked it to configure an interface.
#
# ipconfig(8) is real Apple's own answer to both problems, and it is the
# canonical one -- third_party/bootp/ipconfig.tproj/client.c, unmodified. It
# speaks the real ipconfig MIG subsystem (bootplib/ipconfig.defs, subsystem
# 20000) to the real `com.apple.SystemConfiguration.IPConfiguration` Mach
# service that IPConfiguration.bproj/server.c registers from inside configd:
#
#   ipconfig set en0 DHCP        -> ipconfig_set()   -> set_if()   (starts DHCP)
#   ipconfig getpacket en0       -> ipconfig_get_packet()          (the real ACK)
#   ipconfig getoption en0 <opt> -> ipconfig_get_option()
#   ipconfig getifaddr en0       -> ipconfig_if_addr()
#   ipconfig waitall             -> waits for all services to converge
#
# `ipconfig set en0 DHCP` is a real, first-class Apple entry point into the
# DHCP state machine that does NOT go through SCPreferences/SCDynamicStore at
# all (server.c:184 -> ipconfigd.c's set_if()), so it works today even though
# this port has no network *service* configured -- see the "REAL REMAINING
# GAP" note at the bottom of this comment.
#
# TOOLCHAIN
# =========
# Same setup as every other tools/userland_staging/build_*.sh: Xcode 12's
# iPhoneOS SDK, arm64-apple-ios14.4, clang invoked directly. The final link
# uses the daily-driver Xcode's `ld` (like build_configd.sh) because the
# Xcode-12 ld64 predates -fixup_chains.
#
# This is a real DYNAMIC executable (LC_LOAD_DYLINKER + real dylibs), NOT one
# of the four deliberately-static binaries (pwtool/bash/login/getty). It has
# to be: it links real libCoreFoundation.dylib and real
# libSystemConfiguration.dylib.
#
# REAL FILE LIST (all real, unmodified Apple source unless marked)
# ================================================================
#   ipconfig.tproj/client.c   the tool itself (1306 lines, compiles clean
#                             first try against this project's headers)
#   <mig>/ipconfig_user.c     regenerated here from bootplib/ipconfig.defs.
#                             build_ipconfiguration_plugin.sh regenerates the
#                             SERVER half of the same .defs; this is the
#                             matching client half, into its own
#                             libc_build/ipconfig_client_mig/ directory so the
#                             two never overwrite each other.
#   bootplib/                 dhcp_options.c dhcplib.c bsdplib.c DHCPv6.c
#                             DHCPv6Options.c DHCPDUID.c RouterAdvertisement.c
#                             cfutil.c util.c dynarray.c ptrlist.c
#                             DNSNameList.c IPv4ClasslessRoute.c
#                             IPConfigurationControlPrefs.c
#                             IPConfigurationLog.c
#                             (the real print/parse helpers client.c uses to
#                             decode a real DHCP packet -- 14/14 compile clean,
#                             zero gaps)
#   ipconfiglib/              IPConfigurationUtil.c
#   configd/SystemConfiguration.fproj/scprefs_observer.c
#                             (real dependency of IPConfigurationControlPrefs.c)
#   xnu/libsyscall/mach/      mach_error.c mach_error_string.c fprintf_stderr.c
#                             (real mach_error() reporting; client.c calls it on
#                             every MIG failure. Compiled with a REDUCED flag
#                             set on purpose -- see MACH_CFLAGS below.)
#
# Three compat objects, all pre-existing and reused as-is, none new to this
# script except the first:
#   ipconfig_cmd_ioregpath_compat.c  NEW. One function, NetBoot-only. Full
#                                    reasoning in the file's own header.
#   configd_sigaction_compat.c       DAR-263's real sigaction trampoline.
#   configd_plugin_log_scpreferences_compat.c
#                                    __log_SCPreferences, already written for
#                                    build_configd_plugins.sh.
#   configd_cfurl_filesize_compat.c  REQUIRED, not optional -- see below.
# plus `signal.o` extracted from the already-built liblaunchd_syscalls.a (real
# BSD signal(3)-over-sigaction wrapper; the same member build_syslogd.sh
# extracts for the same reason).
#
# WHY configd_cfurl_filesize_compat.c IS LINKED HERE TOO
# =======================================================
# EVERY binary that loads libSystemConfiguration.dylib needs it, not just
# configd.macho. That dylib carries two `-Wl,-U` FLAT-namespace undefined
# symbols (`_CFURLCopyResourcePropertyForKey`, `_kCFURLFileSizeKey`) which
# this project's DEPLOYMENT_RUNTIME_C CoreFoundation genuinely does not
# define (they live inside CFURL.c's `#if DEPLOYMENT_RUNTIME_SWIFT` block --
# full reasoning in configd_cfurl_filesize_compat.c's own header). dyld
# resolves a flat-namespace bind by searching every loaded image *including
# the main executable*, and under this project's eager -fixup_chains linking
# it aborts the whole process at load if none has it. The first real boot of
# this tool did exactly that:
#
#   -sh-3.2# ipconfig set en0 DHCP
#   dyld[14]: symbol not found in flat namespace '_CFURLCopyResourcePropertyForKey'
#   Abort trap: 6
#
# Worth knowing: none of this project's three link-time audits catch this.
# `nm -m` reported zero undefined-with-no-owner symbols, bind_target_audit.sh
# and gap_audit.sh were both clean, and the link itself was clean -- because
# the unresolved reference is not in this image at all, it is in a dylib this
# image loads. Passed as a loose .o (not via an archive) for the same reason
# build_configd.sh does: nothing in this binary's own object graph calls it,
# so ld64's archive-member selection would never pull it in.
#
# REAL REMAINING GAP THIS TOOL MAKES VISIBLE (not caused by it)
# ============================================================
# configd is resident with all 7 plugin bundles loaded, but
# /Library/Preferences/SystemConfiguration/preferences.plist on this target
# contains exactly one key -- `__VERSION__` -- i.e. there is no network
# service, no set, and therefore nothing for PreferencesMonitor to publish
# under Setup:/Network/Service/<id>/IPv4 and nothing for ipconfigd's
# handle_configuration_changed() to act on. Real Apple fills that file in from
# PreferencesMonitor's establishNewPreferences(), which only runs once
# InterfaceNamer publishes `Plugin:InterfaceNamer` with a Quiet/Timeout key
# (prefsmon.c storeCallback), and which then needs SCNetworkInterfaceCopyAll()
# -> IOServiceMatching() -> IOKit.framework, which this port does not have
# (dy_framework.c dlopen()s it and gets NULL; "could not get IOMasterPort,
# kr = 0x5" in /var/log/configd.log is that, working exactly as Apple designed
# it to degrade). `ipconfig set en0 DHCP` sidesteps that whole chain.
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
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE)" >&2
    exit 1
fi

CONFIGD="../../third_party/configd"
SC="$CONFIGD/SystemConfiguration.fproj"
BOOTP="../../third_party/bootp/IPConfiguration.bproj"
BOOTPLIB="../../third_party/bootp/bootplib"
IPCONFIGLIB="../../third_party/bootp/ipconfiglib"
IPCONFIGTPROJ="../../third_party/bootp/ipconfig.tproj"
XNU="../../third_party/xnu-7195"
LIBC="../../third_party/Libc"
LIBINFO="../../third_party/Libinfo"
PTHREAD="../../third_party/libpthread"
PLATFORM="../../third_party/libplatform"
DISPATCH="../../third_party/libdispatch"
PD="../../third_party/PureDarwin/src/Libraries/XPC"
OUT="libc_build"
MIGOUT="$OUT/ipconfig_client_mig"
OBJ="$OUT/ipconfig_cmd_obj"

if [ ! -d "$IPCONFIGTPROJ" ]; then
    echo "error: $IPCONFIGTPROJ not found -- run setup_third_party.sh first" >&2
    exit 1
fi
for d in sc_headers sc_iokit_headers sc_dispatch_compat sc_compat_include compat_include cf_headers iokit_headers configd_plugin_iokit_headers; do
    if [ ! -d "$OUT/$d" ]; then
        echo "error: $OUT/$d not found -- run build_libsystemconfiguration_client.sh," >&2
        echo "       build_bootp_dhcp_engine.sh and build_configd_plugins.sh first" >&2
        exit 1
    fi
done
for f in libmach_ipc.a chk_fail.o liblaunchd_syscalls.a; do
    if [ ! -f "$OUT/$f" ]; then
        echo "error: $OUT/$f not found -- run build_launchd_mig.sh / build_libmalloc.sh /" >&2
        echo "       build_launchd_syscalls.sh first" >&2
        exit 1
    fi
done
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
mkdir -p "$MIGOUT" "$OBJ"

# Client half of the same real ipconfig.defs build_ipconfiguration_plugin.sh
# generates the server half from. Separate output directory on purpose.
echo "mig: regenerating ipconfig (user only)"
"$MIG" -arch arm64 -I"$BOOTPLIB" -I"$BOOTP" \
    -header "$MIGOUT/ipconfig.h" -user "$MIGOUT/ipconfig_user.c" \
    -server /dev/null -sheader /dev/null \
    "$BOOTPLIB/ipconfig.defs"
echo "mig: ipconfig user stubs regenerated cleanly"

# Same real recipe as build_ipconfiguration_plugin.sh's own CFLAGS (that
# script's header comment documents each -D/-I); the only differences are the
# extra -I for ipconfig.tproj itself and this script's own MIGOUT. Notably
# there is NO -include configd_cffiledescriptor_weak_compat.h here: that
# header is DAR-252's weak-import workaround for the server side, and
# CFFileDescriptor is real now (libCoreFoundation.dylib exports all five
# entry points strong -- `nm -m configd.macho | grep CFFileDescriptor` shows
# real "(from libCoreFoundation)" binds), so nothing new should adopt it.
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
    -I "$BOOTPLIB" -I "$BOOTP" -I "$IPCONFIGLIB" -I "$IPCONFIGTPROJ" \
    -I "ipconfig_compat" -I "cf_compat_headers" -I "configd_compat_include" -I "$MIGOUT" \
    -I "configd_plugin_compat" \
    -I "$OUT/configd_plugin_iokit_headers" -I "$OUT/iokit_headers" \
    -I "$PD/libxpc/include" \
    -I "launchd_compat_headers" \
    -I "$OUT/sc_compat_include" -I "$OUT/compat_include" -I "$OUT/cf_headers" \
    -I "$SC" -I "$SC/helper" \
    -I "$CONFIGD/dnsinfo" -I "$CONFIGD/nwi" -I "$CONFIGD/libSystemConfiguration" \
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

FILES="$IPCONFIGTPROJ/client.c \
    $MIGOUT/ipconfig_user.c \
    $BOOTPLIB/dhcp_options.c \
    $BOOTPLIB/dhcplib.c \
    $BOOTPLIB/bsdplib.c \
    $BOOTPLIB/DHCPv6.c \
    $BOOTPLIB/DHCPv6Options.c \
    $BOOTPLIB/DHCPDUID.c \
    $BOOTPLIB/RouterAdvertisement.c \
    $BOOTPLIB/cfutil.c \
    $BOOTPLIB/util.c \
    $BOOTPLIB/dynarray.c \
    $BOOTPLIB/ptrlist.c \
    $BOOTPLIB/DNSNameList.c \
    $BOOTPLIB/IPv4ClasslessRoute.c \
    $BOOTPLIB/IPConfigurationControlPrefs.c \
    $BOOTPLIB/IPConfigurationLog.c \
    $IPCONFIGLIB/IPConfigurationUtil.c \
    $SC/scprefs_observer.c \
    ipconfig_cmd_ioregpath_compat.c \
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

# xnu's own libsyscall sources need a REDUCED flag set, not the one above:
# mach_error.c does `#include "string.h"` meaning libsyscall's own private
# string.h (which is what declares _mach_snprintf). The main CFLAGS above
# -include <sys/fileport.h>, which drags in the SDK's real <string.h> and
# defines _STRING_H_, so libsyscall's same-named header then compiles to
# nothing and _mach_snprintf is never declared. Dropping the -includes for
# these three files is the fix; they need none of what those headers provide.
MACH_CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin \
    -fno-stack-protector -O0 -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -DPRIVATE \
    -I "$XNU/libsyscall/mach" -I "$XNU/libsyscall" -I "$XNU/osfmk" -I "$LIBC/include")
for f in "$XNU/libsyscall/mach/mach_error.c" \
         "$XNU/libsyscall/mach/mach_error_string.c" \
         "$XNU/libsyscall/mach/fprintf_stderr.c" \
         `# string.c is libsyscall's own private _mach_snprintf/_mach_vsnprintf`\
         `# (the two symbols mach_error.c and fprintf_stderr.c call). Compiled`\
         `# with the same reduced flags, and safe to link here: with -fno-builtin`\
         `# and no -DMIG it defines exactly those two symbols and nothing else`\
         `# (verified with nm -g), so it cannot collide with libc's memcpy/`\
         `# memset/bzero the way this file can in a MIG build.`\
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

echo "ipconfig: $ok compiled, $fail gaps (see $OBJ/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi

# Real BSD signal(3) wrapper (client.c installs a SIGALRM handler for its
# -t/timeout paths). Same already-built archive member build_syslogd.sh
# extracts for the same reason -- extracted, not whole-archive-linked, because
# liblaunchd_syscalls.a's private_names_compat.o collides with libc's own
# __write alias (see build_syslogd.sh's own note).
( cd "$OBJ" && ar x "../liblaunchd_syscalls.a" signal.o )

rm -f ipconfig.macho
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o ipconfig.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OBJ"/*.o \
    "$OUT/libmach_ipc.a" "$OUT/chk_fail.o" \
    "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_blocks.dylib" \
    "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_c.dylib" \
    `# libcopyfile/libremovefile carry this project's real bcmp/rename (see`\
    `# each dylib's own build script); libinfo carries the real ether_aton`\
    `# client.c uses to parse a MAC on the command line.`\
    "$OUT/system/libcopyfile.dylib" "$OUT/system/libremovefile.dylib" \
    "$OUT/system/libinfo.dylib" \
    "$OUT/system/libCoreFoundation.dylib" \
    "$OUT/system/libSystemConfiguration.dylib" \
    "$OUT/system/libdispatch.dylib" "$OUT/system/libxpc.dylib" \
    "$OUT/system/libdyld.dylib" \
    "$OUT/system/libsystem_notify.dylib" \
    "$OUT/system/libsystem_m.dylib" "$OUT/system/libresolv.dylib" \
    2>"$OUT/ipconfig_link.err" \
    && echo "real link: 0 undefined symbols -- ipconfig.macho fully closed" \
    || { echo "real link failed:"; grep '^  "' "$OUT/ipconfig_link.err" | sed 's/,.*//' | sort -u; tail -40 "$OUT/ipconfig_link.err" >&2; exit 1; }

otool -l ipconfig.macho | grep -q LC_LOAD_DYLINKER || \
    { echo "error: ipconfig.macho has no LC_LOAD_DYLINKER -- it would not load" >&2; exit 1; }

real_gaps=$(nm -m ipconfig.macho 2>/dev/null | grep undefined | grep -v "(from " || true)
if [ -n "$real_gaps" ]; then
    echo "error: ipconfig.macho has undefined symbols with no owning dylib:" >&2
    echo "$real_gaps" >&2
    exit 1
fi

# Hard gates, same as build_syslogd.sh/build_configd.sh: a clean link and a
# clean nm do NOT prove a two-level bind resolves at runtime (CLAUDE.md).
./bind_target_audit.sh ipconfig.macho || {
    echo "error: ipconfig.macho has an unresolvable two-level bind -- it would abort at dyld load" >&2
    exit 1
}
./dead_stub_audit.sh ipconfig.macho || {
    echo "error: ipconfig.macho reaches a mach_ipc_dead_stubs.o stand-in" >&2
    exit 1
}

echo "wrote ipconfig.macho ($(wc -c < ipconfig.macho) bytes)"
echo "  install: tools/init_binary/inject_into_sd_image.sh puts it at /usr/sbin/ipconfig"
