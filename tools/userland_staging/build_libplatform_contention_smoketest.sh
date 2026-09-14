#!/bin/bash
# Builds libc_build/libplatform_contention_smoketest.macho: real CONTENDED
# coverage for os_unfair_lock/os_once (DAR-227 item #6), additive to (not
# a retrofit of) build_libplatform.sh's own uncontended libplatform_smoketest
# -- see libplatform_contention_smoketest.c's own header for why this is a
# separate binary.
#
# SPLIT OUT of build_libplatform.sh into its own script 2026-09-14: the
# original single-script version created a real ordering cycle --
# build_libplatform.sh produces libplatform.a, which libc_gen.a/libc_stdio.a/
# libmalloc.a/libpthread.a all depend on, but this smoketest needs THOSE
# archives already built, so folding it into the same script made
# build_libplatform.sh appear to depend on its own downstream consumers.
# gen_build_order.py's own cycle detector flagged it (73 dropped edges,
# up from this project's established 0-edge baseline) the first time this
# ran after merging DAR-227. Same fix shape as every other late-stage
# smoketest in this project (build_dispatch_machport_smoketest.sh,
# build_mach_port_construct_smoketest.sh, ...): its own script, its own
# place in the real dependency order, main library script stays a clean
# leaf producer.
#
# Needs real pthread_create()/pthread_join(), which in turn needs a real
# mmap() for the child thread's stack (pthread_main_thread_bootstrap.c) --
# no purely-static archive in this project defines that raw syscall
# generically (confirmed: `nm` across every libc_build/*.a finds only
# libsyslogd.a's own local copy), so unlike the uncontended smoketest this
# one is built as a real DYNAMIC executable instead (same
# -DCRT_DYNAMIC_LINKING recipe build_pd_console_login.sh already
# established) -- libSystem.B.dylib reexports libsystem_pthread.dylib,
# which has a real _mmap. This also means it exercises the same
# dyld-loaded runtime shape real consumers (syslogd, launchd) actually
# run under, not a static-only bundle.
set -e
cd "$(dirname "$0")"

PLATFORM="../../third_party/libplatform"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
OUT="libc_build"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -Wno-availability -Wno-deprecated-declarations \
    -D__API_AVAILABLE_PLATFORM_bridgeos(x)=ios,introduced=x \
    -DPRIVATE=1 -DSWITCH_OPTION_OSLOCK_DEPRESS=4 -DSWITCH_OPTION_OSLOCK_WAIT=5 \
    -I $PLATFORM/internal -I $PLATFORM/private -I $PLATFORM/include -I $PLATFORM/src \
    -I $PLATFORM/src/os/resolver \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/libsyscall -I $XNU/bsd \
    -I $OUT/compat_include \
    -idirafter $XNU/osfmk -idirafter $XNU/libkern"

if [ ! -f "$OUT/libplatform.a" ] || [ ! -f "$OUT/pthread_tsd_shim.o" ] || [ ! -f "$OUT/cerror_stub.o" ]; then
    echo "error: $OUT/libplatform.a (or pthread_tsd_shim.o/cerror_stub.o) not found -- run build_libplatform.sh first" >&2
    exit 1
fi
for dep in libc_gen.a libc_string.a libc_stdio.a libc_printf.a libc_locale_for_stdio.a \
           libmalloc.a libpthread.a; do
    if [ ! -f "$OUT/$dep" ]; then
        echo "error: $OUT/$dep not found -- run build_${dep%.a}.sh first" >&2
        exit 1
    fi
done
for d in libdyld.dylib libSystem.B.dylib; do
    if [ ! -f "$OUT/system/$d" ]; then
        echo "error: $OUT/system/$d not found -- run build_libdyld.sh/build_libsystem.sh first" >&2
        exit 1
    fi
done
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi
"$CLANG" $CFLAGS libplatform_contention_smoketest.c -o "$OUT/libplatform_contention_smoketest.o"
rm -f libplatform_contention_smoketest.macho
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o libplatform_contention_smoketest.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT/libplatform_contention_smoketest.o" \
    "$OUT/libplatform.a" "$OUT/pthread_tsd_shim.o" "$OUT/cerror_stub.o" \
    "$OUT/libc_gen.a" "$OUT/libc_string.a" "$OUT/libc_stdio.a" "$OUT/libc_printf.a" \
    "$OUT/libc_locale_for_stdio.a" "$OUT/libmalloc.a" "$OUT/libpthread.a" \
    "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib" \
    2>"$OUT/libplatform_contention_smoketest_link.err" \
    || { echo "error: real link failed -- see $OUT/libplatform_contention_smoketest_link.err" >&2; tail -60 "$OUT/libplatform_contention_smoketest_link.err" >&2; exit 1; }
echo "wrote libplatform_contention_smoketest.macho"
