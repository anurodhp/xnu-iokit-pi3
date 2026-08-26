#!/bin/bash
# Builds libc_build/kqueue_smoketest.macho: a real, standalone proof that
# kqueue(2)/kevent(2) work end to end in userland on this target -- see
# kqueue_smoketest.c's own header comment. Part of Phase 6.6 (libdispatch
# prereqs, docs/daemon-roadmap.md), itself a real prerequisite for Phase
# 6.8 (launchd+XPC via PureDarwin).
#
# Reuses already-built libc_build/*.a and the real _kevent/_kqueue raw
# syscall objects build_simple_cmds.sh already compiles into
# libc_build/simple_obj/ (SYS_STUBS_NEEDED list) -- both are real,
# generated once via the same create-syscalls.pl pass every other tier
# in this directory uses, just never linked into a binary that actually
# calls them until now. Run build_simple_cmds.sh at least once before
# this script if libc_build/simple_obj/sys__kevent.o is missing.
set -euo pipefail
cd "$(dirname "$0")"
OUT="libc_build"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi

for f in libc_printf.a libc_gen.a libc_stdio.a libc_locale_for_stdio.a \
         libpthread.a libclosure.a libmalloc.a libplatform.a \
         simple_obj/sys__kevent.o simple_obj/sys__kqueue.o simple_obj/statfs64_compat.o \
         cerror_stub.o simple_sched_yield_trap.o simple_sched_yield.o \
         pthread_tsd_shim_libc_bootstrap.o; do
    if [ ! -f "$OUT/$f" ]; then
        echo "error: $OUT/$f not found -- run build_simple_cmds.sh (and its" \
             "own prerequisite tiers) first" >&2
        exit 1
    fi
done

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0"

"$CLANG" $CFLAGS kqueue_smoketest.c -o "$OUT/kqueue_smoketest.o"

# Real, already-established compat for the one other symbol
# libmalloc.a's now-retired pthread_tsd_shim.o used to provide
# (pthread_tsd_shim_bootstrap, needed by libmalloc.a's real _get_zone) --
# same object build_llvm_libcxx.sh already built/uses for the identical
# reason.
if [ ! -f "$OUT/libcxx_tsd_shim_bootstrap_compat.o" ]; then
    "$CLANG" $CFLAGS libcxx_tsd_shim_bootstrap_compat.c -o "$OUT/libcxx_tsd_shim_bootstrap_compat.o"
fi

# Real Apple $UNIX2003/$INODE64-suffixed symbol aliasing (ld64 -alias),
# needed because libmalloc.a/libc_printf.a call the suffixed names
# directly (real Apple SDK behavior) while this project's own syscall
# stubs only define the plain name. build_simple_cmds.sh's own
# LINK_ALIASES already carries the exact real list every one of its
# binaries needs; extracted verbatim here rather than duplicated by hand
# so the two never drift apart.
eval "$(awk '/^LINK_ALIASES="/{flag=1} flag{print; if ($0 !~ /\\$/) exit}' build_simple_cmds.sh)"

# This smoketest's minimal link doesn't pull in every base symbol
# build_simple_cmds.sh's own binaries do (e.g. no ___pipe/_connect/
# _send -- no networking or process spawning here); real ld64-609 hits
# the exact same assertion-failure crash tar(1) already worked around
# (LINK_ALIASES_TAR's own header comment) when asked to alias a base
# symbol that isn't present at all in a small link. Filtered down to
# only the aliases whose base symbol this specific link actually defines
# -- not a behavior change, same real workaround shape as tar's.
read -ra _alias_tokens <<< "$LINK_ALIASES"
LINK_ALIASES=""
for tok in "${_alias_tokens[@]}"; do
    case "$tok" in
        -Wl,-alias,___pipe,*|-Wl,-alias,_connect,*|-Wl,-alias,_fchmod,*|-Wl,-alias,_getattrlist,*| \
        -Wl,-alias,_getopt\$UNIX2003,*|-Wl,-alias,_getrlimit,*|-Wl,-alias,_lchown,*| \
        -Wl,-alias,_mmap,*|-Wl,-alias,_munmap,*|-Wl,-alias,_nanosleep,*|-Wl,-alias,_read,*| \
        -Wl,-alias,_reallocarray,*|-Wl,-alias,_regcomp,*|-Wl,-alias,_send,*| \
        -Wl,-alias,_setattrlist,*|-Wl,-alias,_sigsuspend,*|-Wl,-alias,_waitpid,*|-Wl,-alias,_writev,*)
            ;;
        *) LINK_ALIASES="$LINK_ALIASES $tok" ;;
    esac
