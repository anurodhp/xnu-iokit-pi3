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
# DAR-416 step 1a: the hand-written syscall stubs that used to be members
# here (pthread_bsdthread_{create,register,terminate}_syscall.o,
# pthread_psynch_syscalls.o, pthread___pthread_{chdir,fchdir}.o,
# pthread___ulock_wait2.o, pthread___thread_selfid.o) are gone: those
# symbols are libsystem_kernel's generated stubs now, as on Apple, and
# this dylib binds them from there.
MEMBERS="pthread_atfork.o pthread_cond.o pthread_cwd.o pthread_mutex.o pthread_rwlock.o pthread_tsd.o \
    pthread_main_thread_bootstrap.o pthread_tsd_platform_bootstrap.o \
    pthread_compat_extras.o \
    pthread_guard_compat.o pthread_simple_getenv.o pthread_platform_memcmp.o pthread_gettimeofday.o \
    pthread_mmap_compat.o pthread___mmap.o \
    pthread___munmap.o \
    pthread_thread_ops_compat.o pthread_clear_qos_tsd_compat.o"
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
# than adding another private copy -- and duplicating the MIG runtime is
# the exact bug this pass exists to remove. NOTE (DAR-153, 2026-09-01):
# the original wording here also claimed "_voucher_mach_msg_set really
# is libdispatch's symbol on real Apple too". That is wrong. Real
# Apple's libsystem_kernel.dylib exports its OWN real
# voucher_mach_msg_set (libsyscall/wrappers/_libc_funcptr.c), which just
# indirects through a function-pointer table that stays empty unless
# libdispatch installs it -- and mig stubs bind to THAT, never to
# libdispatch's. That symbol now really exists in this project's
# libsystem_kernel.dylib, and this dylib binds to it; see the link step
# below.
if [ ! -f "$OUT/libpthread_qos.a" ]; then
    echo "error: $OUT/libpthread_qos.a not found -- run build_libpthread_qos.sh first" >&2
    exit 1
fi
QOS_MEMBERS="qos.o libpthread_attr_compat.o libpthread_qos_compat.o host_privUser.o mach_voucherUser.o"
(cd "$OUT/libsystem_pthread/extracted" && ar x "../../libpthread_qos.a" $QOS_MEMBERS)
MEMBERS="$MEMBERS $QOS_MEMBERS"

# __kernelrpc_host_create_mach_voucher: REMOVED 2026-08-30 (static-
# linking cleanup, dylib-vs-dylib duplicate-symbol pass). This used to
# extract just this one routine out of libmach_ipc.a's mach_hostUser.o
# (which carries ~20 OTHER host_*() routines this dylib has no business
# exporting) via `ld -r -exported_symbol`. That narrowed copy was still
# a real, fully-external duplicate of libsystem_kernel.dylib's own real
# `__kernelrpc_mach_port_*`-tier MIG client-stub set (see that script's
# own comment on gaining "the whole __kernelrpc_mach_port_* MIG
# client-stub set" via a real mig regen) -- confirmed via nm -m that
# both copies are fully external, not private-extern. The only real
# consumer within this dylib, libpthread_qos_compat.o's voucher path
# (undefined reference, confirmed via nm), is a plain function call
# (not a direct-addressed data symbol like the rune-locale/mach_ipc_
# clock_init cases elsewhere in this pass), so it resolves fine via
# normal two-level-namespace dylib binding against the
# libsystem_kernel.dylib sibling already linked below -- no local copy
# needed at all here, unlike those data-symbol cases.

# Real gap: real pthread_rwlock.c/pthread_cond.c call sched_yield() in
# their real contended-lock backoff paths; this project's libpthread
# tier deliberately never compiled real pthread.c itself (see
# build_libpthread.sh's own header comment), so this one real exported
# symbol is supplied the same way libunwind_sched_yield.s/.c already do
# for libunwind's own, separate, still-static needs (see that pair's own
# header comments for the real swtch_pri(-59) trap + one-liner
# rationale) -- compiled fresh here rather than pulled from libunwind.a,
# which isn't a dylib dependency of this one.
# swtch_pri itself (sched_yield_trap.o) is libsystem_kernel's since
# DAR-416 step 1a (Apple's mach_traps.s); sched_yield.o binds it there.
rm -f "$OUT/libsystem_pthread/sched_yield_trap.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libunwind_sched_yield.c -o "$OUT/libsystem_pthread/sched_yield.o"

