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
#
# _fileport_makefd/_fileport_makeport added 2026-08-31 (static-linking
# cleanup, dylib-vs-dylib duplicate-symbol pass): same exact shape as
# geteuid/getegid above -- real BSD syscalls #430/#431
# (bsd/kern/syscalls.master: sys_fileport_makeport/sys_fileport_makefd),
# real Apple's own home is libsystem_kernel.dylib, but this project only
# ever had hand-written stand-ins in libsystem_notify.dylib
# (notify_dyld_compat.c -- its own header comment is stale, dated "this
# project's fully-static (no dyld) build") and libxpc.dylib
# (sys__fileport_makefd.o/sys__fileport_makeport.o), each a private
# copy with no canonical owner. Confirmed via create-syscalls.pl's own
# real output that it generates `_fileport_makefd.s`/
# `_fileport_makeport.s` (not `sys_`-prefixed -- same convention as
# every other entry in this loop).
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
#     via -Wl,-U below. Its fourth symbol, _voucher_mach_msg_set, used
#     to get the same deferral on the stated premise that it "is
#     genuinely libdispatch's on real Apple too" -- WRONG, corrected
#     2026-09-01 (DAR-153). Real Apple's libsystem_kernel.dylib has its
#     OWN real definition (libsyscall/wrappers/_libc_funcptr.c), inert
#     until libdispatch installs a voucher table; deferring it here made
#     every MIG stub in this dylib call libdispatch's real one directly
#     and caused a real recursive-dispatch_once crash. Now compiled in
#     below from real Apple source -- see
#     libsystem_kernel_voucher_funcptr_compat.c's own header.
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
# DAR-124/DAR-8/DAR-22, 2026-08-31 sweep continuation: real Apple home for
# _thread_get_special_reply_port is this dylib (a plain, 2-instruction
# Mach trap stub, kernel_trap(thread_get_special_reply_port,-50,0) --
# same real trap-number source, osfmk/mach/syscall_sw.h:133, as every
# other hand-written trap .s compiled just above). Until this fix it was
# a genuine, confirmed-broken flat-namespace `-Wl,-U` deferral in
# build_libdispatch_dylib.sh, resolved only because launchd.macho happens
# to compile its own local copy (build_launchd_core.sh's
# thread_get_special_reply_port_trap.o) -- a real, currently-shipped
# second consumer, syslogd.macho (loads libdispatch.dylib/libsystem_
# notify.dylib/libxpc.dylib directly, confirmed via otool -L, and does
# NOT define this symbol anywhere in its own object code, confirmed via
# nm -g), would hit "symbol not found in flat namespace
# '_thread_get_special_reply_port'" the instant it's actually spawned.
# Reused verbatim from tools/userland_staging/thread_get_special_reply_
# port_trap.s (the exact same file launchd_core's own copy compiles) --
# one real source, now two real object-file instances (this dylib's, and
# launchd.macho's own pre-existing local one) until a future session
# also drops launchd_core's copy in favor of this dylib's (left alone
# this round -- launchd.macho defining it locally is harmless, already-
# tracked Phase 2b binary-shadowing debt, not a new bug).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c thread_get_special_reply_port_trap.s \
    -o "$OUT/libsystem_kernel/thread_get_special_reply_port_trap.o"

# Real mig regen of osfmk/mach/mach_port.defs (client side), moved here
# 2026-08-30 (static-linking cleanup, Phase 1d). This is the follow-up
# the -Wl,-U block below used to describe as "the real fix": the 12
# __kernelrpc_mach_port_* MIG fallbacks cf_mach_port_syscalls.o's real
# trap-first/MIG-fallback pairs call were deferred to flat-namespace
# lookup here, and their real bodies existed only in libxpc.dylib and
# libiokituser.dylib -- two owners of the same 40+ generated stubs, in
# two images, with libsystem_kernel (their real Apple home) owning
# neither.
#
# There was never a real dependency problem, only a sourcing one: the
# other two scripts got mach_portUser.o by running `mig` themselves, and
# `mig` needs nothing but xnu-7195's own .defs file, which is already
# this dylib's own source tree. Generating it directly here inverts
# nothing. Same invocation and same CFLAGS as
# build_libxpc_syscalls2.sh's (default `_kernelrpc_` UserPrefix applies
# since LIBSYSCALL_INTERFACE is not defined) -- not re-derived.
MIG="$(which mig)"
if [ -z "$MIG" ]; then
    echo "error: 'mig' not found on PATH -- needed to generate mach_port.defs client stubs" >&2
    exit 1
