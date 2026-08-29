#!/bin/bash
# Builds libc_build/system/libsystem_kernel.dylib: the first real,
# separate per-library dylib in the static-to-dylib migration flagged in
# docs/daemon-roadmap.md's "Static-vs-dylib architecture debt" section
# (user directive 2026-08-25). Real Apple's libsystem_kernel.dylib is the
# raw syscall/Mach-trap layer -- this is a narrow real subset of it: just
# the Mach-bootstrap/VM-trap surface that caused two real, independent
# bugs this project already hit (see daemon-roadmap.md's "Phase D: CLOSED
# for real" section) because build_libdyld.sh and build_libsystem.sh each
# bundled their OWN private static copy of mach_init.c, giving each dylib
# a separate, independently-never-initialized mach_task_self_ instead of
# real Apple's ONE shared copy. This script does NOT attempt the full BSD
# syscall surface (open/read/write/etc) yet -- those still work correctly
# as static-archive copies today (no observed bug from THAT duplication,
# since they carry no comparable cross-call mutable state) and migrating
# them is separate, future work, not bundled into this narrower step.
#
# Real, unmodified Apple source (third_party/xnu-7195/libsyscall/mach/):
# mach_init.c (mach_task_self_, mach_init()/mach_init_doit()),
# mach_vm.c (mach_vm_allocate/deallocate/protect/map -- the real
# trap-first, MIG-fallback wrappers), mig_reply_port.c (reply-port
# cleanup), and third_party/xnu-7195/libsyscall/wrappers/
# init_cpu_capabilities.c (mach_init.c's own real dependency). Plus this
# project's own libmalloc_vm_traps.s (the hand-written Mach-trap stubs
# create-syscalls.pl's BSD-only generator doesn't cover -- see that
# file's own header comment for the real trap numbers, cross-checked
# against osfmk/mach/syscall_sw.h) and libmalloc_vm_mig_stubs.c (real but
# provably-unreachable MIG-fallback dead code mach_vm.c's source
# unconditionally references -- see docs/backlog.md).
#
# Reuses build_libmalloc.sh's already-generated MIG header
# ($OUT/malloc_mig/mach/mach_vm_internal.h, from real osfmk/mach/
# mach_vm.defs) and hand-typed compat headers (mach_vm.h/
# vm_map_internal.h/mach_reply_port_decl.h under $OUT/compat_include)
# instead of re-deriving them -- same real header graph
# build_libmalloc.sh's own copy of these files already compiles against,
# not guessed twice.
set -e
cd "$(dirname "$0")"

XNU="../../third_party/xnu-7195"
LIBC="../../third_party/Libc"
OUT="libc_build"
mkdir -p "$OUT/libsystem_kernel"

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
for f in "$OUT/malloc_mig/mach/mach_vm_internal.h" \
         "$OUT/compat_include/mach/mach_vm.h" "$OUT/compat_include/mach/vm_map_internal.h" \
         "$OUT/compat_include/mach_reply_port_decl.h"; do
    if [ ! -f "$f" ]; then
        echo "error: $f not found -- run build_libmalloc.sh first (this script reuses its real generated MIG header + hand-typed compat headers)" >&2
        exit 1
    fi
done

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I $XNU/libsyscall/mach -idirafter $XNU/osfmk -I $LIBC/include -I $LIBC/darwin -I $OUT/compat_include \
    -I $OUT/malloc_mig"

"$CLANG" $CFLAGS "$XNU/libsyscall/mach/mach_vm.c" -o "$OUT/libsystem_kernel/mach_vm.o"
"$CLANG" $CFLAGS -include "$OUT/compat_include/mach_reply_port_decl.h" -I "$XNU/libsyscall" \
    "$XNU/libsyscall/mach/mach_init.c" -o "$OUT/libsystem_kernel/mach_init.o"
