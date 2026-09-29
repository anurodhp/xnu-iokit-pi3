#!/bin/bash
# DAR-416 step 1b (2026-09-27): this dylib is now Apple's real libpthread
# (third_party/libpthread, libpthread-454.80.2 plus the DAR-257 fork
# commit): pthread.c, pthread_cancelable.c (normal and cancelable variants),
# pthread_cond.c, pthread_mutex.c, pthread_rwlock.c, pthread_tsd.c,
# pthread_atfork.c, pthread_cwd.c, qos.c, pthread_dependency.c and
# pthread_asm.s. Its main-thread setup is __pthread_init(), which the
# libSystem initializer calls as Libsystem init.c:221 does
# (libsystem_dyld_initializer_compat.c). Its syscalls, traps and Mach
# wrappers come from libsystem_kernel, and its string/_simple/semaphore
# helpers from libsystem_platform, as on Apple.
#
# Retired: the hand-written create/exit/detach/kill/bootstrap layer
# (pthread_main_thread_bootstrap.c, pthread_thread_ops_compat.c,
# pthread_compat_extras.c, libpthread_attr_compat.c,
# libpthread_qos_compat.c, libsystem_pthread_atfork_compat.c,
# libsystem_pthread_main_stack_compat.c,
# libsystem_pthread_dylib_bootstrap_compat.c and pthread_cond_wait_public.c).
# The last survivor, a no-op pthread_main_thread_bootstrap export
# (libsystem_pthread_legacy_compat.c), is gone too (DAR-426): the main
# wrappers no longer call it. The history comment below describes the old
# layer.
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

# Real, verified fix (2026-08-28, see docs/backlog.md item d9 for the
# full trail and tools/userland_staging/build_simple_cmds.sh's own
# NEWLD_BINDIR header comment for the isolated repro this is based on):
# Xcode 12's own bundled ld64-609 has a real internal crash under
# -Wl,-fixup_chains that a much newer ld does not. Compiling still uses
# Xcode 12's clang/SDK above (untouched -- that vintage match is a
# separate, still-load-bearing invariant); only the final link step's
# linker binary changes, via clang's -B override.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
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
#
# pthread_thread_ops_compat.o/pthread___thread_selfid.o/
# pthread_clear_qos_tsd_compat.o added 2026-08-29 (static-linking
# cleanup, Phase 1a -- see dup_symbol_audit.sh's own header for why):
# they define real, standard pthread API (_pthread_detach, _pthread_kill,
# _pthread_setschedparam, _pthread_threadid_np, _pthread_is_threaded_np,
# _pthread_getugid_np, ___thread_selfid, __pthread_clear_qos_tsd) that
# was reaching processes ONLY through libdispatch.dylib's own private
# static copy of libpthread.a. Real Apple exports every one of these
# from libsystem_pthread.dylib -- this is where they belong, and moving
# them here is what lets libdispatch stop bundling the archive at all.
# --- DAR-416 1b: build Apple's real libpthread --------------------------
PTHREAD="../../third_party/libpthread"
PLATFORM="../../third_party/libplatform"
MALLOC="../../third_party/libmalloc"
XNU="../../third_party/xnu-7195"
# Same flags as build_libpthread.sh's tier (see its header for why each
# define is there). qos.c also needs libsyscall's wrappers/spawn for
# spawn_private.h. It is built WITHOUT VARIANT_STATIC here, the dylib
# variant: real pthread.c provides the _pthread_malloc/_pthread_free vtable
# it expects.
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
    -I $XNU/libsyscall -I $XNU/libsyscall/mach -I $XNU/bsd -I $XNU/libsyscall/wrappers/spawn \
    -idirafter $XNU/osfmk -idirafter $XNU/libkern"
OBJ="$OUT/libsystem_pthread/real"
rm -rf "$OBJ" "$OUT/libsystem_pthread/extracted"
mkdir -p "$OBJ"
for f in pthread pthread_cancelable pthread_cond pthread_mutex pthread_rwlock pthread_tsd \
         pthread_atfork pthread_cwd qos pthread_dependency; do
    $CLANG $CFLAGS "$PTHREAD/src/$f.c" -o "$OBJ/$f.o"
done
# The cancelable variant of pthread_cancelable.c (VARIANT_CANCELABLE):
# pthread_join, pthread_cond_wait and friends with cancellation points.
$CLANG $CFLAGS "$PTHREAD/src/variants/pthread_cancelable_cancel.c" -o "$OBJ/pthread_cancelable_cancel.o"
$CLANG $CFLAGS "$PTHREAD/src/pthread_asm.s" -o "$OBJ/pthread_asm.o"