fi
mkdir -p "$OUT/libsystem_kernel/mach_port_mig"
"$MIG" -arch arm64 -I"$XNU/osfmk" \
    -header "$OUT/libsystem_kernel/mach_port_mig/mach_port.h" \
    -user "$OUT/libsystem_kernel/mach_port_mig/mach_portUser.c" \
    -server /dev/null -sheader /dev/null \
    "$XNU/osfmk/mach/mach_port.defs"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I "$XNU/libsyscall/mach" -idirafter "$XNU/osfmk" -I "$OUT/libsystem_kernel/mach_port_mig" \
    "$OUT/libsystem_kernel/mach_port_mig/mach_portUser.c" \
    -o "$OUT/libsystem_kernel/mach_portUser.o"

# mach_port_set_attributes: the one mach_port_*() public wrapper this
# dylib still didn't own after the MIG regen above. Its only definition
# in this tree lived in libiokituser.dylib's own mach_port_wrappers_real.o
# (and, separately, in launchd.macho's launchd_mach_port_compat.o) --
# real Apple ships it in libsystem_kernel like every sibling wrapper, so
# it comes here and libiokituser drops the whole object.
#
# Narrowed with `ld -r -exported_symbol` rather than linked whole,
# because mach_port_wrappers_real.c ALSO defines mach_port_allocate and
# mach_port_get_attributes, which cf_mach_port_syscalls.o (linked below)
# already provides from real mach_port.c -- linking it entire is a
# genuine duplicate-symbol error, confirmed by trying it. Same narrowing
# precedent as this script's own kernelrpc_host_create_mach_voucher
# extraction in build_libsystem_pthread.sh and libdispatch's vm_mapUser.o.
#
# Deliberately NOT done by appending the wrapper to
# cf_mach_port_syscalls.c: that file is shared with
# build_corefoundation.sh (libCoreFoundation.a's own source), and
# liblaunchd_core.a's launchd_mach_port_compat.o also defines
# mach_port_set_attributes. Post-DAR-195, libCoreFoundation.dylib no
# longer statically embeds into launchd.macho -- but liblaunchd_core.a
# still links directly into launchd.macho, so the same real
# duplicate-symbol collision would still occur if this wrapper were
# added to the shared cf_mach_port_syscalls.c file instead of narrowed
# here. Adding it there would turn a dylib duplicate into a hard link
# error one layer up.
"$CLANG" "${CFLAGS_MACH_PORT[@]}" -I "$OUT/libsystem_kernel/mach_port_mig" \
    mach_port_wrappers_real.c -o "$OUT/libsystem_kernel/mach_port_wrappers_raw.o"
"$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld" -r -arch arm64 \
    -exported_symbol _mach_port_set_attributes \
    "$OUT/libsystem_kernel/mach_port_wrappers_raw.o" \
    -o "$OUT/libsystem_kernel/mach_port_set_attributes.o"