"$CLANG" $CFLAGS -include "$OUT/compat_include/mach_reply_port_decl.h" -I "$XNU/libsyscall" -I "$XNU/libsyscall/os" \
    "$XNU/libsyscall/mach/mig_reply_port.c" -o "$OUT/libsystem_kernel/mig_reply_port.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 -I "$XNU/osfmk" -I "$LIBC/include" -I "$LIBC/darwin" -I "$OUT/compat_include" \
    "$XNU/libsyscall/wrappers/init_cpu_capabilities.c" -o "$OUT/libsystem_kernel/init_cpu_capabilities.o"

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c libmalloc_vm_traps.s -o "$OUT/libsystem_kernel/libmalloc_vm_traps.o"
if [ ! -f libmalloc_vm_mig_stubs.c ]; then
    echo "error: libmalloc_vm_mig_stubs.c not found (real, provably-unreachable MIG-fallback dead code build_libmalloc.sh already established -- see docs/backlog.md)" >&2
    exit 1
fi
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 -I "$OUT/malloc_mig" -I "$OUT/compat_include" \
    libmalloc_vm_mig_stubs.c -o "$OUT/libsystem_kernel/libmalloc_vm_mig_stubs.o"

# Real gap: mach_init.c calls pthread_set_self() (real libpthread isn't
# linked into this narrowly-scoped dylib) and mig_reply_port.c's
# mig_dealloc_reply_port() calls mach_port_deallocate()/
# mach_port_mod_refs() (real mach_port.c needs a MIG-generated header
# this project's build doesn't produce, see libmalloc_vm_traps.s's own
# comment on the same gap for the _trap-suffixed versions). Same real
# fix build_libmalloc.sh's libmalloc_dispatch.c already established for
# its OWN, separate static-archive consumers -- copied here rather than
# linking the whole of libmalloc_dispatch.c (which also carries a lot of
# malloc-specific glue with no business being in the kernel-trap dylib).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libsystem_kernel_compat.c -o "$OUT/libsystem_kernel/compat.o"

# Real gap found 2026-08-25 auditing the libplatform/libclosure migration
# (docs/daemon-roadmap.md's "Static-vs-dylib architecture debt" section)
# for the same duplicate-global-state risk that caused mach_task_self_'s
# two bugs closing Phase D: cerror_stub.c's `int errno` (this project's
# deliberate single-plain-global simplification of real Apple's TSD-based
# errno -- see that file's own comment) was, until now, compiled as a
# LOOSE object linked directly into every dylib/binary that needs it
# (grep -rl cerror_stub.o *.sh), including build_libdyld.sh and
# build_libsystem.sh separately -- the exact same "two private copies of
# one process-global" shape as mach_task_self_, just not yet hit by an
# actual crash (no test here has yet exercised a syscall failure in one
# dylib whose errno a caller then reads from a different one). Moved
# here as the one real, shared copy every dylib consumer resolves
# against instead. Static, single-image binaries (bash, ls, cron, etc)
# are unaffected -- they never had a duplication problem in the first
# place (one process, one image, one errno already).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    cerror_stub.c -o "$OUT/libsystem_kernel/cerror_stub.o"

# Real gap found batch 2 (static-to-dylib migration step 3, docs/
# daemon-roadmap.md): real dyld's own _dyld_initializer() unconditionally
# calls pthread_atfork() to register its own fork handlers, which real,
# unmodified pthread_atfork.c needs a real `_os_alloc_once_table`
# instance for (libplatform/private/os/alloc_once_impl.h's own extern
# array every os_alloc_once() consumer expects SOMETHING to define).
# Real Apple source for exactly this -- third_party/xnu-7195/libsyscall/
# os/alloc_once.c, NOT the same file as libplatform/src/os/alloc_once.c
# already in libsystem_platform.dylib (that one only has the
# _os_alloc_once()/_os_alloc_slow() logic; this one's own comment says
# "Keep in sync with libplatform alloc_once.c" -- it's real Apple's
# actual separate table-definition half of the same subsystem, split
# across the two repos) -- compiled here as real, unmodified source, not
# a hand-rolled stand-in.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    "$XNU/libsyscall/os/alloc_once.c" -o "$OUT/libsystem_kernel/os_alloc_once_table.o"