# Real gap: see libsystem_pthread_atfork_compat.c's own comment --
# pthread_atfork.o's real __pthread_atfork_child() needs these two real
# pthread.c symbols (a file this project's build_libpthread.sh
# deliberately never compiles), only ever reached from an actual fork()
# child, which no dylib-loaded process in this project ever calls.
# DAR-337: this file now includes libpthread's own internal.h (it needs
# the real struct pthread_s to re-init the main thread's lock -- see its
# header), so it is compiled with libpthread's own CFLAGS/include set,
# the same one build_libpthread.sh uses for pthread_atfork.c itself,
# rather than the bare -I libsyscall it used while it was only writing
# TSD slots.
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
    libsystem_pthread_atfork_compat.c -o "$OUT/libsystem_pthread/atfork_compat.o"

# DAR-225: fills the main thread's stackaddr/stackbottom from the kernel's
# apple[] main_stack= string (see the file's own header). Needs the real
# struct pthread_s, so same libpthread CFLAGS/include set as atfork_compat.o.
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
    libsystem_pthread_main_stack_compat.c -o "$OUT/libsystem_pthread/main_stack_compat.o"

# Real gap: see libsystem_pthread_dylib_bootstrap_compat.c's own comment --
# nothing ever called pthread_main_thread_bootstrap() for a real, dynamic-
# executable (dyld-linked) consumer of this dylib; every static consumer's
# own hand-rolled entry point calls it explicitly, but dyld jumps straight
# to main() with no such wrapper. Real dylib constructor closes the gap.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libsystem_pthread_dylib_bootstrap_compat.c -o "$OUT/libsystem_pthread/dylib_bootstrap_compat.o"

# DAR-280: real pthread_cond_wait()/pthread_cond_timedwait() trampolines
# (see pthread_cond_wait_public.c's own header) - replaces a raw
# `-Wl,-alias,__pthread_cond_wait,_pthread_cond_wait` that used to sit on
# the link line below. That alias was a real, root-caused bug, not a
# narrower version of this: aliasing the public 2-arg symbol directly
# onto the internal 5-arg implementation meant a normal
# pthread_cond_wait(cond, mutex) caller left abstime/isRelative/conforming
# (x2/x3/x4) as whatever garbage those registers already held, and the
# internal function's `if (abstime)` guard only skips its timeout-conversion
# block for a genuine NULL - a real SIGSEGV dereferencing a garbage
# `abstime->tv_nsec`, confirmed via a real QEMU crash disassembled back to
# that exact instruction. This needs internal.h's own include path.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    pthread_cond_wait_public.c -o "$OUT/libsystem_pthread/cond_wait_public.o"

mkdir -p "$OUT/system"
rm -f "$OUT/system/libsystem_pthread.dylib"
# Real UNIX2003/raw-syscall-naming aliases real pthread source calls by
# these exact names -- same class of gap build_libsystem_malloc.sh's own
# comment already documents (real dylib symbol resolution doesn't
# flatten across image boundaries the way static-archive linking did,
# so each dylib needs its own copy of whichever aliases its own real
# source actually calls).
#
# _pthread_cond_init added 2026-08-30 (static-linking cleanup, Phase
# 1d): real libpthread.a only ever defines the `$UNIX2003` spelling, but
# the iOS SDK's own <pthread.h> emits plain `_pthread_cond_init` call
# sites, so every consumer that wanted it was forced to statically bundle
# libpthread.a for that one symbol -- which is exactly why libxml2.dylib
# carried a private copy of the entire pthread mutex/cond tier. The alias
# base IS local to this link (it comes from the extracted libpthread.a
# members below), which is the precondition for -Wl,-alias actually
# landing in the real exports trie -- see docs/backlog.md item d10.
    # -not_for_dyld_shared_cache: real, newly-surfaced requirement of
    # the daily-driver Xcode's newer ld (NEWLD_BINDIR above) -- it
    # refuses "-undefined dynamic_lookup"/-U on a dylib it classifies
    # as shared-cache eligible (real /usr/lib install path) unless this
    # is set, a real check Xcode 12's ld64-609 never enforced. This
    # project builds no real dyld shared cache at all, so opting every
    # real dylib out is correct, not a workaround (docs/backlog.md item
    # d9's NEWLD_BINDIR sweep, 2026-08-28).