rm -f "$OUT/libsystem_kernel/mach_port_wrappers_raw.o"

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
# BSD socket family added 2026-08-31 (static-linking cleanup,
# dylib-vs-dylib duplicate-symbol pass): `_socket`/`_writev` are plain
# generated stubs (real Apple's own public name, confirmed via a real
# create-syscalls.pl run, not guessed); `___chmod`/`___connect_nocancel`/
# `___recvmsg_nocancel`/`___sendmsg_nocancel`/`___select_nocancel`/
# `___sendto_nocancel` are the real raw traps the NO_SYSCALL_STUB public
# wrappers below call (chmod/connect/recvmsg/sendmsg/select/sendto are
# each marked NO_SYSCALL_STUB in bsd/kern/syscalls.master, meaning real
# Apple needs real hand-written C logic on top, not just a generated
# asm stub -- see the real wrapper compiles below).
# _fpathconf added 2026-08-31 (static-linking cleanup, "needs a new
# home" batch): plain fpathconf(2), syscall #192, not NO_SYSCALL_STUB
# (same real Perl-generated-stub shape as every other entry in this
# loop) -- real canonical home, previously owned independently by
# libcopyfile.dylib (a real generated stub, build_copyfile.sh's own
# create-syscalls.pl invocation) and libncurses.dylib (a hand-rolled
# inline-asm reimplementation, ncurses_extra_syscalls.c) with neither
# being libsystem_kernel.dylib/libsystem_c.dylib -- real Apple's actual
# home for a plain raw syscall wrapper. See build_copyfile_dylib.sh's
# and build_ncurses_dylib.sh's own updated comments for their halves.
# _setegid/_seteuid/_getgroups/_setitimer added 2026-09-01 (DAR-153,
# syslogd boot investigation): each a plain generated stub, no
# NO_SYSCALL_STUB marker in syscalls.master (#182/#183/#79/#83) --
# same shape as _geteuid/_getegid/_getuid already in this loop.
# libopenpam.dylib's own `-Wl,-U` deferrals for setegid/seteuid/
# getgroups (build_openpam_dylib.sh, "real, confirmed genuinely absent
# from every archive in this checkout" at the time) and
# build_libc_gen.sh's new alarm.c (needs real setitimer()) are the real
# callers driving these -- found one dyld abort at a time
# (`_alarm` first, since it's earliest in that dylib's own fixup
# chain), not added speculatively: `nm -m libopenpam.dylib` shows all
# four as genuinely undefined+dynamically-looked-up before this fix.
# _flock added 2026-09-02 (DAR-153 session, real QEMU boot found while
# verifying an unrelated concurrent DAR-195 launchctl fix): plain
# generated stub, syscall #131 (`AUE_FLOCK`, no NO_SYSCALL_STUB marker
# in syscalls.master), same shape as every other entry in this loop.
# Real dyld abort: "Symbol not found: _flock / Referenced from:
# /bin/launchctl / Expected in: /usr/lib/system/libdispatch.dylib" --
# libdispatch.dylib's own real source calls flock(2) directly and this
# project never had a real provider anywhere, confirmed via `nm -m
# libdispatch.dylib` showing it dynamically-looked-up before this fix.
# _getgid added 2026-09-06 (DAR-167, notifyd boot investigation): plain
# generated stub, syscall #47 (`AUE_GETGID`, no NO_SYSCALL_STUB marker in
# syscalls.master), same shape as _geteuid/_getegid/_getuid already in
# this loop -- a real oversight (those three were added, plain getgid()
# never was) rather than a deliberate omission. libbsm.dylib's real
# audit_token_to_pid()-adjacent path calls getgid() directly;
# notifyd.macho links libbsm.dylib directly (build_notifyd.sh) and, since
# this project's default -fixup_chains link mode resolves every bind
# eagerly at process launch rather than lazily on first call, this
# aborted every dynamic-executable launch that pulled in libbsm.dylib,
# not just the callers that actually invoke it. Found the same way
# DAR-153/DAR-163's own gaps above were: a real kernel-side
# abort_with_payload() probe (bsd/kern/kern_exit.c's
# abort_with_payload_internal(), gated behind -user_fault_debug) printing
# dyld's own real "symbol not found in flat namespace '_getgid'" message,
# since notifyd's stderr (where dyld's own halt()/console() output
# really goes) is never wired to /dev/console for a launchd MachServices
# job or a bare interactive-shell exec alike.
for s in _geteuid _getegid _getuid _getgid _pread _pwrite _fileport_makefd _fileport_makeport \
    _socket _writev ___chmod ___connect_nocancel ___recvmsg_nocancel ___sendmsg_nocancel \
    ___select_nocancel ___sendto_nocancel ___recvfrom_nocancel _fpathconf _kevent _kqueue ___sigaction \
    _setegid _seteuid _getgroups _setitimer _flock; do
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
        -I "$XNU/libsyscall/custom" -I "$XNU/libsyscall/mach" \
        -idirafter "$XNU/osfmk" -idirafter "$MACSDK/usr/include" \
        "$SYSGEN/${s}.s" -o "$OUT/libsystem_kernel/sys${s}.o"
done

