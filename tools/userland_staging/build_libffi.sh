#!/bin/bash
# Builds Apple's libffi (third_party/libffi, apple-oss-distributions tag
# libffi-27 -- the macos-112 manifest's pin: distribution-macOS@macos-112's
# libffi submodule is 491d23ac, which is libffi-27) as the two dylibs its
# own libffi.xcodeproj builds for arm64 (DAR-388: gnustep-base's
# NSInvocation/forwarding needs libffi; configure.ac:3134 errors without it):
#   libc_build/system/libffi.dylib             (/usr/lib/libffi.dylib)
#   libc_build/system/libffi-trampolines.dylib (/usr/lib/libffi-trampolines.dylib)
#
# Sources: the xcodeproj's arm64 Sources phases -- prep_cif.c, types.c,
# raw_api.c, java_raw_api.c, closures.c, src/aarch64/ffi.c and
# src/aarch64/sysv.S for libffi; src/aarch64/trampoline.S alone for
# libffi-trampolines. Headers: Apple's pre-generated darwin/include
# (ffi.h, fficonfig_arm64.h, ffitarget_arm64.h), then include/ -- the
# xcodeproj's HEADER_SEARCH_PATHS order.
#
# Why a separate trampolines dylib: darwin/include/fficonfig_arm64.h:24
# sets FFI_EXEC_TRAMPOLINE_TABLE, so closures are built by vm_remap()ing
# a page of trampolines out of libffi-trampolines.dylib, which
# src/closures.c:238-248 dlopen()s from /usr/lib at first use (W^X-safe
# closures, no writable+executable memory). vm_remap is real in
# libsystem_kernel since DAR-236.
# Link flags: the xcodeproj's OTHER_LDFLAGS minus -allowable_client
# (Apple-internal client restriction) and minus -no_compact_unwind:
# -not_for_dyld_shared_cache only.
# DEVIATION: -no_compact_unwind leaves the C code with no unwind info at
# all. Apple's own /usr/lib/libffi.dylib has DWARF FDEs for the three
# sysv.S functions and nothing else (`dyld_info -unwind
# /usr/lib/libffi.dylib` on the macOS 27 host). Apple's Foundation never
# throws through libffi, but gnustep-base's forwarding does:
# -forwardInvocation: and -doesNotRecognizeSelector: run inside
# ffi_closure_SYSV_inner (src/aarch64/ffi.c) via GSFFIInvocation.m's
# closure callback, and without unwind info for that frame an exception
# raised there is never caught (DAR-388, found by foundation_smoketest).
# Upstream libffi's own build keeps compact unwind. Versions: DYLIB_COMPATIBILITY_VERSION = 1,
# DYLIB_CURRENT_VERSION = $(CURRENT_PROJECT_VERSION), which Apple's build
# sets from the project version (27 for this tag; the xcodeproj's own
# RC_ProjectSourceVersion = 1 is the local-build placeholder).
set -e
cd "$(dirname "$0")"
OUT="libc_build"
XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
[ -x "$CLANG" ] && [ -d "$SDK" ] || { echo "error: Xcode-12.app not found at $XCODE12" >&2; exit 1; }
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
[ -x "$NEWLD_BINDIR/ld" ] || { echo "error: no usable 'ld' under $DAILY_XCODE (docs/backlog.md item d9)" >&2; exit 1; }
FFI="../../third_party/libffi"
[ -f "$FFI/darwin/include/fficonfig_arm64.h" ] || { echo "error: $FFI not found -- run setup_third_party.sh first" >&2; exit 1; }
for d in libdispatch libsystem_kernel libsystem_c libsystem_malloc libsystem_platform libdyld libSystem.B; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- build the libSystem tier first" >&2; exit 1; }
done