# geteuid/getegid: plain generated BSD syscall traps, added 2026-08-29
# (static-linking cleanup, Phase 1a). Real Apple puts every raw syscall
# trap in libsystem_kernel.dylib and these are no exception -- but this
# project only ever had them as incidental members of unrelated archives
# (libpthread.a's pthread_getegid.o, syslogd_libc_extras.a's geteuid.o),
# so each image that needed one statically bundled its own private copy:
# libbsm, libcopyfile, libopenpam and libremovefile all export _geteuid
# and/or _getuid today for exactly that reason. Surfaced by moving
# pthread_thread_ops_compat.o (real pthread_getugid_np(), which calls
# both) into libsystem_pthread.dylib.
#
# libsystem_c.dylib would be the other plausible home, and is where the
# same create-syscalls.pl recipe already lives -- but it links
# libsystem_pthread.dylib, so putting them there would make the one
# consumer that needs them depend on its own dependent. libsystem_kernel
# is both the correct home and the acyclic one.
#
# Same real create-syscalls.pl generator + recipe build_libsystem_c.sh
# already established (see its own header comment); regenerating here
# rather than sharing output because the two scripts are independently
# runnable by design.
# libmach_ipc.a's Mach MIG user-stub runtime, moved here 2026-08-29
# (static-linking cleanup, Phase 1a). This archive is the real generated
# MIG client side of clock.defs/mach_host.defs/task.defs plus
# mig_strncpy, mach_host_self and the clock-port cache -- i.e. exactly
# the surface real Apple exports from libsystem_kernel.dylib. This
# project instead had libdispatch.dylib, libxpc.dylib, libc++.dylib and
# libncurses.dylib each statically bundling their own private copy, ~30
# duplicated symbols apiece.
#
# Two of those duplicates were real duplicate STATE, not just duplicate
# code: mach_ipc_clock_init.o's `_g_mach_ipc_clock_port` and
# `_g_mach_ipc_clock_sem` are plain globals caching a real Mach port and
# semaphore, so every image that bundled the archive was lazily
# initializing and holding its own separate port right -- the same shape
# as the mach_task_self_ and g_zone bugs already fixed in this tree.
#
# Confirmed via nm before doing this, not assumed: the archive has ZERO
# undefined symbols that this dylib (plus libsystem_platform/malloc)
# doesn't already resolve.
#
# Three members are excluded, each for a real reason:
#   - mach_msg_real.o / mach_ipc_traps.o: this script already compiles
#     the very same real sources directly (above), so including the
#     archive copies would be a genuine duplicate-definition link error.
#   - mach_ipc_dead_stubs.o: already known-dead (see
#     build_libdispatch_dylib.sh's own comment, which excluded it for the
#     same reason) -- its _mach_port_extract_right/_insert_right/_unguard
#     stand-ins are superseded, and this dylib already defers all three
#     via -Wl,-U below. Its fourth symbol, _voucher_mach_msg_set, is
#     genuinely libdispatch's on real Apple too, so it gets the same
#     deferral rather than another stand-in here.
if [ ! -f "$OUT/libmach_ipc.a" ]; then
    echo "error: $OUT/libmach_ipc.a not found -- run build_libmach_ipc.sh first" >&2
    exit 1