# ___sigaction (DAR-164, real gap found root-causing syslogd/pd-console-
# login's post-exec abort): the raw sigaction(2) trap (#46, plain
# generated stub -- syscalls.master has no NO_SYSCALL_STUB marker on it),
# real canonical home per real Apple architecture. Real Libc/sys/
# sigaction.c (wired into libsystem_c.dylib by this same fix, see
# build_libsystem_c.sh's own updated comment) calls libplatform's
# __platform_sigaction(), which calls this raw trap plus __sigtramp
# (compiled just below) directly by name -- confirmed via a real QEMU
# boot: syslogd.macho and pd-console-login (the first real fork()+exec()'d
# non-launchd dyld processes this port has ever booted, DAR-163) each
# abort() immediately post-exec with dyld's own real fatal error
# ("Symbol not found: _sigaction ... Expected in: /usr/lib/libSystem.B.dylib",
# "Symbol not found: __sigaction_nobind ... Expected in:
# /usr/lib/system/libdispatch.dylib") -- captured via a real kernel-side
# abort_with_payload() probe (bsd/kern/kern_exit.c), not guessed. Both
# reexport this dylib transitively (libSystem.B.dylib and libdispatch.dylib
# each real-reexport libsystem_c.dylib, confirmed via otool -l), so this
# dylib owning the raw trap + trampoline and libsystem_c.dylib owning the
# public sigaction()/_sigaction_nobind() wrappers matches real Apple's
# actual split and closes both binaries' bindings with one real fix.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    libsystem_kernel_sigtramp_compat.c -o "$OUT/libsystem_kernel/sigtramp_compat.o"

# proc_pidinfo() (DAR-164, real gap found root-causing syslogd's
# post-exec abort, same investigation as ___sigaction/__sigtramp above):
# real, self-contained proc_info(2) (#336, NO_SYSCALL_STUB) wrapper,
# already compiled and QEMU-proven once for ps(1)
# (ps_libproc_syscall.c, build_ps.sh) -- reused verbatim, not
# reinvented. Real canonical home is this dylib (real Apple ships
# proc_pidinfo as part of libsystem_kernel.dylib, a plain syscall
# wrapper, same tier as every other hand-written NO_SYSCALL_STUB trap
# here). Found via the same real kernel-side abort_with_payload() probe:
# "Symbol not found: _proc_pidinfo / Referenced from: /usr/sbin/syslogd /
# Expected in: /usr/lib/libSystem.B.dylib" -- syslogd.macho also directly
# loads libxpc.dylib (which carries its own private copy,
# cf_libproc_syscalls.c-derived), but that doesn't satisfy this specific
# two-level ordinal, same "Expected in: X" != "loaded somewhere" pattern
# already diagnosed twice this session (see build_libdispatch_dylib.sh's
# and build_libsystem_notify.sh's own updated comments). libSystem.B.dylib
# already reexports this dylib, so no further wiring needed once this
# dylib exports it for real.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    ps_libproc_syscall.c -o "$OUT/libsystem_kernel/proc_pidinfo_compat.o"

# voucher_mach_msg_set()/clear()/adopt()/revert() + __libkernel_voucher_
# init(): real Apple libsyscall source (libsyscall/wrappers/
# _libc_funcptr.c), real canonical home is exactly this dylib. Added
# 2026-09-01 (DAR-153) -- this dylib previously deferred
# _voucher_mach_msg_set via -Wl,-U on the stated premise that it "is
# genuinely libdispatch's on real Apple too". That premise was wrong and
# it caused a real, deterministic recursive-dispatch_once SIGTRAP in
# syslogd; see libsystem_kernel_voucher_funcptr_compat.c's own header
# for the full root-cause trace and why real Apple's table indirection
# (inert until libdispatch installs a voucher table, which this build's
# libdispatch never does) is the thing that was missing.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    -I "$XNU/libsyscall/mach" -idirafter "$XNU/osfmk" \
    libsystem_kernel_voucher_funcptr_compat.c \
    -o "$OUT/libsystem_kernel/voucher_funcptr_compat.o"

