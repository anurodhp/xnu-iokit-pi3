#!/bin/bash
# Builds libc_build/pthread_smoketest.macho -- libpthread's own smoketest
# (pthread_smoketest.c): bootstrap, pthread_self/equal/main_np, mutex,
# TSD keys, pthread_once, and a real second thread contending on a mutex.
#
# DAR-127: this used to be a static Csu link at the end of
# build_libpthread.sh, which had stopped linking (undefined _kdebug_trace/
# _kill/_mprotect$UNIX2003/_sleep$UNIX2003/_write$UNIX2003) and took that
# whole script down with it. It is now a dynamic executable against the
# real libsystem_pthread.dylib, like every other smoketest here (a static
# binary would also break the DAR-314 rule).
#
# Same real dynamic link recipe as build_tlv_smoketest.sh - read that
# script's own comments for the reasoning behind each flag.
set -euo pipefail
cd "$(dirname "$0")"
OUT="libc_build"
CSU="../../third_party/Csu"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi
if [ ! -d "$CSU" ]; then
    echo "error: $CSU not found -- run ./setup_third_party.sh first" >&2
    exit 1
fi

DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE)" >&2
    exit 1
fi

for f in system/libsystem_c.dylib system/libsystem_pthread.dylib \
         system/libsystem_malloc.dylib system/libsystem_kernel.dylib \
         system/libsystem_platform.dylib \
         system/libdyld.dylib system/libSystem.B.dylib; do
    if [ ! -f "$OUT/$f" ]; then
        echo "error: $OUT/$f not found -- run the libSystem tier build scripts first" >&2
        exit 1
    fi
done

# pthread_smoketest.c includes libpthread's own internal.h, so it needs
# libpthread's include set and defines (same set build_libsystem_pthread.sh
# uses for its internal.h-including compat objects).
PTHREAD_SRC="../../third_party/libpthread"
PLATFORM_SRC="../../third_party/libplatform"
MALLOC_SRC="../../third_party/libmalloc"
XNU_SRC="../../third_party/xnu-7195"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -Wno-availability -Wno-deprecated-declarations \
    -D__LIBC__ -D__POSIX_LIB__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_NON_CANCELABLE=1 \
    -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 -D__PTHREAD_BUILDING_PTHREAD__=1 -D__PTHREAD_EXPOSE_INTERNALS__ \
    -DOS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY=1 -DPRIVATE=1 \
    -D_os_xbs_chrooted=0 \
    -D__API_UNAVAILABLE_PLATFORM_bridgeos=ios,unavailable \
    '-D__API_AVAILABLE_PLATFORM_bridgeos(x)=ios,introduced=x' \
    -I . \
    -I "$PTHREAD_SRC" -I "$PTHREAD_SRC/src" -I "$PTHREAD_SRC/src/resolver" \
    -I "$PTHREAD_SRC/private" -I "$PTHREAD_SRC/include" -I "$PTHREAD_SRC/kern" \
    -I "$PLATFORM_SRC/internal" -I "$PLATFORM_SRC/private" -I "$PLATFORM_SRC/include" \
    -I "$OUT/compat_include" \
    -I "$MALLOC_SRC/private" \
    -I "$XNU_SRC/libsyscall" -I "$XNU_SRC/libsyscall/mach" -I "$XNU_SRC/bsd" \
    -idirafter "$XNU_SRC/osfmk" -idirafter "$XNU_SRC/libkern" \
    pthread_smoketest.c -o "$OUT/pthread_smoketest.o"

rm -f "$OUT/pthread_smoketest.macho"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o "$OUT/pthread_smoketest.macho" \
    "$CSU/start.s" "$CSU/crt.c" \
    "$OUT/pthread_smoketest.o" \
    "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_platform.dylib" \
    "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib"

M="$OUT/pthread_smoketest.macho"
otool -l "$M" | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: no LC_LOAD_DYLINKER /usr/lib/dyld -- not a real dynamic executable" >&2
    exit 1
}
otool -l "$M" | grep -q "cmd LC_MAIN" || {
    echo "error: no LC_MAIN -- see build_simple_cmds.sh's LINK_DYLIBS comment" >&2
    exit 1
}
./bind_target_audit.sh "$M" || {
    echo "error: $M has an unresolvable two-level bind -- it would abort at dyld load" >&2
    exit 1
}
echo "wrote $M ($(wc -c < "$M") bytes, real dynamic executable)"