fi
# Mach port-rights / semaphore / mach_error tier, moved here 2026-08-29
# (static-linking cleanup, Phase 1a). Every source below is a real Mach
# trap stub or a thin wrapper over one, i.e. squarely this dylib's job --
# but each was being compiled separately into libdispatch.dylib AND
# libxpc.dylib (and, for the mach_port_allocate family, libiokituser.dylib
# too), so a process loading two of them had two copies of each.
#
# Three of these (_mach_port_insert_right/_extract_right/_unguard) were
# additionally on this dylib's own -Wl,-U deferral list, i.e. this dylib
# was punting to flat-namespace lookup for symbols it should have owned
# outright. Those deferrals are removed below now that the real bodies
# are here.
#
# Sources are compiled directly rather than reused from
# $OUT/xpc_syscall_obj/ or $OUT/cf_obj/ deliberately: this dylib is the
# bottom of the stack, and reusing those would make it depend on
# build_libxpc_syscalls.sh / build_corefoundation.sh, inverting the real
# dependency order. Compile flags match those scripts' own recipes
# exactly (CFLAGS_XNU / CFLAGS_MACH_PORT there).
CFLAGS_XNU_TRAPS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I "$XNU/libsyscall/mach" -idirafter "$XNU/osfmk")
CFLAGS_MACH_PORT=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I "$XNU/libsyscall/mach" -I "$XNU/libsyscall/os" -idirafter "$XNU/osfmk" \
    -I "$LIBC/include" -I "$LIBC/darwin")

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c mach_semaphore_traps.s \
    -o "$OUT/libsystem_kernel/mach_semaphore_traps.o"
"$CLANG" "${CFLAGS_XNU_TRAPS[@]}" mach_semaphore_wrap_compat.c \
    -o "$OUT/libsystem_kernel/semaphore_wrap.o"
"$CLANG" "${CFLAGS_XNU_TRAPS[@]}" "$XNU/libsyscall/mach/mach_error_string.c" \
    -o "$OUT/libsystem_kernel/mach_error_string.o"
"$CLANG" "${CFLAGS_XNU_TRAPS[@]}" mach_error_string_minimal_compat.c \
    -o "$OUT/libsystem_kernel/mach_error_string_minimal_compat.o"
"$CLANG" "${CFLAGS_MACH_PORT[@]}" mach_port_gap_closures2.c \
    -o "$OUT/libsystem_kernel/mach_port_gap_closures2.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c mach_port_gap_closures2.s \
    -o "$OUT/libsystem_kernel/mach_port_gap_closures2_traps.o"
"$CLANG" "${CFLAGS_MACH_PORT[@]}" cf_mach_port_syscalls.c \
    -o "$OUT/libsystem_kernel/cf_mach_port_syscalls.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c mach_port_traps.s \
    -o "$OUT/libsystem_kernel/mach_port_traps.o"

MACH_IPC_EXTRACT="$OUT/libsystem_kernel/mach_ipc_extracted"
rm -rf "$MACH_IPC_EXTRACT"
mkdir -p "$MACH_IPC_EXTRACT"
(cd "$MACH_IPC_EXTRACT" && ar x ../../libmach_ipc.a && \
    rm -f __.SYMDEF* mach_msg_real.o mach_ipc_traps.o mach_ipc_dead_stubs.o)

SYSGEN="$(mktemp -d)"
trap 'rm -rf "$SYSGEN"' EXIT
ARCHS=arm64 perl "$XNU/libsyscall/xcodescripts/create-syscalls.pl" \
    "$XNU/bsd/kern/syscalls.master" \
    "$XNU/libsyscall/custom" \
    "$XNU/libsyscall/Platforms" \
    iPhoneOS \
    "$SYSGEN" >/dev/null
MACSDK="$(xcode-select -p)/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"
for s in _geteuid _getegid _getuid _pread _pwrite; do
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
        -I "$XNU/libsyscall/custom" -I "$XNU/libsyscall/mach" \
        -idirafter "$XNU/osfmk" -idirafter "$MACSDK/usr/include" \
        "$SYSGEN/${s}.s" -o "$OUT/libsystem_kernel/sys${s}.o"
done

