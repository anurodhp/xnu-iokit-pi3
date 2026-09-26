#!/bin/bash
# Builds libc_build/system/libiokituser.dylib: static-to-dylib migration,
# docs/backlog.md item d8 -- last unit of this pass's Stage A batch.
# Real Apple ships this as part of IOKit.framework, not a plain dylib --
# this project has no framework-bundle packaging (same simplification
# d8's own note already flags), so this uses a plain, top-level
# /usr/lib/libiokituser.dylib install_name instead, matching every other
# non-libsystem_* standalone dylib in this migration (libresolv.9.dylib,
# libxml2.2.dylib, libiconv.2.dylib).
#
# Real, unmodified source: reuses every already-built member object out
# of libc_build/libiokituser.a (ar x, not recompiled -- see
# build_iokituser.sh's own header for the full scope: real
# iokitmig64.o/mach_portUser.o MIG client stubs, IODataQueueClient.o,
# iokit_trap.o, mach_port_wrappers_real.o/mach_port_extra_traps.o).
#
# Real gap closure, found via the same nm-based intra-archive resolution
# pass build_libresolv_dylib.sh's header documents: the only 3 truly-
# undefined symbols (mig_strncpy, mig_strncpy_zerofill,
# voucher_mach_msg_set) are ALL real, direct exports of this same
# migration pass's own libxpc.dylib (built earlier this pass, via its
# real liblaunchd_mig.a force_load + mach_ipc_dead_stubs_voucher_only.o)
# -- linked as a real sibling below instead of deferred. No `-Wl,-U`
# deferrals needed for this library at all.
set -e
cd "$(dirname "$0")"

OUT="libc_build"
EXTRACT="$OUT/libiokituser_dylib/extracted"
rm -rf "$EXTRACT"
mkdir -p "$EXTRACT"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -f "$OUT/libiokituser.a" ]; then
    echo "error: $OUT/libiokituser.a not found -- run build_iokituser.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libxpc.dylib" ]; then
    echo "error: $OUT/system/libxpc.dylib not found -- run build_libxpc_dylib.sh first (this script links it as a real sibling for mig_strncpy/voucher_mach_msg_set)" >&2
    exit 1
fi
for d in libsystem_c libsystem_malloc libsystem_platform libsystem_kernel libsystem_pthread; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2
        exit 1
    fi
done

# Real, verified fix (docs/backlog.md item d9): Xcode 12's own bundled
# ld64-609 has a real internal crash under -Wl,-fixup_chains that a much
# newer ld does not. Compiling still uses Xcode 12's clang/SDK above;
# only the final link step's linker binary changes, via clang's -B
# override.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi

(cd "$EXTRACT" && ar x "../../libiokituser.a")

# Three members dropped 2026-08-30 (static-linking cleanup, Phase 1d).
# Between them they were 44 of the audit's duplicate symbols -- the
# single largest group left after Phase 1a:
#
#   mach_portUser.o           -- the MIG-generated __kernelrpc_mach_port_*
#                                client stub set (40 symbols). This was
#                                ALSO in libxpc.dylib; neither was their
#                                real home. build_libsystem_kernel.sh now
#                                runs `mig` on xnu-7195's own
#                                osfmk/mach/mach_port.defs and owns all
#                                51 outright, matching real Apple.
#   mach_port_wrappers_real.o -- mach_port_allocate/get_attributes/
#                                set_attributes. All three are real
#                                exports of libsystem_kernel.dylib now
#                                (the first two from cf_mach_port_syscalls.o,
#                                set_attributes newly narrowed out of this
#                                very file over there).
#   mach_port_extra_traps.o   -- the _allocate_trap/_get_attributes_trap
#                                Mach trap stubs those wrappers call;
#                                libsystem_kernel.dylib has its own
#                                (mach_port_traps.s).
#
# libc_build/libiokituser.a itself is deliberately NOT changed: it is
# still linked statically by tools that legitimately need a private copy
# (the iokituser smoketest), and build_iokituser.sh remains its single
# producer. This is a dylib-link-line change only.
(cd "$EXTRACT" && rm -f \
    mach_portUser.o mach_port_wrappers_real.o mach_port_extra_traps.o)

# DAR-251: the high-level IOKitLib API (IOServiceGetMatchingService,
# IORegistryEntryFromPath, IONotificationPortCreate, ...) plus the CF
# serialize/unserialize it uses -- real, unmodified IOKitUser sources, the
# user half of Apple's IOKit.framework. This dylib used to carry only the
# MIG transport (iokitmig64.o) because IOKitLib.c needs CoreFoundation,
# libdispatch and libxpc, none of which existed when build_iokituser.sh
# was written. configd's InterfaceNamer plugin (DAR-240) is the first
# consumer. Only built into this dylib, not libiokituser.a.
for d in libCoreFoundation libdispatch libinfo libsystem_blocks; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- build it first" >&2
        exit 1
    fi
