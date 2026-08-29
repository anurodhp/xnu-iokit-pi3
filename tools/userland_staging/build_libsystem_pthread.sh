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
MEMBERS="pthread_atfork.o pthread_cond.o pthread_cwd.o pthread_mutex.o pthread_rwlock.o pthread_tsd.o \
    pthread_main_thread_bootstrap.o pthread_bsdthread_create_syscall.o pthread_tsd_platform_bootstrap.o \
    pthread_bsdthread_register_syscall.o pthread_bsdthread_terminate_syscall.o \
    pthread_psynch_syscalls.o pthread_compat_extras.o pthread_abort_with_reason_compat.o \
    pthread_guard_compat.o pthread_simple_getenv.o pthread_platform_memcmp.o pthread_gettimeofday.o \
    pthread___pthread_chdir.o pthread___pthread_fchdir.o pthread_mmap_compat.o pthread___mmap.o \
    pthread___munmap.o pthread___ulock_wait2.o \
    pthread_thread_ops_compat.o pthread___thread_selfid.o pthread_clear_qos_tsd_compat.o"
rm -rf "$OUT/libsystem_pthread/extracted"
mkdir -p "$OUT/libsystem_pthread/extracted"
(cd "$OUT/libsystem_pthread/extracted" && ar x "../../libpthread.a" $MEMBERS)

# Real pthread QoS/attr tier, moved here 2026-08-29 (static-linking
# cleanup, Phase 1a). build_libpthread_qos.sh's libpthread_qos.a carries
# the real POSIX pthread_attr_* API, the real QoS API (qos_class_self,
# pthread_set_qos_class_self_np, _pthread_qos_class_encode/_decode,
# the _pthread_workqueue_override_* family) and pthread_exit --
# i.e. straightforwardly the exported surface of real Apple's own
# libsystem_pthread.dylib. This project had it in exactly one place
# instead: force_load'ed into libdispatch.dylib, which is why
# _pthread_attr_init/_pthread_exit/_qos_class_self showed up as
# libdispatch exports rather than pthread ones, and why libdispatch had
# to statically bundle libpthread.a alongside them (the QoS code calls
# straight into pthread internals like __pthread_ptr_munge_token that
# only resolve inside the same image).
#
# host_privUser.o/mach_voucherUser.o come along because
# libpthread_qos_compat.o's real voucher path calls their MIG stubs
# (__kernelrpc_host_create_mach_voucher /
# __kernelrpc_mach_voucher_extract_attr_recipe) -- confirmed via nm per
# member, not assumed. Their own two remaining externs (_mig_strncpy,
# _voucher_mach_msg_set) are deliberately left undefined here rather
# than adding another private copy: _voucher_mach_msg_set really is
# libdispatch's symbol on real Apple too, and duplicating the MIG
# runtime is the exact bug this pass exists to remove.
if [ ! -f "$OUT/libpthread_qos.a" ]; then
    echo "error: $OUT/libpthread_qos.a not found -- run build_libpthread_qos.sh first" >&2
    exit 1
fi
QOS_MEMBERS="qos.o libpthread_attr_compat.o libpthread_qos_compat.o host_privUser.o mach_voucherUser.o"
(cd "$OUT/libsystem_pthread/extracted" && ar x "../../libpthread_qos.a" $QOS_MEMBERS)
MEMBERS="$MEMBERS $QOS_MEMBERS"

# __kernelrpc_host_create_mach_voucher: real MIG output, but it lives in
# libmach_ipc.a's mach_hostUser.o alongside ~20 OTHER host_*() routines
# this dylib has no business exporting (and which would collide with the
# copies libdispatch/libxpc already carry). Narrowed to the one routine
# with `ld -r -exported_symbol` -- same real precedent
# build_libdispatch_dylib.sh already established for pulling
# _mach_make_memory_entry_64 out of a multi-routine vm_mapUser.o.
if [ ! -f "$OUT/libmach_ipc.a" ]; then
    echo "error: $OUT/libmach_ipc.a not found -- run build_libmach_ipc.sh first" >&2
    exit 1
fi
LD="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
ar p "$OUT/libmach_ipc.a" mach_hostUser.o > "$OUT/libsystem_pthread/mach_hostUser_raw.o"
"$LD" -r -arch arm64 -exported_symbol __kernelrpc_host_create_mach_voucher \
    "$OUT/libsystem_pthread/mach_hostUser_raw.o" \
    -o "$OUT/libsystem_pthread/extracted/kernelrpc_host_create_mach_voucher.o"
rm -f "$OUT/libsystem_pthread/mach_hostUser_raw.o"
MEMBERS="$MEMBERS kernelrpc_host_create_mach_voucher.o"

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

# Real gap: see libsystem_pthread_dylib_bootstrap_compat.c's own comment --
# nothing ever called pthread_main_thread_bootstrap() for a real, dynamic-
# executable (dyld-linked) consumer of this dylib; every static consumer's
# own hand-rolled entry point calls it explicitly, but dyld jumps straight
# to main() with no such wrapper. Real dylib constructor closes the gap.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libsystem_pthread_dylib_bootstrap_compat.c -o "$OUT/libsystem_pthread/dylib_bootstrap_compat.o"

mkdir -p "$OUT/system"
rm -f "$OUT/system/libsystem_pthread.dylib"
# Real UNIX2003/raw-syscall-naming aliases real pthread source calls by
# these exact names -- same class of gap build_libsystem_malloc.sh's own
# comment already documents (real dylib symbol resolution doesn't
# flatten across image boundaries the way static-archive linking did,
# so each dylib needs its own copy of whichever aliases its own real
# source actually calls).
    # -not_for_dyld_shared_cache: real, newly-surfaced requirement of
    # the daily-driver Xcode's newer ld (NEWLD_BINDIR above) -- it
    # refuses "-undefined dynamic_lookup"/-U on a dylib it classifies
    # as shared-cache eligible (real /usr/lib install path) unless this
    # is set, a real check Xcode 12's ld64-609 never enforced. This
    # project builds no real dyld shared cache at all, so opting every
    # real dylib out is correct, not a workaround (docs/backlog.md item
    # d9's NEWLD_BINDIR sweep, 2026-08-28).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libsystem_pthread.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-alias,___mmap,_mmap \
    -Wl,-alias,___munmap,_munmap \
    -Wl,-alias,__pthread_cond_wait,_pthread_cond_wait \
    -Wl,-alias,_pthread_testcancel,'_pthread_testcancel$UNIX2003' \
    -Wl,-alias,'_pthread_mutexattr_destroy$UNIX2003',_pthread_mutexattr_destroy \
    -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003',__pthread_rwlock_rdlock \
    -Wl,-alias,'_pthread_rwlock_wrlock$UNIX2003',__pthread_rwlock_wrlock \
    -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003',__pthread_rwlock_unlock \
    -Wl,-U,_mig_strncpy -Wl,-U,_voucher_mach_msg_set \
    -o "$OUT/system/libsystem_pthread.dylib" \
    $(for m in $MEMBERS; do echo "$OUT/libsystem_pthread/extracted/$m"; done) \
    "$OUT/libsystem_pthread/sched_yield_trap.o" "$OUT/libsystem_pthread/sched_yield.o" \
    "$OUT/libsystem_pthread/atfork_compat.o" \
    "$OUT/libsystem_pthread/dylib_bootstrap_compat.o" \
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