# Real gap found+fixed 2026-08-28 booting launchd as PID 1 with real
# libdispatch.dylib loaded alongside it: real mach_msg()/mach_msg_trap()
# (mach_msg_real.c/mach_ipc_traps.s, this project's own first real
# userland Mach-IPC exerciser, Phase 4.6 item 5) were only ever compiled
# as loose objects bundled into libmach_ipc.a -- both launchd.macho's own
# static portion AND libdispatch.dylib's own separate static bundling of
# that same archive (build_libdispatch_dylib.sh's `ar x ../libmach_ipc.a`
# step) each got their OWN private copy, and launchd's real
# _kernelrpc_mach_port_move_member() call (generated MIG client code,
# also statically linked into launchd.macho) resolved its own mach_msg()
# reference to libdispatch.dylib's copy instead of its own local one --
# confirmed via a real instrumentation pass (temporary prints added to a
# scratch copy of mach_msg(), verified they never fired) -- exact same
# "two private copies of one process-global[-ish]" bug class as
# mach_task_self_'s Phase D bugs and this session's own mach_init.o
# fix above, just for a FUNCTION instead of a data symbol. Real Apple
# architecture ships mach_msg/mach_msg_trap/mach_reply_port/etc as part
# of libsystem_kernel.dylib -- this project's own libsystem_kernel.dylib
# already hosts mach_reply_port/task_self_trap (libmalloc_vm_traps.o
# above) but never mach_msg itself. Real, correct fix: build it here too,
# so every real consumer (launchd.macho, libdispatch.dylib, anything
# else) shares this ONE real, canonical copy instead of each bundling
# their own -- matching how mach_task_self_ was fixed, at the actual
# architectural root rather than patching each consumer separately.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I "$XNU/libsyscall/mach" -idirafter "$XNU/osfmk" -I "$LIBC/include" -I "$LIBC/darwin" \
    -include mach_msg_trap_decl.h \
    mach_msg_real.c -o "$OUT/libsystem_kernel/mach_msg_real.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    mach_ipc_traps.s -o "$OUT/libsystem_kernel/mach_ipc_traps.o"

