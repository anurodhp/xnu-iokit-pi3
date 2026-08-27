#!/bin/bash
# Builds libc_build/system/libsystem_pthread.dylib: static-to-dylib
# migration batch 2b (docs/daemon-roadmap.md's "Static-vs-dylib
# architecture debt" section).
#
# ANOTHER REAL CORRECTNESS FIX, same class as libsystem_malloc.dylib's
# own g_zone finding, arguably more serious. Audited before writing this
# script: pthread_atfork.o/pthread_cwd.o and friends aside, the real,
# process-wide thread registry (`_pthread_head`, the linked list of
# every live pthread_t; `_pthread_list_lock`, its guard;
# `_main_thread`/`_main_thread_ptr`) lives in plain BSS globals, exactly
# the mach_task_self_/g_zone shape. Until this script, libdyld.dylib and
# libSystem.B.dylib EACH bundled their own private static copy of the
# ENTIRE pthread implementation, meaning a thread created via
# pthread_create() called from code executing in one dylib would be
# completely invisible to the other dylib's own copy of the thread
# registry -- pthread_mutex_lock()/pthread_self()/thread-enumeration
# calls made from the "wrong" dylib would silently operate on a
# different, incomplete view of the process's real thread state. Real
# Apple ships exactly one shared libsystem_pthread.dylib process-wide
# for exactly this reason. This was never caught by this project's
# existing pthread_create()/contended-mutex QEMU verification (see
# project memory) because that test ran inside a single, fully static
# binary (pre-dyld) -- a single image by definition can't hit this
# duplication, so it's a real gap this specific migration closes, not
# one anything before it could have found.
#
# Real, unmodified Apple source: same file set build_libpthread.sh
# already established for static libpthread.a (its own header comment
# has the full scope/gap writeup, not re-derived here) -- reused by
# extracting the already-built member objects out of libc_build/
# libpthread.a directly (ar x), same approach build_libsystem_malloc.sh
# already established.
#
# pthread_atfork.o IS included below -- first attempt excluded it
# (its real _pthread_atfork()/fork-handler machinery references
# `_os_alloc_once_table`, which nothing in this project's static build
# ever defined, since nothing in the old fully-static world pulled this
# member out of libpthread.a at all -- `ar` only extracts a member when
# something calls a symbol it defines). Turned out to be a real, live
# gap, not a safe-to-skip one: real dyld's own _dyld_initializer()
# unconditionally calls pthread_atfork() to register its own fork
# handlers, discovered when this dylib was first linked against
# libdyld.dylib for real. Fixed at the source of the gap, not here --
# see build_libsystem_kernel.sh's own comment on
# third_party/xnu-7195/libsyscall/os/alloc_once.c, real Apple's actual
# `_os_alloc_once_table` definition, now built into libsystem_kernel.dylib.
set -e
cd "$(dirname "$0")"

OUT="libc_build"
mkdir -p "$OUT/libsystem_pthread/extracted"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -f "$OUT/libpthread.a" ]; then
    echo "error: $OUT/libpthread.a not found -- run build_libpthread.sh first (this script reuses its already-built member objects)" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libsystem_kernel.dylib" ] || [ ! -f "$OUT/system/libsystem_platform.dylib" ] || [ ! -f "$OUT/system/libsystem_malloc.dylib" ]; then
    echo "error: libsystem_kernel.dylib / libsystem_platform.dylib / libsystem_malloc.dylib not found -- run those build scripts first" >&2
    exit 1
fi

# pthread_bsdthread_register_syscall.o/pthread_bsdthread_terminate_
# syscall.o added 2026-08-27: pthread_main_thread_bootstrap.o (already in
# this list) now makes real __bsdthread_register()/__bsdthread_terminate()
# calls (see that file's own git history, part of the real pthread QoS/
# attr subsystem port, commits d3a69cc/6b346f9) -- this MEMBERS list
# wasn't updated when that landed, a real stale-extraction gap found by
# actually rebuilding this dylib (both symbols are self-contained raw
# syscall traps, only depending on `errno`, already resolved via the
# libsystem_kernel.dylib sibling link below -- confirmed via nm before
# adding, not assumed).
MEMBERS="pthread_atfork.o pthread_cond.o pthread_cwd.o pthread_mutex.o pthread_rwlock.o pthread_tsd.o \
    pthread_main_thread_bootstrap.o pthread_bsdthread_create_syscall.o pthread_tsd_platform_bootstrap.o \
    pthread_bsdthread_register_syscall.o pthread_bsdthread_terminate_syscall.o \
    pthread_psynch_syscalls.o pthread_compat_extras.o pthread_abort_with_reason_compat.o \
    pthread_guard_compat.o pthread_simple_getenv.o pthread_platform_memcmp.o pthread_gettimeofday.o \
    pthread___pthread_chdir.o pthread___pthread_fchdir.o pthread_mmap_compat.o pthread___mmap.o \
    pthread___munmap.o pthread___ulock_wait2.o"
