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
# stubs only define the plain name.
#
# Real gap, found 2026-09-14: this used to `awk`-scrape
# build_simple_cmds.sh's own LINK_ALIASES variable out of its source
# text at eval time, "extracted verbatim... so the two never drift
# apart" -- except build_simple_cmds.sh's real per-command dynamic-link
# rewrite (2026-08-28 static-linking cleanup) REPLACED that one shared
# LINK_ALIASES variable with per-command inline alias lists, so the
# scrape silently found nothing and `eval`'d an empty string, only
# surfacing now as "LINK_ALIASES: unbound variable" once this script
# was actually reached in a real run for the first time. The two
# scripts had already drifted apart; the scrape just never caught it.
# Fixed by resolving the list once (git history has the exact real
# LINK_ALIASES text as it stood before that rewrite) and hard-coding
# the result directly here -- same filtered-down subset this script's
# own minimal link already needed (no ___pipe/_connect/_send -- no
# networking or process spawning here; real ld64-609 hits the exact
# same assertion-failure crash tar(1) already worked around,
# LINK_ALIASES_TAR's own header comment, when asked to alias a base
# symbol that isn't present at all in a small link), just no longer
# dependent on build_simple_cmds.sh's own internal variable shape.
LINK_ALIASES="-Wl,-alias,_open,_open\$UNIX2003 \
    -Wl,-alias,_close,_close\$UNIX2003 \
    -Wl,-alias,_write,_write\$UNIX2003 \
    -Wl,-alias,___kill,_kill\$UNIX2003 \
    -Wl,-alias,___kill,_kill \
    -Wl,-alias,__mal_semwait_sleep,_sleep\$UNIX2003 \
    -Wl,-alias,___kdebug_trace,_kdebug_trace \
    -Wl,-alias,_fcntl,_fcntl\$UNIX2003 \
    -Wl,-alias,_fstat,_fstat\$INODE64 \
    -Wl,-alias,_lstat,_lstat\$INODE64 \
    -Wl,-alias,_stat,_stat\$INODE64 \
    -Wl,-alias,_poll,_poll\$UNIX2003 \
    -Wl,-alias,__err,_err \
    -Wl,-alias,__warn,_warn \
    -Wl,-alias,_fputs,_fputs\$UNIX2003 \
    -Wl,-alias,_mprotect,_mprotect\$UNIX2003 \
    -Wl,-alias,_pthread_rwlock_rdlock\$UNIX2003,_pthread_rwlock_rdlock \
    -Wl,-alias,_pthread_rwlock_unlock\$UNIX2003,_pthread_rwlock_unlock \
    -Wl,-alias,_strerror,_strerror\$UNIX2003 \
    -Wl,-alias,_fwrite,_fwrite\$UNIX2003 \
    -Wl,-alias,_strtod,_strtod\$UNIX2003 \
    -Wl,-alias,_fdopen,_fdopen\$UNIX2003 \
    -Wl,-alias,_fstatfs,_fstatfs\$INODE64 \
    -Wl,-alias,_fopen,_fopen\$UNIX2003 \
    -Wl,-alias,__flockfile,_flockfile \
    -Wl,-alias,__funlockfile,_funlockfile \
    -Wl,-alias,_pthread_rwlock_rdlock\$UNIX2003,__pthread_rwlock_rdlock \
    -Wl,-alias,_pthread_rwlock_wrlock\$UNIX2003,__pthread_rwlock_wrlock \
    -Wl,-alias,_pthread_rwlock_unlock\$UNIX2003,__pthread_rwlock_unlock \
    -Wl,-alias,_strchr,_index \
    -Wl,-alias,_fstatat64,_fstatat\$INODE64 \
    -Wl,-alias,_closedir,_closedir\$UNIX2003 \
    -Wl,-alias,_opendir,_opendir\$INODE64\$UNIX2003 \
    -Wl,-alias,_readdir,_readdir\$INODE64 \
    -Wl,-alias,_readdir_r,_readdir_r\$INODE64 \
    -Wl,-alias,_fdopendir,_fdopendir\$INODE64\$UNIX2003 \
    -Wl,-alias,_fsync,_fsync\$UNIX2003 \
    -Wl,-alias,_pread,_pread\$UNIX2003 \
    -Wl,-alias,_pwrite,_pwrite\$UNIX2003 \
    -Wl,-alias,_statfs,_statfs\$INODE64 \
    -Wl,-alias,_getfsstat,_getfsstat\$INODE64 \
    -Wl,-alias,_confstr,_confstr\$UNIX2003 \
    -Wl,-alias,_pthread_testcancel,_pthread_testcancel\$UNIX2003"

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
SYS_OBJS=$(ls "$OUT"/simple_obj/sys_*.o 2>/dev/null || true)

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

# ___gdtoa_locks (COMMON/tentative symbol, glue.o): same real gap
# already fixed in build_cron.sh/build_removefile.sh/build_uname.sh/
# build_bash.sh/build_copyfile.sh/build_getty.sh/build_ifconfig.sh/
# build_login.sh/build_ping.sh/build_ps.sh -- ar x it directly (never
# -force_load).
mkdir -p "$OUT/kqueue_smoketest_glue_extract"
(cd "$OUT/kqueue_smoketest_glue_extract" && ar x "../libc_printf.a" glue.o)

LINK_COMMON="$OUT/libc_printf.a $OUT/kqueue_smoketest_glue_extract/glue.o $OUT/libc_gen.a $OUT/libc_stdio.a $OUT/libcxx_locale_for_stdio_scratch.a $OUT/libc_gen.a $OUT/libc_stdio.a \
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