mkdir -p "$OUT/system"
rm -f "$OUT/system/libsystem_kernel.dylib"
    # -not_for_dyld_shared_cache: real, newly-surfaced requirement of
    # the daily-driver Xcode's newer ld (NEWLD_BINDIR above) -- it
    # refuses "-undefined dynamic_lookup"/-U on a dylib it classifies
    # as shared-cache eligible (real /usr/lib install path) unless this
    # is set, a real check Xcode 12's ld64-609 never enforced. This
    # project builds no real dyld shared cache at all, so opting every
    # real dylib out is correct, not a workaround (docs/backlog.md item
    # d9's NEWLD_BINDIR sweep, 2026-08-28).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libsystem_kernel.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-U,_voucher_mach_msg_set \
    `# mach_ipc_clock_init.o (from libmach_ipc.a, above) calls abort(),` \
    `# memcpy() and pthread_tsd_shim_bootstrap(). All three really live` \
    `# in libsystem_c.dylib/libsystem_pthread.dylib, both of which link` \
    `# THIS dylib -- so linking them back here would be a dependency` \
    `# cycle, not a fix. Deferred to flat-namespace lookup instead, which` \
    `# is safe for these three specifically: nothing in this project ever` \
    `# loads libsystem_kernel.dylib without libSystem.B.dylib (which` \
    `# re-exports both) also in the process.` \
    -Wl,-U,_abort -Wl,-U,_memcpy -Wl,-U,_pthread_tsd_shim_bootstrap \
    `# The 12 __kernelrpc_mach_port_* MIG fallbacks below are the` \
    `# slow-path half of cf_mach_port_syscalls.o's real trap-first/` \
    `# MIG-fallback pairs (moved here this same pass). Their real bodies` \
    `# are MIG-generated mach_portUser.o, which this project currently` \
    `# produces only in build_dyld_core.sh / build_libxpc_syscalls2.sh --` \
    `# both far above this dylib in the dependency order, so pulling` \
    `# either here would invert it. Deferred with exactly the same` \
    `# mechanism and for exactly the same reason libdispatch.dylib` \
    `# already documents in its own UNDEF_SYMS_ELSEWHERE_IN_PROCESS list;` \
    `# generating mach_port.defs here directly is the real follow-up.` \
    -Wl,-U,__kernelrpc_mach_port_allocate -Wl,-U,__kernelrpc_mach_port_construct \
    -Wl,-U,__kernelrpc_mach_port_destruct -Wl,-U,__kernelrpc_mach_port_extract_member \
    -Wl,-U,__kernelrpc_mach_port_extract_right -Wl,-U,__kernelrpc_mach_port_get_attributes \
    -Wl,-U,__kernelrpc_mach_port_insert_member -Wl,-U,__kernelrpc_mach_port_insert_right \
    -Wl,-U,__kernelrpc_mach_port_kernel_object -Wl,-U,__kernelrpc_mach_port_request_notification \
    -Wl,-U,__kernelrpc_mach_port_type -Wl,-U,__kernelrpc_mach_port_unguard \
    `# mach_msg_real.o's mach_msg_destroy_port() references these on its` \
    `# own provably-unreachable MAKE_SEND/MAKE_SEND_ONCE reply cases (see` \
    `# mach_msg_real.c's own header) -- deliberately left undefined here` \
    `# rather than adding another local definition, to avoid re-creating` \
    `# the exact duplicate-symbol risk this whole fix exists to close;` \
    `# real implementations already live in launchd.macho's own` \
    `# mach_port_gap_closures2.o and libdispatch.dylib's own equivalent.` \
    -o "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/libsystem_kernel/mach_vm.o" "$OUT/libsystem_kernel/mach_init.o" "$OUT/libsystem_kernel/mig_reply_port.o" \
    "$OUT/libsystem_kernel/init_cpu_capabilities.o" "$OUT/libsystem_kernel/libmalloc_vm_traps.o" \
    "$OUT/libsystem_kernel/libmalloc_vm_mig_stubs.o" "$OUT/libsystem_kernel/compat.o" \
    "$OUT/libsystem_kernel/cerror_stub.o" "$OUT/libsystem_kernel/os_alloc_once_table.o" \
    "$OUT/libsystem_kernel/mach_msg_real.o" "$OUT/libsystem_kernel/mach_ipc_traps.o" \
    "$OUT/libsystem_kernel/sys_geteuid.o" "$OUT/libsystem_kernel/sys_getegid.o" \
    "$OUT/libsystem_kernel/sys_getuid.o" \
    "$OUT/libsystem_kernel/sys_pread.o" "$OUT/libsystem_kernel/sys_pwrite.o" \
    "$OUT/libsystem_kernel/mach_semaphore_traps.o" "$OUT/libsystem_kernel/semaphore_wrap.o" \
    "$OUT/libsystem_kernel/mach_error_string.o" "$OUT/libsystem_kernel/mach_error_string_minimal_compat.o" \
    "$OUT/libsystem_kernel/mach_port_gap_closures2.o" "$OUT/libsystem_kernel/mach_port_gap_closures2_traps.o" \
    "$OUT/libsystem_kernel/cf_mach_port_syscalls.o" "$OUT/libsystem_kernel/mach_port_traps.o" \
    "$MACH_IPC_EXTRACT"/*.o

echo "wrote $OUT/system/libsystem_kernel.dylib"
otool -hv "$OUT/system/libsystem_kernel.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
if nm -u "$OUT/system/libsystem_kernel.dylib" | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u "$OUT/system/libsystem_kernel.dylib" >&2
else
    echo "libsystem_kernel.dylib: fully self-contained, zero undefined symbols"
fi