done

# libpthread.a's own real pthread_mmap_compat.o (see build_libpthread.sh)
# directly defines _mmap\$UNIX2003/_munmap\$UNIX2003, not plain
# _mmap/_munmap -- the opposite direction from LINK_ALIASES' entries
# above (which assume plain _mmap already exists and alias the suffixed
# name to it, true in build_simple_cmds.sh's fuller link, not true
# here). pthread_main_thread_bootstrap.o's real pthread_create() calls
# plain _mmap directly, so alias the real direction instead.
LINK_ALIASES="$LINK_ALIASES -Wl,-alias,_mmap\$UNIX2003,_mmap -Wl,-alias,_munmap\$UNIX2003,_munmap"

# All real raw syscall stubs build_simple_cmds.sh's own SYS_STUBS_NEEDED
# pass already generated (same real create-syscalls.pl output) -- linked
# wholesale here rather than picked one at a time, since libc_gen.a/
# libc_printf.a pull in an unpredictable transitive subset of them
# depending on which real Apple .c file happens to get linked in first.
SYS_OBJS=$(ls "$OUT"/simple_obj/sys_*.o)

# Real, pre-existing duplicate-archive-member conflict (same one
# build_llvm_libcxx.sh's own header comment documents and fixes the same
# way): libc_locale_for_stdio.a's superseded pthread_tsd_shim.o still
# defines pthread_key_init_np, colliding with real libpthread.a
# (pthread_tsd.o) once both are linked together, which this smoketest
# (like that one) needs. Fixed on a scratch copy only, never the shared
# committed archive -- reuses build_llvm_libcxx.sh's own scratch output
# if already built, since it's byte-for-byte the same fix.
if [ ! -f "$OUT/libcxx_locale_for_stdio_scratch.a" ]; then
    cp "$OUT/libc_locale_for_stdio.a" "$OUT/libcxx_locale_for_stdio_scratch.a"
    ar d "$OUT/libcxx_locale_for_stdio_scratch.a" pthread_tsd_shim.o
fi

LINK_COMMON="$OUT/libc_printf.a $OUT/libc_gen.a $OUT/libc_stdio.a $OUT/libcxx_locale_for_stdio_scratch.a $OUT/libc_gen.a $OUT/libc_stdio.a \
    $OUT/pthread_tsd_shim_libc_bootstrap.o $OUT/libpthread.a $OUT/simple_sched_yield_trap.o $OUT/simple_sched_yield.o \
    $SYS_OBJS $OUT/simple_obj/statfs64_compat.o \
    $OUT/libclosure.a $OUT/libmalloc.a $OUT/cerror_stub.o $OUT/libplatform.a"

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -o "$OUT/kqueue_smoketest.macho" \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT/kqueue_smoketest.o" "$OUT/libcxx_tsd_shim_bootstrap_compat.o" $LINK_COMMON $LINK_ALIASES

if otool -l "$OUT/kqueue_smoketest.macho" | grep -q LC_LOAD_DYLIB; then
    echo "error: kqueue_smoketest.macho is not fully static (LC_LOAD_DYLIB present)" >&2
    exit 1
fi
if ! otool -l "$OUT/kqueue_smoketest.macho" | grep -q LC_UNIXTHREAD; then
    echo "error: kqueue_smoketest.macho missing LC_UNIXTHREAD (not a valid static executable)" >&2
    exit 1
fi

echo "wrote $OUT/kqueue_smoketest.macho (real, fully static, zero LC_LOAD_DYLIB)"