mkdir -p "$OUT/system"
rm -f "$OUT/system/libsystem_pthread.dylib"
# Aliases: this port's userland is compiled with xnu's bsd/sys/cdefs.h on
# the include path, which (unlike the iPhoneOS SDK's cdefs.h:490) does not
# set __DARWIN_ONLY_UNIX_CONFORMANCE. So libpthread's __DARWIN_ALIAS'd
# functions come out as name$UNIX2003, while consumers compiled against the
# SDK headers bind the plain name. On Apple's arm64 build the plain name
# is the definition. Each such export gets its plain name here too. The
# __pthread_rwlock_* names are the ones libsystem_c already binds, from
# this dylib's previous aliases. _bzero/_memcpy/_memset: pthread.c's own
# VARIANT copies (pthread.c:2560+) stay private so they never shadow
# libsystem_platform's.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libsystem_pthread.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-unexported_symbol,_bzero -Wl,-unexported_symbol,_memcpy -Wl,-unexported_symbol,_memset \
    -Wl,-alias,'_pthread_cancel$UNIX2003',_pthread_cancel \
    -Wl,-alias,'_pthread_cond_init$UNIX2003',_pthread_cond_init \
    -Wl,-alias,'_pthread_cond_timedwait$UNIX2003',_pthread_cond_timedwait \
    -Wl,-alias,'_pthread_cond_wait$UNIX2003',_pthread_cond_wait \
    -Wl,-alias,'_pthread_join$UNIX2003',_pthread_join \
    -Wl,-alias,'_pthread_mutexattr_destroy$UNIX2003',_pthread_mutexattr_destroy \
    -Wl,-alias,'_pthread_rwlock_destroy$UNIX2003',_pthread_rwlock_destroy \
    -Wl,-alias,'_pthread_rwlock_init$UNIX2003',_pthread_rwlock_init \
    -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003',_pthread_rwlock_rdlock \
    -Wl,-alias,'_pthread_rwlock_tryrdlock$UNIX2003',_pthread_rwlock_tryrdlock \
    -Wl,-alias,'_pthread_rwlock_trywrlock$UNIX2003',_pthread_rwlock_trywrlock \
    -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003',_pthread_rwlock_unlock \
    -Wl,-alias,'_pthread_rwlock_wrlock$UNIX2003',_pthread_rwlock_wrlock \
    -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003',__pthread_rwlock_rdlock \
    -Wl,-alias,'_pthread_rwlock_wrlock$UNIX2003',__pthread_rwlock_wrlock \
    -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003',__pthread_rwlock_unlock \
    -Wl,-alias,'_pthread_setcancelstate$UNIX2003',_pthread_setcancelstate \
    -Wl,-alias,'_pthread_setcanceltype$UNIX2003',_pthread_setcanceltype \
    -Wl,-alias,'_pthread_sigmask$UNIX2003',_pthread_sigmask \
    -Wl,-alias,'_pthread_testcancel$UNIX2003',_pthread_testcancel \
    -Wl,-alias,'_sigwait$UNIX2003',_sigwait \
    -o "$OUT/system/libsystem_pthread.dylib" \
    "$OBJ"/*.o \
    "$OUT/system/libsystem_kernel.dylib" "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_malloc.dylib"

echo "wrote $OUT/system/libsystem_pthread.dylib"
otool -hv "$OUT/system/libsystem_pthread.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
real_gaps=$(nm -m "$OUT/system/libsystem_pthread.dylib" 2>/dev/null | grep undefined | grep -v "(from " || true)
if [ -n "$real_gaps" ]; then
    echo "error: undefined symbols that no sibling dylib resolves:" >&2
    echo "$real_gaps" >&2
    exit 1
fi
for s in ___pthread_init _pthread_join _pthread_cancel _pthread_create _pthread_exit \
         __pthread_exit_if_canceled __pthread_clear_qos_tsd _pthread_current_stack_contains_np; do
    nm -gU "$OUT/system/libsystem_pthread.dylib" | awk '{print $3}' | grep -qxF "$s" || {
        echo "error: libsystem_pthread.dylib does not export $s" >&2; exit 1; }
done
echo "libsystem_pthread.dylib: real libpthread, no undefined symbols outside kernel/platform/malloc"