#
# _mmap/_munmap/_pthread_testcancel$UNIX2003 unexported 2026-08-30
# (static-linking cleanup, dylib-vs-dylib duplicate-symbol pass): this
# dylib is BELOW libdyld.dylib/libsystem_c.dylib in the real dependency
# order (pthread cannot link either back), so real pthread source's own
# calls to `mmap()`/`munmap()`/the UNIX2003 testcancel spelling
# genuinely need local aliases -- but nothing OUTSIDE this dylib should
# ever bind to pthread's copies instead of libdyld.dylib's/
# libsystem_c.dylib's real ones. `_pthread_testcancel` itself (the
# PLAIN, non-suffixed name) is NOT unexported here: this dylib IS real
# Apple's actual home for that one (the duplicate is libncurses.dylib's
# own separate copy -- a leaf dylib that CAN link this one back,
# deferred to its own pass rather than folded in here). Same
# `ld -r`-adjacent `-Wl,-unexported_symbol` narrowing this project
# already uses in narrow_rune_locale.sh/build_libsystem_malloc.sh.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libsystem_pthread.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-alias,___mmap,_mmap \
    -Wl,-alias,___munmap,_munmap \
    -Wl,-unexported_symbol,_mmap -Wl,-unexported_symbol,_munmap \
    -Wl,-alias,'_pthread_cond_init$UNIX2003',_pthread_cond_init \
    -Wl,-alias,_pthread_testcancel,'_pthread_testcancel$UNIX2003' \
    -Wl,-unexported_symbol,'_pthread_testcancel$UNIX2003' \
    -Wl,-unexported_symbol,___gettimeofday -Wl,-unexported_symbol,_gettimeofday \
    -Wl,-alias,'_pthread_mutexattr_destroy$UNIX2003',_pthread_mutexattr_destroy \
    -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003',__pthread_rwlock_rdlock \
    -Wl,-alias,'_pthread_rwlock_wrlock$UNIX2003',__pthread_rwlock_wrlock \
    -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003',__pthread_rwlock_unlock \
    `# _voucher_mach_msg_set is NO LONGER deferred (DAR-153, 2026-09-01).` \
    `# This dylib carries its own real mig-generated host_get_special_port/` \
    `# host_privUser/mach_voucherUser client stubs, each of which emits real` \
    `# Apple's own weak_import \`if (voucher_mach_msg_set != NULL)\`` \
    `# guard before its mach_msg() send. Left undefined, that bound` \
    `# straight through to libdispatch.dylib's REAL voucher_mach_msg_set(),` \
    `# skipping the _libkernel_voucher_functions table indirection real` \
    `# Apple relies on -- a real, deterministic recursive-dispatch_once` \
    `# SIGTRAP in syslogd (see` \
    `# libsystem_kernel_voucher_funcptr_compat.c's own header for the full` \
    `# symbolicated trace). libsystem_kernel.dylib, linked as a real` \
    `# sibling below, now owns the real Apple definition, so these stubs` \
    `# bind two-level against it exactly as they do on real Apple.` \
    -Wl,-U,_mig_strncpy \
    `# ___isthreaded: pre-existing gap found while rebuilding this dylib` \
    `# for DAR-13 (unrelated to that ticket's memcpy/memset scope, fixed` \
    `# here only because it blocked the required relink). pthread_create()` \
    `# (pthread_main_thread_bootstrap.c, real Apple behavior -- flips` \
    `# __isthreaded once a second thread actually spawns) now writes this` \
    `# real global, whose one real owner is libc_gen.a's libc_gen_shims.o` \
    `# (see that file's own header) -- now part of libsystem_c.dylib. This` \
    `# dylib cannot link libsystem_c.dylib directly (real circular` \
    `# dependency, see this file's own header comment: pthread is BELOW` \
    `# libsystem_c.dylib), so same real "bottom of stack, defer to flat` \
    `# namespace" pattern as _mig_strncpy above -- resolved at runtime` \
    `# since libsystem_c.dylib is always loaded alongside via` \
    `# libSystem.B.dylib.` \
    -Wl,-U,___isthreaded \
    -o "$OUT/system/libsystem_pthread.dylib" \
    $(for m in $MEMBERS; do echo "$OUT/libsystem_pthread/extracted/$m"; done) \
    "$OUT/libsystem_pthread/sched_yield.o" \
    "$OUT/libsystem_pthread/atfork_compat.o" \
    "$OUT/libsystem_pthread/dylib_bootstrap_compat.o" \
    "$OUT/libsystem_pthread/main_stack_compat.o" \
    "$OUT/libsystem_pthread/cond_wait_public.o" \
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