done
T="../../third_party"
IOKITUSER_ABS="$(cd "$T/IOKitUser" && pwd)"
XNU_IOKIT_ABS="$(cd "$T/xnu-7195/iokit/IOKit" && pwd)"
# <IOKit/...> layout of the real framework: IOKitUser's own headers first,
# then xnu's iokit/IOKit headers for the rest (IOTypes.h, IOReturn.h, ...),
# the same split real Apple installs.
IOKH="$OUT/iokitlib_headers/IOKit"
rm -rf "$OUT/iokitlib_headers"
mkdir -p "$IOKH"
for f in "$IOKITUSER_ABS"/*.h; do ln -s "$f" "$IOKH/"; done
for f in "$XNU_IOKIT_ABS"/*.h; do [ -e "$IOKH/$(basename "$f")" ] || ln -s "$f" "$IOKH/"; done
for d in "$XNU_IOKIT_ABS"/*/; do d="${d%/}"; [ -e "$IOKH/$(basename "$d")" ] || ln -s "$d" "$IOKH/"; done
# <device/device_types.h> must be xnu's, not the SDK's: IOKIT_SERVER_VERSION
# is defined only in its `#if PRIVATE` block (xnu-7195
# osfmk/device/device_types.h:77-79), which the public SDK copy strips.
# Without it, IOCFSerialize.c:736 and IOKitLib.c:701 compile out the binary
# serializer while IOKitLib.c:104 still defaults to
# kIOCFSerializeToBinary, so every IOService* matching call returned
# kIOReturnUnsupported (found by iokitlib_smoketest).
mkdir -p "$OUT/iokitlib_headers/device"
ln -s "$(cd "$T/xnu-7195/osfmk/device" && pwd)/device_types.h" "$OUT/iokitlib_headers/device/device_types.h"
# IOCFSerialize.c's binary path includes the kernel's serializer format
# header by its private SDK path.
mkdir -p "$OUT/iokitlib_headers/System/libkern"
ln -s "$(cd "$T/xnu-7195/libkern/libkern" && pwd)/OSSerializeBinary.h" \
    "$OUT/iokitlib_headers/System/libkern/OSSerializeBinary.h"
# DISPATCH_MACH_SPI=1 is from IOKitUser-Embedded.xcconfig.
# libsyscall/mach goes -idirafter: its private string.h would otherwise
# shadow the SDK's and hide strncpy/strlen/strlcpy.
IOKITLIB_CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -O0 -fno-stack-protector \
    -Wno-nullability-completeness -Wno-deprecated-declarations \
    -DPRIVATE=1 -DIOKIT=1 -DDISPATCH_MACH_SPI=1 \
    -D__API_UNAVAILABLE_PLATFORM_bridgeos=bridgeos,unavailable \
    '-D__API_AVAILABLE_PLATFORM_bridgeos(x)=ios,introduced=x' \
    -I "$OUT/iokitlib_headers" -I "$OUT/compat_include" -I "$OUT/cf_headers" -I "$OUT/iokituser_mig" \
    -I "$T/IOKitUser" -I "$OUT/sc_dispatch_compat" -I "$T/libdispatch" \
    -I "$T/PureDarwin/src/Libraries/XPC/libxpc/include" \
    -I "$T/PureDarwin/src/Libraries/CrashReporterClient/include" \
    -I "$T/libplatform/private" -I "$T/libplatform/include" \
    -I "$T/xnu-7195/iokit" -I "$T/xnu-7195/libkern" \
    -idirafter "$T/xnu-7195/osfmk" -idirafter "$T/xnu-7195/libsyscall/mach" -idirafter "$T/xnu-7195/bsd")
# IOCFURLAccess.c (the legacy IOURLCreateDataAndPropertiesFromResource
# API) is left out: nothing here calls it, and it needs fsync/umask/
# getdirentries wrappers no dylib exports yet.
for f in IOKitLib IOCFSerialize IOCFUnserialize.tab; do
    "$CLANG" "${IOKITLIB_CFLAGS[@]}" "$T/IOKitUser/$f.c" -o "$EXTRACT/$f.o"
done

mkdir -p "$OUT/system"
rm -f "$OUT/system/libiokituser.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/libiokituser.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o "$OUT/system/libiokituser.dylib" \
    "$EXTRACT"/*.o \
    "$OUT/system/libCoreFoundation.dylib" "$OUT/system/libdispatch.dylib" \
    "$OUT/system/libinfo.dylib" "$OUT/system/libsystem_blocks.dylib" \
    "$OUT/system/libxpc.dylib" \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_pthread.dylib" \
    2>"$OUT/libiokituser_dylib_link.err" \
    && echo "wrote $OUT/system/libiokituser.dylib" \
    || { echo "libiokituser.dylib link found real undefined symbols:" >&2; grep '^  "' "$OUT/libiokituser_dylib_link.err" | sed 's/,.*//' | sort -u >&2; exit 1; }

otool -hv "$OUT/system/libiokituser.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
echo "declared -Wl,-U deferrals (expect: none for this library):"
xcrun dyld_info -fixups "$OUT/system/libiokituser.dylib" 2>/dev/null | grep '<flat-namespace>' | awk '{print "  " $NF}'
UNEXPECTED=$(xcrun dyld_info -fixups "$OUT/system/libiokituser.dylib" 2>/dev/null | grep -v -E '<flat-namespace>|<weak-def-coalesce>' | grep 'bind' | grep -v -E 'libsystem_c/|libsystem_malloc/|libsystem_platform/|libsystem_kernel/|libsystem_pthread/|libxpc/|libCoreFoundation/|libdispatch/|libinfo/|libsystem_blocks/' || true)
if [ -n "$UNEXPECTED" ]; then
    echo "error: real, unexpected non-sibling bind targets found:" >&2
    echo "$UNEXPECTED" >&2
    exit 1
fi
echo "libiokituser.dylib: every external reference binds to a real, named sibling dylib -- no unexpected/dangling references"