# DAR-124/DAR-11 (libresolv.dylib), 2026-08-31 sweep continuation:
# _kevent/_kqueue added above (plain generated stubs, syscalls.master
# has no NO_SYSCALL_STUB marker on either -- confirmed by grep, not
# guessed) -- real Apple canonical home for both. Found via a real
# DAR-124 audit that libresolv.dylib (unconditionally reexported by
# libSystem.B.dylib -- i.e. loaded into literally every process this
# project ships, not just launchd) carries real, confirmed chained-
# fixups binds to `<flat-namespace>/_kevent`/`_kqueue`/`_pipe`/
# `___darwin_check_fd_set_overflow`, none of which libsystem_kernel.
# dylib (already a linked sibling) previously defined. `_kevent`/
# `_kqueue` happened to be silently safe only for the 76
# build_simple_cmds.sh binaries (each carries its own private LINK_COMMON
# copy) and anything else that happens to define them locally --
# NOT a real fix, since any OTHER real or future consumer that merely
# loads libSystem.B.dylib (the umbrella every process needs) with no
# such local copy would hit "symbol not found in flat namespace" the
# instant libresolv.dylib loads, whether or not it ever calls kevent()/
# kqueue() itself. Real Apple's own canonical home for both is this
# dylib; giving it real ownership fixes this for every current and
# future consumer at once, not one binary at a time. See _pipe's own
# separate fix just below (NO_SYSCALL_STUB, can't go through this same
# generated-stub loop) and build_libresolv_dylib.sh's updated header for
# the other half of this fix (dropping the now-stale -Wl,-U deferrals).

# pipe(2): NO_SYSCALL_STUB (the real kernel trap returns TWO fds via
# x0/x1, not one -- a case create-syscalls.pl's generic single-return
# generator above can't produce), so real Apple ships a hand-written
# arm64 implementation instead: $XNU/libsyscall/custom/__pipe.s, the
# exact same real source build_simple_cmds.sh already compiles once for
# `ed`'s own popen()/pclose() (see that script's own ed_pipe_trap.o
# comment) -- reused verbatim here too, now for real Apple's actual
# intended owner (same real "needs a new home" finding as _kevent/
# _kqueue above).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -arch arm64 \
    -I "$XNU/libsyscall/custom" -I "$XNU/libsyscall/mach" \
    -idirafter "$XNU/osfmk" -idirafter "$MACSDK/usr/include" \
    "$XNU/libsyscall/custom/__pipe.s" -o "$OUT/libsystem_kernel/pipe_trap.o"
# __pipe.s (above) only exports the private-indirection name `___pipe`
# (real Apple naming convention, same as __lchown/__platform_sigaction);
# the real public `_pipe` name needs a thin C forwarder on top -- reused
# verbatim from tar_pipe_compat.c (see that file's own header for the
# full real trail: originally written for tar/ed/diff/less's own
# real pipe(2) callers after the old static-linking-era `-alias ___pipe
# _pipe` LINK_ALIASES directive was dropped). Found needed here the same
# way: a first attempt at this fix compiled __pipe.s alone and confirmed
# via `nm -gUj` that libsystem_kernel.dylib still had no `_pipe` export
# at all, only `___pipe`.
"$CLANG" "${CFLAGS_XNU_TRAPS[@]}" tar_pipe_compat.c -o "$OUT/libsystem_kernel/pipe_forwarder.o"

