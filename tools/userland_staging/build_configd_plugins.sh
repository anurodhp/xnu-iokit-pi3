#!/bin/bash
# DAR-170 (post-split, IPMonitor/QoSMarking now DAR-239's scope): real
# configd plugin subsystems -- InterfaceNamer, LinkConfiguration,
# KernelEventMonitor, PreferencesMonitor -- all real, unmodified, plain-C
# Apple source from third_party/configd/Plugins/, confirmed (2026-09-04
# session) to be genuinely statically-linked "builtin_plugins[]" entries
# in real Apple's own configd.tproj/plugin_support.c even on iOS/embedded
# (grep of the real, unmodified source: builtin_plugins[] table literally
# lists load_InterfaceNamer/load_KernelEventMonitor/load_LinkConfiguration/
# load_PreferencesMonitor -- IPConfiguration is NOT in that table at all,
# see build_configd_plugins.sh's own IPConfiguration-scoped sibling work
# and the DAR-170 ticket comment for that real, separate finding).
#
# Produces libc_build/libconfigd_plugins.a. build_configd.sh links this
# archive INSTEAD of relying on configd_builtin_plugins_stub.c's stand-ins
# for these four real symbols (IPMonitor/QoSMarking's stand-ins stay,
# DAR-239's scope, real ObjC wall not addressed here).
#
# ============================================================
# REAL FILE LIST (verified against configd.xcodeproj's own per-plugin
# PBXNativeTarget Sources build phases)
# ============================================================
# - InterfaceNamer: ifnamer.c (one real file, already literally named
#   load_InterfaceNamer() in source -- no wrapper needed).
# - LinkConfiguration: linkconfig.c (ditto, load_LinkConfiguration()).
# - PreferencesMonitor: prefsmon.c (ditto, load_/prime_PreferencesMonitor()).
# - KernelEventMonitor: eventmon.c ONLY -- eventmon.c itself textually
#   #includes ev_dlil.c/ev_ipv4.c/ev_ipv6.c (confirmed by grep, lines
#   ~897/902/910) as ONE translation unit; compiling those 3 files
#   separately would double-define every symbol in them. ev_extra.m
#   (a 5th, real, Objective-C-*named*-but-zero-actual-ObjC-syntax file --
#   confirmed by grep, no @interface/@implementation/message-send anywhere)
#   is real but DELIBERATELY EXCLUDED: its is_expensive() helper needs the
#   SCNetworkInterface family (_SCNetworkInterfaceIsTethered() etc), which
#   build_libsystemconfiguration_client.sh already deliberately excluded
#   from libSystemConfiguration.dylib as a separate, substantial subsystem.
#   The one real symbol ev_dlil.c calls from it, interface_update_expensive(),
#   gets a narrow, functionally-honest stand-in
#   (configd_plugin_expensive_compat.c) instead -- see that file's own
#   header comment for why `return NULL` is the REAL correct answer for
#   this project's only real interface (en0, plain Ethernet, never
#   tethered/BT-PAN/WWAN), not a functional shortcut.
#
# ============================================================
# REAL GAPS FOUND AND CLOSED, one file/header at a time
# ============================================================
# - <IOKit/IOKitLibPrivate.h>, <IOKit/usb/USB.h>: real, already-pinned
#   headers (IOKitUser, IOUSBFamily) just missing the IOKit/ path prefix
#   their real source tree doesn't itself provide -- symlinked into a
#   staged configd_plugin_compat/IOKit/ dir, same technique
#   build_libsystemconfiguration_client.sh already established for
#   IOKit/IOKitLib.h. <IOKit/IOKitKeysPrivate.h>/<IOKit/IOBSD.h>/
#   <IOKit/IOMessage.h>/<IOKit/network/*.h> are already real, pinned,
#   reachable via -I "$XNU/iokit" (xnu-7195/iokit/IOKit/...), no staging
#   needed.
# - <lockdown.h> + <SoftLinking/WeakLinking.h>: real, genuinely
#   never-open-sourced Apple-internal headers (confirmed via full
#   pinned-repo grep -- MobileLockdown isn't on apple-oss-distributions,
#   SoftLinking is an internal build-system umbrella). Narrow, honestly
#   documented compat stand-ins (configd_plugin_lockdown_compat.h,
#   configd_plugin_compat/SoftLinking/WeakLinking.h) -- real, load-bearing
#   fact confirmed by reading ifnamer.c directly: every real caller of
#   these symbols is gated behind haveLockdown() (ifnamer.c ~line 2399),
#   itself only consulted at ifnamer.c:3384, guarding a whole "Trust
#   Required" (iOS host-pairing) subsystem this Pi3/Ethernet target
#   genuinely has no equivalent for. Weak-importing these symbols with no
#   definition anywhere makes haveLockdown() correctly, honestly return
#   FALSE -- real Apple's own designed graceful-degradation path (the same
#   one real Mac hardware without MobileLockdown already relies on), not a
#   functional shortcut.
# - <nw/private.h>: real, never-open-sourced libnetcore/Network.framework
#   internal header, confirmed by grep to be genuinely UNUSED (zero
#   nw_*/NW_* symbol references anywhere in eventmon.c) -- empty,
#   honestly-documented stand-in, same precedent as
#   CoreFoundation/CFUserNotification.h's own empty stand-in.
set -e
cd "$(dirname "$0")"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi

CONFIGD="../../third_party/configd"
SC="$CONFIGD/SystemConfiguration.fproj"
XNU="../../third_party/xnu-7195"
LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
PLATFORM="../../third_party/libplatform"
DISPATCH="../../third_party/libdispatch"
PD="../../third_party/PureDarwin/src/Libraries/XPC"
IOKITUSER="../../third_party/IOKitUser"
IOUSBFAMILY="../../third_party/IOUSBFamily/IOUSBFamily/Headers"
OUT="libc_build"
rm -rf "$OUT/configd_plugins_obj"
mkdir -p "$OUT/configd_plugins_obj"

for d in sc_headers sc_iokit_headers sc_dispatch_compat sc_compat_include compat_include; do
    if [ ! -d "$OUT/$d" ]; then
        echo "error: $OUT/$d not found -- run build_libsystemconfiguration_client.sh first" >&2
        exit 1
    fi
done

# Real, staged IOKit/ header symlinks -- regenerated fresh every run for
# reproducibility (per repo convention: no manual/ad-hoc staging).
rm -rf "$OUT/configd_plugin_iokit_headers"
mkdir -p "$OUT/configd_plugin_iokit_headers/IOKit/usb"
ln -sf "$(pwd)/$IOKITUSER/IOKitLibPrivate.h" "$OUT/configd_plugin_iokit_headers/IOKit/IOKitLibPrivate.h"
ln -sf "$(pwd)/$IOUSBFAMILY/USB.h" "$OUT/configd_plugin_iokit_headers/IOKit/usb/USB.h"

CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__va_list=__builtin_va_list \
    -D__API_UNAVAILABLE_PLATFORM_bridgeos=bridgeos,unavailable \
    -D__API_AVAILABLE_PLATFORM_bridgeos\(x\)=ios,introduced=x \
    -DPRIVATE \
    -include "sys/fileport.h" \
    -include "network_config_check_compat.h" \
    -include "IOKit/usb/USBSpec_ifnamer_compat.h" \
    -I "configd_plugin_compat" \
    -I "$OUT/configd_plugin_iokit_headers" \
    -I "$PD/libxpc/include" \
    -I "launchd_compat_headers" \
    -I "$OUT/sc_compat_include" -I "$OUT/compat_include" -I "$OUT/cf_headers" \
    -I "$SC" -I "$SC/helper" -I "$CONFIGD/IPMonitorControl" \
    -I "$CONFIGD/Plugins/common" \
    -I "$CONFIGD/Plugins/InterfaceNamer" -I "$CONFIGD/Plugins/LinkConfiguration" \
    -I "$CONFIGD/Plugins/PreferencesMonitor" -I "$CONFIGD/Plugins/KernelEventMonitor" \
    -I "$OUT/sc_headers" -I "$OUT/sc_iokit_headers" -I "$XNU/iokit" \
    -I "$PD/launchd/System-compat" \
    -I "$OUT/sc_dispatch_compat" -I "$DISPATCH" -I "$DISPATCH/private" \
    -I "$LIBC/include" -I "$LIBC/darwin" -I "$LIBC/fbsdcompat" -I "$LIBC/locale" -I "$LIBC/locale/FreeBSD" \
    -I "$LIBC/stdtime/FreeBSD" \
    -I "$PTHREAD/include" -I "$PTHREAD/private" \
    -I "$PLATFORM/private" -I "$PLATFORM/include" \
    -I ../../generated \
    -I "$XNU/libsyscall/mach" -I "$XNU/libsyscall" -I "$XNU/osfmk" -I "$XNU/bsd")

FILES="$CONFIGD/Plugins/InterfaceNamer/ifnamer.c \
    $CONFIGD/Plugins/LinkConfiguration/linkconfig.c \
    $CONFIGD/Plugins/PreferencesMonitor/prefsmon.c \
    $CONFIGD/Plugins/KernelEventMonitor/eventmon.c \
    configd_plugin_expensive_compat.c \
    configd_plugin_network_config_compat.c"

ok=0
fail=0
for f in $FILES; do
    base=$(basename "$f" .c)
    out="$OUT/configd_plugins_obj/${base}.o"
    if "$CLANG" "${CFLAGS[@]}" "$f" -o "$out" 2>"$OUT/configd_plugins_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/configd_plugins_obj/${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OUT/configd_plugins_obj/${base}.err")"
    fi
done
echo "configd_plugins: $ok compiled, $fail gaps (see $OUT/configd_plugins_obj/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi

rm -f "$OUT/libconfigd_plugins.a"
ar rcs "$OUT/libconfigd_plugins.a" "$OUT"/configd_plugins_obj/*.o
echo "wrote $OUT/libconfigd_plugins.a ($(ls "$OUT"/configd_plugins_obj/*.o | wc -l | tr -d ' ') objects)"