OBJ="$OUT/libffi_obj"
rm -rf "$OBJ"
mkdir -p "$OBJ" "$OUT/system"
CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -O0 -std=gnu99
    -fno-stack-protector -D_FORTIFY_SOURCE=0 -Wno-deprecated-declarations
    `# darwin/include/ffi.h:56-57 annotate the API with SPI_AVAILABLE/` \
    `# SPI_DEPRECATED, Apple-internal macros the public Xcode 12 SDK` \
    `# lacks; empty them, as build_libdispatch_real.sh does for the same gap.` \
    '-DSPI_AVAILABLE(...)=' '-DSPI_DEPRECATED(...)=' \
    `# ffi.h:55-57 also name the platform "iosmac", the older spelling of` \
    `# macCatalyst; the Xcode 12 SDK only defines the macCatalyst forms` \
    `# (usr/include/AvailabilityInternal.h:4448, :4496), so alias them.` \
    '-D__API_AVAILABLE_PLATFORM_iosmac(x)=macCatalyst,introduced=x' \
    '-D__API_DEPRECATED_PLATFORM_iosmac(x,y)=macCatalyst,introduced=x,deprecated=y'
    -I "$FFI/darwin/include" -I "$FFI/include" -I "$FFI/src/aarch64")
for f in prep_cif types raw_api java_raw_api closures; do
    "$CLANG" "${CFLAGS[@]}" "$FFI/src/$f.c" -o "$OBJ/$f.o"
done
"$CLANG" "${CFLAGS[@]}" "$FFI/src/aarch64/ffi.c" -o "$OBJ/aarch64_ffi.o"
"$CLANG" "${CFLAGS[@]}" "$FFI/src/aarch64/sysv.S" -o "$OBJ/aarch64_sysv.o"
mkdir -p "$OBJ/tramp"
"$CLANG" "${CFLAGS[@]}" "$FFI/src/aarch64/trampoline.S" -o "$OBJ/tramp/trampoline.o"

# libdispatch: closures.c's trampoline-table setup runs under dispatch_once.
SYS=("$OUT/system/libdispatch.dylib" "$OUT/system/libsystem_kernel.dylib" "$OUT/system/libsystem_c.dylib"
    "$OUT/system/libsystem_malloc.dylib" "$OUT/system/libsystem_platform.dylib"
    "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib")
link() { # link <out> <install-name> <objs...>
    local out="$1" name="$2"; shift 2
    rm -f "$out"
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
        -install_name "$name" -compatibility_version 1 -current_version 27 \
        -Wl,-not_for_dyld_shared_cache \
        -B"$NEWLD_BINDIR" -Wl,-fixup_chains -o "$out" "$@" "${SYS[@]}"
    otool -hv "$out" | grep -q " DYLIB " || { echo "error: $out is not MH_DYLIB" >&2; exit 1; }
    echo "wrote $out ($name)"
    ./bind_target_audit.sh "$out"
}
link "$OUT/system/libffi-trampolines.dylib" /usr/lib/libffi-trampolines.dylib "$OBJ/tramp/trampoline.o"
link "$OUT/system/libffi.dylib" /usr/lib/libffi.dylib "$OBJ"/*.o
nm -gU "$OUT/system/libffi-trampolines.dylib" | grep -q " _ffi_closure_trampoline_table_page$" || {
    echo "error: libffi-trampolines.dylib does not export ffi_closure_trampoline_table_page (closures.c:244 dlsym()s it)" >&2; exit 1; }
for s in _ffi_prep_cif _ffi_call _ffi_closure_alloc _ffi_prep_closure_loc _ffi_closure_free; do
    nm -gU "$OUT/system/libffi.dylib" | grep -q " $s$" || { echo "error: libffi.dylib does not export $s" >&2; exit 1; }
done
# Public headers, for gnustep-base's configure and compile.
INC="$OUT/libffi_include"
rm -rf "$INC"; mkdir -p "$INC"
cp "$FFI"/darwin/include/ffi.h "$FFI"/darwin/include/ffitarget*.h "$FFI"/darwin/include/fficonfig*.h "$INC/"
echo "staged headers in $INC"