# Real public-name wrappers on top of the raw traps above:
#   - wrappers/unix03/chmod.c: real, unmodified Apple source, already
#     proven to compile cleanly in this exact toolchain (build_getty.sh/
#     build_simple_cmds.sh's mkdir/touch/cp/mv/chmod/gzip/compress/tar
#     entries all already compile this same file verbatim) -- needs
#     `-I .../libsyscall/wrappers` for its own quoted `#include
#     "_errno.h"` (lives one directory up from unix03/, confirmed by
#     the same -I flag those other scripts already use).
#   - xpc_syscall_wrappers_compat.c/xpc_select_compat.c: real, already
#     proven, already-QEMU-verified curated source
#     build_libxpc_syscalls.sh's own header comment documents in full
#     (Apple's real legacy/{connect,recvmsg,sendmsg}.c and
#     select-base.c bodies, curated only to drop the `_NONSTD_SOURCE`
#     preprocessor scaffolding this project's LP64/arm64-only
#     __DARWIN_ONLY_UNIX_CONFORMANCE SDK target rejects -- confirmed via
#     a real trial compile, not guessed) -- reused verbatim from the
#     same shared tools/userland_staging/ directory, not duplicated.
#   - libsystem_kernel_sendto_compat.c: new this session, same real
#     curation shape as the two files above, for sendto()/send() (see
#     that file's own header for the full citation).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I "$XNU/libsyscall/mach" -idirafter "$XNU/osfmk" -I "$LIBC/include" -I "$LIBC/darwin" -I "$OUT/compat_include" \
    -I "$OUT/malloc_mig" -I "$XNU/libsyscall/wrappers" \
    "$XNU/libsyscall/wrappers/unix03/chmod.c" -o "$OUT/libsystem_kernel/chmod_wrap.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I "$XNU/libsyscall/mach" -idirafter "$XNU/osfmk" -I "$LIBC/include" -I "$LIBC/darwin" -I "$OUT/compat_include" \
    -I "$OUT/malloc_mig" \
    xpc_syscall_wrappers_compat.c -o "$OUT/libsystem_kernel/xpc_syscall_wrappers_compat.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I "$XNU/libsyscall/mach" -idirafter "$XNU/osfmk" -I "$LIBC/include" -I "$LIBC/darwin" -I "$OUT/compat_include" \
    -I "$OUT/malloc_mig" \
    xpc_select_compat.c -o "$OUT/libsystem_kernel/select_wrap.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I "$XNU/libsyscall/mach" -idirafter "$XNU/osfmk" -I "$LIBC/include" -I "$LIBC/darwin" -I "$OUT/compat_include" \
    -I "$OUT/malloc_mig" \
    libsystem_kernel_sendto_compat.c -o "$OUT/libsystem_kernel/sendto_wrap.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I "$XNU/libsyscall/mach" -idirafter "$XNU/osfmk" -I "$LIBC/include" -I "$LIBC/darwin" -I "$OUT/compat_include" \
    -I "$OUT/malloc_mig" \
    libsystem_kernel_recvfrom_compat.c -o "$OUT/libsystem_kernel/recvfrom_wrap.o"

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

