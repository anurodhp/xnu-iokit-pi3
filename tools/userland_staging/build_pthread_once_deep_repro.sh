#!/bin/bash
# Builds pthread_once_deep_repro.macho -- see pthread_once_deep_repro.c's
# own header comment for what this diagnostic tool proved (real
# libpthread.a's pthread_once does NOT corrupt a caller's stack at real
# bash-depth recursion, isolating the Eighteenth/Nineteenth pass bug to
# which pthread_once implementation gets linked, not to real
# pthread_once()/_os_once() logic itself -- see docs/roadmap.md's
# Nineteenth pass). Same real curated-stack link as pthread_smoketest.c
# (build_libpthread.sh) -- needs real TSD bootstrap, real locks, real
# malloc, all the way down.
set -e
cd "$(dirname "$0")"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

PTHREAD="../../third_party/libpthread"
PLATFORM="../../third_party/libplatform"
MALLOC="../../third_party/libmalloc"
XNU="../../third_party/xnu-7195"
OUT="libc_build"

for a in libpthread.a libplatform.a libmalloc.a libc_stdio.a; do
    if [ ! -f "$OUT/$a" ]; then
        echo "error: $OUT/$a not found -- run build_libpthread.sh (and its prerequisites) first" >&2
        exit 1
    fi
done

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -Wno-availability -Wno-deprecated-declarations \
    -D__LIBC__ -D__POSIX_LIB__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_NON_CANCELABLE=1 \
    -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 -D__PTHREAD_BUILDING_PTHREAD__=1 -D__PTHREAD_EXPOSE_INTERNALS__ \
    -DOS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY=1 -DPRIVATE=1 \
    -D_os_xbs_chrooted=0 \
    -D__API_UNAVAILABLE_PLATFORM_bridgeos=ios,unavailable \
    -D__API_AVAILABLE_PLATFORM_bridgeos(x)=ios,introduced=x \
    -I . \
    -I $PTHREAD -I $PTHREAD/src -I $PTHREAD/src/resolver -I $PTHREAD/private -I $PTHREAD/include -I $PTHREAD/kern \
    -I $PLATFORM/internal -I $PLATFORM/private -I $PLATFORM/include \
    -I $OUT/compat_include \
    -I $MALLOC/private \
    -I $XNU/libsyscall -I $XNU/libsyscall/mach -I $XNU/bsd \
    -idirafter $XNU/osfmk -idirafter $XNU/libkern"

$CLANG $CFLAGS pthread_once_deep_repro.c -o "$OUT/pthread_once_deep_repro.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -o pthread_once_deep_repro.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT/pthread_once_deep_repro.o" \
    "$OUT/libpthread.a" \
    "$OUT/libplatform.a" "$OUT/libmalloc.a" "$OUT/libc_stdio.a" "$OUT/cerror_stub.o"
echo "wrote pthread_once_deep_repro.macho"

otool -l pthread_once_deep_repro.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l pthread_once_deep_repro.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
if nm -u pthread_once_deep_repro.macho | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u pthread_once_deep_repro.macho >&2
fi
