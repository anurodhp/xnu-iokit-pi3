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
    -Wl,-U,_mach_port_insert_right -Wl,-U,_mach_port_extract_right -Wl,-U,_mach_port_unguard \
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
    "$OUT/libsystem_kernel/mach_msg_real.o" "$OUT/libsystem_kernel/mach_ipc_traps.o"

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