# nanosleep() added 2026-08-31 (static-linking cleanup, "needs a new
# home" batch): simple_cmds_nanosleep_pub_compat.c (this project's real,
# already-QEMU-verified Mach-IPC nanosleep port -- see that file's own
# header for the full algorithm citation) had no single real owner
# before this -- libc++.dylib and libncurses.dylib each independently
# compiled their own copy. Its two data-symbol dependencies
# (_g_mach_ipc_clock_port/_g_mach_ipc_clock_sem, via _mach_ipc_clock_init)
# are ALREADY this dylib's own real canonical copies (MACH_IPC_EXTRACT's
# mach_ipc_clock_init.o above, per build_ncurses_dylib.sh's own comment:
# "this dylib already links libsystem_kernel.dylib as a sibling below,
# which owns the real canonical copy") -- so compiling the function
# itself here too, instead of in a sibling that has to reach back for
# those globals, matches real Apple's own architecture (nanosleep is a
# Libsyscall/libsystem_kernel-tier custom wrapper, not a plain Libc
# "gen" function -- see libsystem_c_sleep_compat.c's own header for the
# other half of this same investigation, sleep() itself).
#
# __semwait_signal deferred via -Wl,-U below (see the final link
# command): the only other real, already-working copy of this raw
# syscall trap is libsystem_malloc.dylib's own sys_semwait_signal.o
# (build_libmalloc.sh), and this dylib is BELOW libsystem_malloc.dylib
# in real dependency order (malloc already links kernel as a sibling;
# the reverse would be circular) -- same "flat-namespace, always loaded
# alongside" reasoning this script's own -Wl,-U,_abort/_memcpy/
# _pthread_tsd_shim_bootstrap already use just below, not a new pattern.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -I "$XNU/osfmk" -I ../../generated \
    simple_cmds_nanosleep_pub_compat.c -o "$OUT/libsystem_kernel/nanosleep_compat.o"

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
    `# mach_ipc_clock_init.o (from libmach_ipc.a, above) calls abort(),` \
    `# memcpy() and pthread_tsd_shim_bootstrap(). All three really live` \
    `# in libsystem_c.dylib/libsystem_pthread.dylib, both of which link` \
    `# THIS dylib -- so linking them back here would be a dependency` \
    `# cycle, not a fix. Deferred to flat-namespace lookup instead, which` \
    `# is safe for these three specifically: nothing in this project ever` \
    `# loads libsystem_kernel.dylib without libSystem.B.dylib (which` \
    `# re-exports both) also in the process.` \
    -Wl,-U,_abort -Wl,-U,_memcpy -Wl,-U,_pthread_tsd_shim_bootstrap \
    `# __semwait_signal: nanosleep_compat.o's own real dependency, see` \
    `# that compile step's own comment just above -- real canonical copy` \
    `# is libsystem_malloc.dylib's sys_semwait_signal.o, always loaded` \
    `# alongside this dylib (same class of deferral as the 3 just above).` \
    -Wl,-U,___semwait_signal \
    `# BSD socket family additions, 2026-08-31 (static-linking cleanup,` \
    `# dylib-vs-dylib duplicate-symbol pass). chmod/connect/recvmsg/` \
    `# writev's $UNIX2003 aliases are created HERE (base is genuinely` \
    `# local to this link) rather than in libresolv.dylib (their` \
    `# previous real home) specifically to avoid the documented ld64` \
    `# "-alias nonlocal base" bug -- libresolv_more_socket_syscalls.c's/` \
    `# libresolv_socket_syscalls.c's own plain chmod()/connect()/` \
    `# recvmsg()/writev() are now guarded out of the dylib build (see` \
    `# those files' own comments) so this is genuinely the one place` \
    `# left creating these aliases.` \
    -Wl,-alias,_chmod,'_chmod$UNIX2003' -Wl,-alias,_connect,'_connect$UNIX2003' \
    -Wl,-alias,_recvmsg,'_recvmsg$UNIX2003' -Wl,-alias,_writev,'_writev$UNIX2003' \
    `# _send$UNIX2003 added 2026-09-01 (DAR-153, syslogd boot` \
    `# investigation): real gap found via a real QEMU boot -- oldsyslog.c` \
    `# (openpam's own real syslog(3) fallback, __DARWIN_UNIX03=1 asm-labels` \
    `# its plain send() call to this versioned symbol) had it` \
    `# "dynamically looked up" with zero real provider anywhere in the` \
    `# process, same shape as connect/writev above. Base (sendto_wrap.o's` \
    `# own send(), compiled earlier in this same script/link -- see` \
    `# libsystem_kernel_sendto_compat.c) is genuinely local to this` \
    `# link, same nonlocal-base requirement as every other alias here.` \
    -Wl,-alias,_send,'_send$UNIX2003' \
    `# _recv$UNIX2003 added 2026-09-02 (DAR-181, real gap found via a` \
    `# real QEMU boot): Libinfo/lookup.subproj/si_compare.c's real` \
    `# RFC6724 source-address probe calls plain recv(), which` \
    `# __DARWIN_UNIX03=1 asm-labels to this versioned symbol, same shape` \
    `# as _send$UNIX2003 just above. Base (recvfrom_wrap.o's own recv(),` \
    `# compiled earlier in this same script/link -- see` \
    `# libsystem_kernel_recvfrom_compat.c) is genuinely local to this` \
    `# link, same nonlocal-base requirement as every other alias here.` \
    -Wl,-alias,_recv,'_recv$UNIX2003' \
    `# _nanosleep$UNIX2003 added 2026-08-31 ("needs a new home" batch):` \
    `# real Apple genuinely exports both the plain and $UNIX2003 names` \
    `# from libsystem_kernel.dylib (the SDK's own <time.h> __DARWIN_ALIAS_C` \
    `# macro asm-labels any UNIX03-compiled caller's direct nanosleep()` \
    `# call to the versioned symbol -- confirmed by libsystem_c_sleep_` \
    `# compat.o's own real link failure needing exactly this before this` \
    `# alias was added). Base is genuinely local to this link` \
    `# (nanosleep_compat.o, just above), same nonlocal-base requirement as` \
    `# every other alias here.` \
    -Wl,-alias,_nanosleep,'_nanosleep$UNIX2003' \
    `# _sendto unexported (kept private-extern, not removed): libresolv.a` \
    `# still owns the real, public canonical PLAIN+$UNIX2003 _sendto pair` \
    `# (its own local base is required there for its own -Wl,-alias, same` \
    `# nonlocal-base constraint as above) -- this dylib's own copy exists` \
    `# ONLY so send() here has something to call locally, and must not` \
    `# become a second public owner. See libsystem_kernel_sendto_compat.c` \
    `# and libresolv_socket_syscalls.c's own comments for the full trail.` \
    -Wl,-unexported_symbol,_sendto \
    `# The 12 __kernelrpc_mach_port_* -Wl,-U deferrals that used to sit` \
    `# here are GONE (2026-08-30, static-linking cleanup Phase 1d): their` \
    `# real bodies -- MIG-generated mach_portUser.o -- are now generated` \
    `# and linked directly above, so this dylib owns them outright the way` \
    `# real Apple's libsystem_kernel does, instead of punting to whichever` \
    `# of libxpc.dylib/libiokituser.dylib happened to be loaded.` \
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
    "$OUT/libsystem_kernel/sys_getuid.o" "$OUT/libsystem_kernel/sys_getgid.o" \
    "$OUT/libsystem_kernel/sys_pread.o" "$OUT/libsystem_kernel/sys_pwrite.o" \
    "$OUT/libsystem_kernel/sys_fileport_makefd.o" "$OUT/libsystem_kernel/sys_fileport_makeport.o" \
    "$OUT/libsystem_kernel/sys_socket.o" "$OUT/libsystem_kernel/sys_writev.o" \
    "$OUT/libsystem_kernel/sys___chmod.o" "$OUT/libsystem_kernel/sys___connect_nocancel.o" \
    "$OUT/libsystem_kernel/sys___recvmsg_nocancel.o" "$OUT/libsystem_kernel/sys___sendmsg_nocancel.o" \
    "$OUT/libsystem_kernel/sys___select_nocancel.o" "$OUT/libsystem_kernel/sys___sendto_nocancel.o" \
    "$OUT/libsystem_kernel/sys___recvfrom_nocancel.o" \
    "$OUT/libsystem_kernel/sys_fpathconf.o" "$OUT/libsystem_kernel/nanosleep_compat.o" \
    "$OUT/libsystem_kernel/sys_kevent.o" "$OUT/libsystem_kernel/sys_kqueue.o" \
    "$OUT/libsystem_kernel/pipe_trap.o" "$OUT/libsystem_kernel/pipe_forwarder.o" \
    "$OUT/libsystem_kernel/chmod_wrap.o" "$OUT/libsystem_kernel/xpc_syscall_wrappers_compat.o" \
    "$OUT/libsystem_kernel/select_wrap.o" "$OUT/libsystem_kernel/sendto_wrap.o" \
    "$OUT/libsystem_kernel/recvfrom_wrap.o" \
    "$OUT/libsystem_kernel/mach_semaphore_traps.o" "$OUT/libsystem_kernel/semaphore_wrap.o" \
    "$OUT/libsystem_kernel/mach_error_string.o" "$OUT/libsystem_kernel/mach_error_string_minimal_compat.o" \
    "$OUT/libsystem_kernel/mach_port_gap_closures2.o" "$OUT/libsystem_kernel/mach_port_gap_closures2_traps.o" \
    "$OUT/libsystem_kernel/cf_mach_port_syscalls.o" "$OUT/libsystem_kernel/mach_port_traps.o" \
    "$OUT/libsystem_kernel/thread_get_special_reply_port_trap.o" \
    "$OUT/libsystem_kernel/mach_portUser.o" "$OUT/libsystem_kernel/mach_port_set_attributes.o" \
    "$OUT/libsystem_kernel/sys___sigaction.o" "$OUT/libsystem_kernel/sigtramp_compat.o" \
    "$OUT/libsystem_kernel/proc_pidinfo_compat.o" \
    "$OUT/libsystem_kernel/voucher_funcptr_compat.o" \
    "$OUT/libsystem_kernel/sys_setegid.o" "$OUT/libsystem_kernel/sys_seteuid.o" \
    "$OUT/libsystem_kernel/sys_getgroups.o" "$OUT/libsystem_kernel/sys_setitimer.o" \
    "$OUT/libsystem_kernel/sys_flock.o" \
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