rm -rf "$OUT/libsystem_pthread/extracted"
mkdir -p "$OUT/libsystem_pthread/extracted"
(cd "$OUT/libsystem_pthread/extracted" && ar x "../../libpthread.a" $MEMBERS)

# Real gap: real pthread_rwlock.c/pthread_cond.c call sched_yield() in
# their real contended-lock backoff paths; this project's libpthread
# tier deliberately never compiled real pthread.c itself (see
# build_libpthread.sh's own header comment), so this one real exported
# symbol is supplied the same way libunwind_sched_yield.s/.c already do
# for libunwind's own, separate, still-static needs (see that pair's own
# header comments for the real swtch_pri(-59) trap + one-liner
# rationale) -- compiled fresh here rather than pulled from libunwind.a,
# which isn't a dylib dependency of this one.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c libunwind_sched_yield.s -o "$OUT/libsystem_pthread/sched_yield_trap.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libunwind_sched_yield.c -o "$OUT/libsystem_pthread/sched_yield.o"

# Real gap: see libsystem_pthread_atfork_compat.c's own comment --
# pthread_atfork.o's real __pthread_atfork_child() needs these two real
# pthread.c symbols (a file this project's build_libpthread.sh
# deliberately never compiles), only ever reached from an actual fork()
# child, which no dylib-loaded process in this project ever calls.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libsystem_pthread_atfork_compat.c -o "$OUT/libsystem_pthread/atfork_compat.o"

mkdir -p "$OUT/system"
rm -f "$OUT/system/libsystem_pthread.dylib"
# Real UNIX2003/raw-syscall-naming aliases real pthread source calls by
# these exact names -- same class of gap build_libsystem_malloc.sh's own
# comment already documents (real dylib symbol resolution doesn't
# flatten across image boundaries the way static-archive linking did,
# so each dylib needs its own copy of whichever aliases its own real
# source actually calls).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libsystem_pthread.dylib \
    -Wl,-fixup_chains \
    -Wl,-alias,___mmap,_mmap \
    -Wl,-alias,__pthread_cond_wait,_pthread_cond_wait \
    -Wl,-alias,_pthread_testcancel,'_pthread_testcancel$UNIX2003' \
    -Wl,-alias,'_pthread_mutexattr_destroy$UNIX2003',_pthread_mutexattr_destroy \
    -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003',__pthread_rwlock_rdlock \
    -Wl,-alias,'_pthread_rwlock_wrlock$UNIX2003',__pthread_rwlock_wrlock \
    -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003',__pthread_rwlock_unlock \
    -o "$OUT/system/libsystem_pthread.dylib" \
    $(for m in $MEMBERS; do echo "$OUT/libsystem_pthread/extracted/$m"; done) \
    "$OUT/libsystem_pthread/sched_yield_trap.o" "$OUT/libsystem_pthread/sched_yield.o" \
    "$OUT/libsystem_pthread/atfork_compat.o" \
    "$OUT/system/libsystem_kernel.dylib" "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_malloc.dylib"

echo "wrote $OUT/system/libsystem_pthread.dylib"
otool -hv "$OUT/system/libsystem_pthread.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
# plain `nm -u` is misleading here: every undefined-symbol-table entry
# in a real two-level-namespace dylib includes symbols already bound to
# a specific sibling dylib at link time (nm -m annotates these
# "(from libsystem_X)") -- those are NOT gaps, they resolve exactly like
# any other real Apple dylib's cross-image call. The only real gap is an
# entry nm -m marks "(dynamically looked up)", meaning no sibling dylib
# on this link line claims it and it's only resolvable via flat-namespace
# lookup at runtime (same distinction build_libdispatch_dylib.sh's own
# UNDEF_SYMS/-Wl,-U gap list makes explicit).
real_gaps=$(nm -m "$OUT/system/libsystem_pthread.dylib" 2>/dev/null | grep undefined | grep -v "(from " || true)
if [ -n "$real_gaps" ]; then
    echo "WARNING: real undefined symbols present (no sibling dylib resolves these):" >&2
    echo "$real_gaps" >&2
else
    echo "libsystem_pthread.dylib: fully self-contained (beyond libsystem_kernel/platform/malloc.dylib), zero real undefined symbols"
fi
