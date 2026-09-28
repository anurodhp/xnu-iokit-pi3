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
    -DCERROR_STUB_LIBKERNEL_FUNCPTR cerror_stub.c -o "$OUT/libsystem_kernel/cerror_stub.o"

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
    -exported_symbol _mach_port_get_refs \
    "$OUT/libsystem_kernel/mach_port_wrappers_raw.o" \
    -o "$OUT/libsystem_kernel/mach_port_set_attributes.o"
rm -f "$OUT/libsystem_kernel/mach_port_wrappers_raw.o"

# mig_reply_setup() (DAR-251): IOKitLib.c (libiokituser.dylib) calls it,
# and nothing owned it. Real libsyscall/mach source, one function, linked
# whole. (mach_port_get_refs, IOKitLib's other gap, comes from
# mach_port_wrappers_real.c above.)
"$CLANG" "${CFLAGS_MACH_PORT[@]}" \
    "$XNU/libsyscall/mach/mig_reply_setup.c" -o "$OUT/libsystem_kernel/mig_reply_setup.o"

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
# X11 client libraries (DAR-383, 2026-09-26): link #9, pathconf #191,
# readv #120, poll #230, getsockopt #118, shutdown #134, umask #60 are
# plain generated stubs (bsd/kern/syscalls.master, no NO_SYSCALL_STUB
# marker); listen #106 and accept #30 ARE NO_SYSCALL_STUB, and on this
# UNIX-conformance-only arm64 target the generator's ___listen.s/
# ___accept.s themselves carry the public `.set _listen, ___listen` /
# `.set _accept, ___accept` aliases (read from the generated .s, not
# assumed) -- libsyscall/wrappers/legacy/{listen,accept}.c are
# #ifndef NO_SYSCALL_LEGACY code that real Apple does not build for
# arm64 either. Real Apple exports all nine from libsystem_kernel. Before
# this, no dylib exported pathconf/readv/poll/shutdown/umask at all, and
# link/listen/accept/getsockopt only as incidental private copies
# (libncurses.dylib, libCoreFoundation.dylib); libXau, libxcb, libX11,
# libICE and libXt reference them (tools/userland_staging/build_libX*.sh).
# Font/2D stack (DAR-384, 2026-09-26): utimes #138, shmat #262, shmdt
# #264, shmget #265 are plain generated stubs (bsd/kern/syscalls.master);
# shmctl #263 is NO_SYSCALL_STUB, and, like ___listen/___accept, the
# generated ___shmctl.s carries the public `.set _shmctl, ___shmctl` alias
# itself on arm64 (read from the generated .s; the arm64 syscall.map
# entry is `_shmctl ___shmctl`). Real Apple exports all five from
# libsystem_kernel; no dylib here exported any of them. fontconfig's
# fccache.c calls utimes() to touch a cache directory (works: #138 is
# unconditional in the kernel). cairo's xlib backend
# (cairo-xlib-surface-shm.c) probes the SysV shm calls for MIT-SHM images,
# Until DAR-407 (2026-09-26) the kernel had no SysV IPC at all:
# config/MASTER.arm64.bcm2837's BSD_BASE lacked sysv_shm, so #262-265 were
# nosys, which raises SIGSYS (bsd/kern/subr_xxx.c:183-188). DAR-407 added
# sysv_sem/sysv_msg/sysv_shm there, matching config/MASTER.arm64.MacOSX:23.
# SysV sem/msg (DAR-407, same change): the kernel now implements #251-261
# (bsd/kern/syscalls.master:364-400), so export the matching wrappers the
# way real Libsyscall does. _semget #255, _semop #256, _msgget #259,
# _msgsnd #260, _msgrcv #261 are plain generated stubs; ___msgsnd_nocancel/
# ___msgrcv_nocancel are the $NOCANCEL traps (syscall.map:22-23).
# ___semctl #254, ___msgctl #258, ___semsys #251, ___msgsys #252,
# ___shmsys #253 are NO_SYSCALL_STUB raw traps; ___msgctl.s carries
# `.set _msgctl, ___msgctl` itself (syscall.map:68). The public variadic
# semctl()/semsys()/msgsys()/shmsys() are Apple's own
# libsyscall/wrappers/varargs_wrappers.s (arm64 passes variadic args on
# the stack, so a plain stub would read garbage), compiled whole below.
# xnu-7195's copy of that file had a real bug -- _msgsys ended at
# POP_FRAME with no ARM64_STACK_EPILOG, so it fell through into _semsys;
# the fork carries upstream's one-line fix (third_party/xnu-latest's
# varargs_wrappers.s:89), and sysv_ipc_smoketest.c exercises msgsys().
# That file also defines sem_open()/shm_open() over ___sem_open #268 /
# ___shm_open #266, so those two raw traps are generated here too.
# ___accept/___listen/___socketpair/___getattrlist added 2026-09-25
# (DAR-346): all four are NO_SYSCALL_STUB in syscalls.master, and
# create-syscalls.pl emits them under the private names with the public
# name as an alias inside the stub (`.set _accept, ___accept`), exactly
# as Platforms/iPhoneOS/arm64/syscall.map maps them. On arm64 real Apple
# has no C wrapper on top: libsyscall builds with -DNO_SYSCALL_LEGACY
# (Libsyscall.xcconfig:27), so wrappers/legacy/{accept,listen,socketpair}.c
# (the EOPNOTSUPP->ENOTSUP versions) are not compiled. sshd used to carry
# its own raw copies (sshd_syscalls.c).
# ___getlogin/___setlogin (same change): the raw traps behind real
# Libc/gen/FreeBSD/getlogin.c and Libc/gen/setlogin.c, which
# build_libsystem_c.sh now compiles.
# DAR-387 (Xvfb, xkbcomp, libXfont2): _chdir #12, _readlink #58,
# _fchown #123, _getpgrp #81, _getppid #39, _setgid #181, _setuid #23 and
# _setpgid #82 are plain generated stubs (bsd/kern/syscalls.master). #124
# fchmod, #194 getrlimit, #195 setrlimit and #65 msync are
# NO_SYSCALL_STUB, so the generator emits the raw ___fchmod/___getrlimit/
# ___setrlimit/___msync traps; ___msync.s itself carries
# `.set _msync, ___msync`, and the other three get Apple's
# wrappers/unix03/{fchmod,getrlimit,setrlimit}.c below.
# _csops #169 and _csops_audittoken #170 (DAR-391: code-signing status and
# entitlement queries, syscalls.master:260-261) are plain stubs too; libxpc
# exported a non-owner _csops.
# _sigprocmask #48 (DAR-404, syscalls.master:101) is a plain stub as well.
# It used to be built into libsystem_c, so libsystem_platform's setjmp.s
# (which calls it) had to defer it flat-namespace to avoid a link cycle. Several of these
# names were already exported by dylibs that are not their owner
# (libncurses, libcopyfile, libSystemConfiguration, libCoreFoundation,
# libdispatch for _mach_continuous_time);
# those copies are left alone here.
for s in _geteuid _getegid _getuid _getgid _pread _pwrite _fileport_makefd _fileport_makeport \
    _socket _writev ___chmod ___connect_nocancel ___recvmsg_nocancel ___sendmsg_nocancel \
    ___select_nocancel ___sendto_nocancel ___recvfrom_nocancel _fpathconf _kevent _kqueue ___sigaction \
    _setegid _seteuid _getgroups _setitimer _flock _sync ___reboot ___getlogin _setsid ___setpriority _dup2 \
    _link _pathconf _readv _poll _getsockopt _shutdown _umask ___listen ___accept \
    _utimes _shmat _shmdt _shmget ___shmctl \
    _semget _semop ___semctl _msgget _msgsnd _msgrcv ___msgctl \
    ___msgsnd_nocancel ___msgrcv_nocancel ___semsys ___msgsys ___shmsys \
    ___sem_open ___shm_open \
    ___socketpair ___getattrlist ___setlogin \
    _chdir _readlink _fchown _getpgrp _getppid _setgid _setuid _setpgid \
    ___fchmod ___getrlimit ___setrlimit ___msync _csops _csops_audittoken _sigprocmask; do
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
        -I "$XNU/libsyscall/custom" -I "$XNU/libsyscall/mach" \
        -idirafter "$XNU/osfmk" -idirafter "$MACSDK/usr/include" \
        "$SYSGEN/${s}.s" -o "$OUT/libsystem_kernel/sys${s}.o"
done
# Apple's own variadic-ABI shims (see the DAR-407 comment above the loop):
# semctl, semsys, msgsys, shmsys, sem_open, shm_open.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    -I "$XNU/libsyscall/custom" -I "$XNU/libsyscall/mach" \
    -idirafter "$XNU/osfmk" -idirafter "$MACSDK/usr/include" \
    "$XNU/libsyscall/wrappers/varargs_wrappers.s" \
    -o "$OUT/libsystem_kernel/varargs_wrappers.o"

# ___sigaction (DAR-164, real gap found root-causing syslogd/pd-console-
# login's post-exec abort): the raw sigaction(2) trap (#46, plain
# generated stub -- syscalls.master has no NO_SYSCALL_STUB marker on it),
# real canonical home per real Apple architecture. Real Libc/sys/
# sigaction.c (wired into libsystem_c.dylib by this same fix, see
# build_libsystem_c.sh's own updated comment) calls libplatform's
# __platform_sigaction(), which calls this raw trap directly by name -- confirmed via a real QEMU
# boot: syslogd.macho and pd-console-login (the first real fork()+exec()'d
# non-launchd dyld processes this port has ever booted, DAR-163) each
# abort() immediately post-exec with dyld's own real fatal error
# ("Symbol not found: _sigaction ... Expected in: /usr/lib/libSystem.B.dylib",
# "Symbol not found: __sigaction_nobind ... Expected in:
# /usr/lib/system/libdispatch.dylib") -- captured via a real kernel-side
# abort_with_payload() probe (bsd/kern/kern_exit.c), not guessed. Both
# reexport this dylib transitively (libSystem.B.dylib and libdispatch.dylib
# each real-reexport libsystem_c.dylib, confirmed via otool -l), so this
# dylib owning the raw trap and libsystem_c.dylib owning the
# public sigaction()/_sigaction_nobind() wrappers matches real Apple's
# actual split and closes both binaries' bindings with one real fix.
# The trampoline is NOT here any more (DAR-405, 2026-09-27): the old
# libsystem_kernel_sigtramp_compat.c passed an SA_SIGINFO handler
# (sig, infostyle, sig). libplatform's real _sigtramp
# (src/setjmp/generic/sigtramp.c) and __platform_sigaction now live
# together in libsystem_platform, as on Apple (build_libsystem_platform.sh).

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
# (DAR-416 1b: the voucher thunks now come from Apple's own
# libsyscall/wrappers/_libc_funcptr.c; see the LIBKERNEL block below.
# libsystem_kernel_voucher_funcptr_compat.c is retired.)

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

# ptrace(2): same NO_SYSCALL_STUB story as __pipe.s above -- real Apple
# hand-writes it ($XNU/libsyscall/custom/__ptrace.s) because the wrapper
# must ZERO errno before the trap (ptrace legitimately returns -1 on
# success, so the caller distinguishes the two by errno).
#
# It belongs HERE, not in a consumer's own link, and that is load-bearing
# rather than tidiness (DAR-342): the arm64 body is
#     MI_GET_ADDRESS(x9,_errno)   ->   adrp x9, _errno@page
#                                      add  x9, x9, _errno@pageoff
# a DIRECT, same-image address computation (SYS.h:460) with no GOT-
# indirect form. Assembled into any image that does not itself define
# _errno it fails the link outright ("fixup error (kind=arm64_adrp_lo12)
# ... target '_errno' does not have address") -- and the way build_sshd.sh
# had silenced that was to link a loose cerror_stub.o alongside it, which
# handed sshd a SECOND private errno and caused DAR-342. Compiled here,
# next to this dylib's own cerror_stub.o errno, the direct reference is
# correct by construction and consumers just import _ptrace.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -arch arm64 \
    -I "$XNU/libsyscall/custom" -I "$XNU/libsyscall/mach" \
    -idirafter "$XNU/osfmk" -idirafter "$MACSDK/usr/include" \
    "$XNU/libsyscall/custom/__ptrace.s" -o "$OUT/libsystem_kernel/ptrace_trap.o"

# sigaltstack(2)/sigreturn(2) raw traps (DAR-383): both NO_SYSCALL_STUB
# (bsd/kern/syscalls.master:53 and :184), so, like __pipe/__ptrace above,
# real Apple hand-writes them: libsyscall/custom/__sigaltstack.s and
# __sigreturn.s, exporting ___sigaltstack / ___sigreturn. Needed by
# libplatform's real setjmp family, now built into
# libsystem_platform.dylib (see build_libsystem_platform.sh):
# setjmp/arm64/setjmp.s's _setjmp calls ___sigaltstack, and
# setjmp/generic/sigtramp.c's _sigunaltstack (called by _longjmp) calls
# ___sigreturn. Real Apple exports both from libsystem_kernel.
for t in __sigaltstack __sigreturn; do
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -arch arm64 \
        -I "$XNU/libsyscall/custom" -I "$XNU/libsyscall/mach" \
        -idirafter "$XNU/osfmk" -idirafter "$MACSDK/usr/include" \
        "$XNU/libsyscall/custom/${t}.s" -o "$OUT/libsystem_kernel/${t}_trap.o"
done
# DAR-416 step 1a: the syscall stubs, traps and Mach wrappers that real
# libpthread (pthread.c, pthread_cancelable.c, qos.c) calls. On Apple all
# of them are libsystem_kernel exports. Until now some were hand-written
# into libsystem_pthread's compat objects (bsdthread_*, psynch_*,
# __pthread_[f]chdir, __ulock_wait2, __thread_selfid, swtch_pri) and the
# rest existed nowhere. build_libsystem_pthread.sh drops its copies.
#  - generated plain stubs (bsd/kern/syscalls.master): #328 __pthread_kill,
#    #330 __sigwait, #331 __disable_threadsignal, #332 __pthread_markcancel,
#    #333 __pthread_canceled, #336 __proc_info, #202 __sysctl, #367/#368
#    __workq_open/__workq_kernreturn, #423 __semwait_signal_nocancel,
#    #360/#366/#478/#361 __bsdthread_create/register/ctl/terminate,
#    #301-#308,#312 __psynch_*, #348/#349 __pthread_[f]chdir, #329
#    __pthread_sigmask, #544 __ulock_wait2, #530 __kqueue_workloop_ctl.
#  - __gettimeofday/__thread_selfid: libsyscall/custom/*.s, which the
#    generator defers to, as for __pipe above.
#  - __thread_set_tsd_base: libsyscall/custom/custom.s.
#  - swtch_pri (trap -59, osfmk/mach/syscall_sw.h:140): Apple's
#    libsyscall/mach/mach_traps.s, narrowed with ld -r to that one symbol,
#    because this port's other traps come from hand-written .s files that
#    already export the rest (a full switch to mach_traps.s is separate).
#  - mach_thread_self: libsyscall/mach/mach_legacy.c:48, narrowed the same
#    way, since mach_host_self_real.c already provides its mach_host_self.
#  - thread_policy/thread_policy_set/thread_set_policy:
#    libsyscall/mach/thread_act.c, over the _kernelrpc_* MIG routines
#    already in thread_actUser.o.
#  - abort_with_reason and getpid: see the DAR-423 block below.
# Start clean: PT416_OBJS below is a glob, so an object this block no
# longer builds must not survive into the link.
rm -f "$OUT"/libsystem_kernel/pt416*.o
for s in ___disable_threadsignal ___proc_info ___pthread_canceled ___pthread_kill \
    ___pthread_markcancel ___semwait_signal_nocancel ___sigwait ___sysctl \
    ___workq_kernreturn ___workq_open ___bsdthread_create ___bsdthread_ctl \
    ___bsdthread_register ___bsdthread_terminate ___psynch_cvbroad ___psynch_cvclrprepost \
    ___psynch_cvsignal ___psynch_cvwait ___psynch_mutexdrop ___psynch_mutexwait \
    ___psynch_rw_rdlock ___psynch_rw_unlock ___psynch_rw_wrlock ___pthread_chdir \
    ___pthread_fchdir ___pthread_sigmask ___ulock_wait2 ___kqueue_workloop_ctl \
    ___gettimeofday; do
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
        -I "$XNU/libsyscall/custom" -I "$XNU/libsyscall/mach" \
        -idirafter "$XNU/osfmk" -idirafter "$MACSDK/usr/include" \
        "$SYSGEN/${s}.s" -o "$OUT/libsystem_kernel/pt416${s}.o"
done
for t in __thread_selfid custom; do
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -arch arm64 \
        -I "$XNU/libsyscall/custom" -I "$XNU/libsyscall/mach" \
        -idirafter "$XNU/osfmk" -idirafter "$MACSDK/usr/include" \
        "$XNU/libsyscall/custom/${t}.s" -o "$OUT/libsystem_kernel/pt416_${t}.o"
done
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -DPRIVATE=1 \
    -I "$XNU/osfmk" -I "$XNU/libsyscall/mach" -idirafter "$MACSDK/usr/include" \
    "$XNU/libsyscall/mach/mach_traps.s" -o "$OUT/libsystem_kernel/pt416_mach_traps_all.o"
"$CLANG" $CFLAGS -I "$XNU/libsyscall" -include "$XNU/osfmk/mach/mach_traps.h" "$XNU/libsyscall/mach/mach_legacy.c" \
    -o "$OUT/libsystem_kernel/pt416_mach_legacy_all.o"
LD_R="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
# Also __kernelrpc_mach_port_guard_trap/_move_member_trap and the two voucher
# traps (DAR-416 1b):
# mach_port.c, narrowed below for mach_voucher_extract_attr_recipe, still
# references them from its other functions.
"$LD_R" -r -arch arm64 -exported_symbol _swtch_pri \
    -exported_symbol __kernelrpc_mach_port_guard_trap -exported_symbol __kernelrpc_mach_port_move_member_trap \
    -exported_symbol _host_create_mach_voucher_trap -exported_symbol _mach_voucher_extract_attr_recipe_trap \
    "$OUT/libsystem_kernel/pt416_mach_traps_all.o" -o "$OUT/libsystem_kernel/pt416_swtch_pri.o"
"$LD_R" -r -arch arm64 -exported_symbol _mach_thread_self \
    "$OUT/libsystem_kernel/pt416_mach_legacy_all.o" -o "$OUT/libsystem_kernel/pt416_mach_thread_self.o"
# thread_act.c (and, for the voucher narrowing below, mach_port.c) include
# <mach/thread_act_internal.h> / <mach/mach_port_internal.h>, the MIG headers Apple
# generates without -DLIBSYSCALL_INTERFACE so it declares the _kernelrpc_
# routines (libsyscall/xcodescripts/mach_install_mig.sh:171-179, with
# thread_act.defs in MIGS_INTERNAL at :97-100). Apple runs it with a
# 32-bit MACHINE_ARCH (:49, for vm_map_internal.h's sake); this uses
# -arch arm64, which for thread_act.defs changes nothing but the
# prototypes' pointer widths, already arm64 here.
PT416_HDR="$PWD/$OUT/libsystem_kernel/pt416_internal_hdr"
PT416_XNU="$(cd "$XNU" && pwd)"
mkdir -p "$PT416_HDR/mach"
for d in thread_act mach_port; do
    (cd "$PT416_HDR" && "$(which mig)" -novouchers -arch arm64 -I"$PT416_XNU/osfmk" \
        -header "$PT416_HDR/mach/${d}_internal.h" -user /dev/null -server /dev/null \
        "$PT416_XNU/osfmk/mach/${d}.defs" >/dev/null)
done
"$CLANG" $CFLAGS -I "$PT416_HDR" -I "$XNU/libsyscall" -I "$XNU/libsyscall/mach" \
    "$XNU/libsyscall/mach/thread_act.c" -o "$OUT/libsystem_kernel/pt416_thread_act.o"
PT416_OBJS=$(ls "$OUT"/libsystem_kernel/pt416*.o | grep -v "_all\.o$")

# DAR-423: getpid, fork, os_fault_with_payload and the terminate/abort-
# with-reason family, all libsystem_kernel exports on Apple.
#  - ___getpid (with _getpid and the arm64 .globl __current_pid pid cache)
#    and ___fork, generated from libsyscall/custom/__getpid.s and
#    __fork.s. They must be linked into the same image, because __fork.s
#    updates __current_pid through a local adrp/lo12 pair
#    (MI_GET_ADDRESS, custom/__fork.s:144). Before DAR-423 both lived in
#    libsystem_malloc for that reason (build_libsystem_malloc.sh's old
#    header), and every getpid() caller bound libsystem_malloc.
#  - _os_fault_with_payload (#529) and ___terminate_with_payload/
#    ___abort_with_payload (#520/#521), plain generated stubs
#    (bsd/kern/syscalls.master:841 and nearby).
#  - libsyscall/wrappers/terminate_with_reason.c: abort_with_reason,
#    abort_with_payload, terminate_with_reason/payload, and
#    __darwin_check_fd_set_overflow.
# The stand-ins these replace are gone from libsystem_malloc,
# libsystem_notify (os_fault_with_payload), libsystem_pthread
# (abort_with_reason) and libsystem_c (__darwin_check_fd_set_overflow).
rm -f "$OUT"/libsystem_kernel/d423*.o
for s in ___getpid ___fork _os_fault_with_payload ___terminate_with_payload ___abort_with_payload; do
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
        -I "$XNU/libsyscall/custom" -I "$XNU/libsyscall/mach" \
        -idirafter "$XNU/osfmk" -idirafter "$MACSDK/usr/include" \
        "$SYSGEN/${s}.s" -o "$OUT/libsystem_kernel/d423${s}.o"
done
"$CLANG" $CFLAGS -I "$XNU/libsyscall" -I "$XNU/libsyscall/wrappers" -I "$XNU/bsd" -idirafter "$XNU/libkern" \
    "$XNU/libsyscall/wrappers/terminate_with_reason.c" -o "$OUT/libsystem_kernel/d423_terminate_with_reason.o"
# terminate_with_reason.c's __darwin_check_fd_set_overflow calls
# pthread_current_stack_contains_np, which Apple resolves inside this dylib
# through a _libc_funcptr.c thunk (the LIBKERNEL block below).
D423_OBJS=$(ls "$OUT"/libsystem_kernel/d423*.o)

# LIBKERNEL block (DAR-416 step 1b): Apple's libSystem init parity for this
# dylib.
#  - wrappers/_libkernel_init.c: __libkernel_init(fns, envp, apple, vars)
#    stores the libSystem function table and calls mach_init(). The libSystem
#    initializer calls it first, as Libsystem init.c:215 does
#    (libsystem_dyld_initializer_compat.c).
#  - wrappers/_libc_funcptr.c: the hidden thunks this dylib uses to reach
#    upward: malloc/free/realloc/reallocf, _pthread_exit_if_canceled,
#    _pthread_clear_qos_tsd, pthread_current_stack_contains_np,
#    voucher_mach_msg_*, and the string functions mapped onto the private
#    _libkernel_* copies. This replaces libsystem_kernel_voucher_funcptr_compat.c,
#    libsystem_kernel_stack_contains_funcptr_compat.c, cerror_stub.c's no-op
#    _pthread_exit_if_canceled, and the -U deferrals of _memcpy and
#    __pthread_clear_qos_tsd.
#  - wrappers/string/*.c: the _libkernel_* string layer. It is compiled with
#    libsyscall's own string/strings.h forced first, because it shares the
#    _STRINGS_H_ guard with the SDK's; and with the SDK's machine/limits.h
#    pre-included, because under -ffreestanding <limits.h> is clang's own,
#    which never includes arm/limits.h, where LONG_BIT (which strlen.c
#    needs) is defined.
#  - wrappers/spawn/posix_spawn.c plus the generated ___posix_spawn stub:
#    the posix_spawn family. No dylib exported it before (launchd carries a
#    private copy). It also provides posix_spawnattr_get/set_qos_clamp_np,
#    which libpthread's qos.c calls.
rm -f "$OUT"/libsystem_kernel/lk416*.o
LK_FLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -D_LIBC_NO_FEATURE_VERIFICATION -D_FORTIFY_SOURCE=0 -DPRIVATE=1 \
    -I $OUT/compat_include -include machine/limits.h -include $XNU/libsyscall/wrappers/string/strings.h \
    -I $XNU/libsyscall/wrappers/string -I $XNU/libsyscall/wrappers \
    -idirafter $XNU/bsd -idirafter $XNU/osfmk -idirafter $XNU/libsyscall/mach -idirafter $XNU/libsyscall \
    -idirafter $XNU/libkern"
for f in _libkernel_init _libc_funcptr; do
    "$CLANG" $LK_FLAGS "$XNU/libsyscall/wrappers/$f.c" -o "$OUT/libsystem_kernel/lk416_$f.o"
done
for f in "$XNU"/libsyscall/wrappers/string/*.c; do
    "$CLANG" $LK_FLAGS "$f" -o "$OUT/libsystem_kernel/lk416_str_$(basename "$f" .c).o"
done
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -D_LIBC_NO_FEATURE_VERIFICATION -D_FORTIFY_SOURCE=0 -DPRIVATE=1 \
    -idirafter "$XNU/bsd" -idirafter "$XNU/osfmk" -idirafter "$XNU/libsyscall/mach" -idirafter "$XNU/libsyscall" \
    -I "$XNU/libsyscall/wrappers/spawn" "$XNU/libsyscall/wrappers/spawn/posix_spawn.c" \
    -o "$OUT/libsystem_kernel/lk416_posix_spawn.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    -I "$XNU/libsyscall/custom" -I "$XNU/libsyscall/mach" \
    -idirafter "$XNU/osfmk" -idirafter "$MACSDK/usr/include" \
    "$SYSGEN/___posix_spawn.s" -o "$OUT/libsystem_kernel/lk416___posix_spawn.o"
#  - mmap/munmap: generated ___mmap/___munmap plus libsyscall's
#    wrappers/unix03/mmap.c and munmap.c (the syscall-logger hooks go to
#    this dylib's own __syscall_logger). Before this, only
#    libsystem_pthread's compat objects exported them.
#  - host_priv and mach_voucher MIG user stubs (host_reboot,
#    host_get_special_port, mach_voucher_*, host_create_mach_voucher, ...):
#    Mach MIG clients that live here on Apple. They are extracted from
#    libpthread_qos.a, which build_libpthread_qos.sh generates from
#    xnu-7195's host_priv.defs and mach_voucher.defs; libsystem_pthread used
#    to export them.
#  - getrusage (#117, plain generated stub) and ___settimeofday (#122,
#    NO_SYSCALL_STUB, raw trap only). Libc's gen/clock_gettime.c, now in
#    libsystem_c, calls getrusage() and settimeofday(). No dylib exported
#    either before.
#    DEVIATION, temporary: Apple's public settimeofday() is Libc's
#    third_party/Libc/sys/settimeofday.c:46-59. It wraps ___settimeofday, then posts
#    kNotifyClockSet (:49) and ASL-logs the change (:51-56). That needs notify_post (in
#    libsystem_notify, above libsystem_c), asprintf (exported by no dylib
#    here) and _simple_asl_log (exported by libsystem_malloc, a wrong
#    owner). DAR-410 tracks porting it. Until then, _settimeofday is an
#    alias of the raw trap, so the clock is set but no notification is sent.
#  - ___gettid/___settid (#286/#285): the generated stubs carry Apple's
#    syscall.map aliases pthread_getugid_np/pthread_setugid_np
#    (libsyscall/Platforms/syscall.map:12-13). libsystem_pthread's
#    pthread_thread_ops_compat.o used to export a geteuid()-based stand-in
#    (CoreFoundation binds it).
for s in ___mmap ___munmap _getrusage ___settimeofday ___gettid ___settid; do
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
        -I "$XNU/libsyscall/custom" -I "$XNU/libsyscall/mach" \
        -idirafter "$XNU/osfmk" -idirafter "$MACSDK/usr/include" \
        "$SYSGEN/${s}.s" -o "$OUT/libsystem_kernel/lk416${s}.o"
done
for f in mmap munmap; do
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
        -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
        -I "$XNU/libsyscall/mach" -idirafter "$XNU/osfmk" -I "$LIBC/include" -I "$LIBC/darwin" -I "$OUT/compat_include" \
        -I "$OUT/malloc_mig" -I "$XNU/libsyscall/wrappers" \
        "$XNU/libsyscall/wrappers/unix03/$f.c" -o "$OUT/libsystem_kernel/lk416_$f.o"
done
[ -f "$OUT/libpthread_qos.a" ] || { echo "error: $OUT/libpthread_qos.a not found -- run build_libpthread_qos.sh first" >&2; exit 1; }
(cd "$OUT/libsystem_kernel" && ar x ../libpthread_qos.a host_privUser.o mach_voucherUser.o && \
    mv host_privUser.o lk416_host_privUser.o && mv mach_voucherUser.o lk416_mach_voucherUser.o)
#  - host_create_mach_voucher, mach_voucher_deallocate and
#    mach_voucher_extract_attr_recipe: Apple's libsyscall/mach/host.c:86,
#    mach_msg.c:767 and mach_port.c:731. This port links its own hand-written
#    Mach wrappers rather than those whole files, so each is narrowed with
#    ld -r to the one function (the same precedent as the
#    mach_port_set_attributes narrowing above). libsystem_pthread's
#    libpthread_qos_compat.o used to export hand-written copies.
LK_MACH_FLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I $OUT/compat_include -I $XNU/libsyscall -I $XNU/libsyscall/mach -I $XNU/libsyscall/os -I $OUT/libsystem_kernel/pt416_internal_hdr \
    -I $OUT/malloc_mig -I $XNU/osfmk -idirafter $XNU/bsd"
LD_R="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
for pair in host:_host_create_mach_voucher mach_msg:_mach_voucher_deallocate mach_port:_mach_voucher_extract_attr_recipe; do
    f=${pair%%:*}; sym=${pair#*:}
    "$CLANG" $LK_MACH_FLAGS "$XNU/libsyscall/mach/$f.c" -o "$OUT/libsystem_kernel/lkall_$f.o"
    "$LD_R" -r -arch arm64 -exported_symbol "$sym" "$OUT/libsystem_kernel/lkall_$f.o" \
        -o "$OUT/libsystem_kernel/lk416_narrow$sym.o"
done
LIBKERNEL_OBJS=$(ls "$OUT"/libsystem_kernel/lk416*.o)

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
# reboot(2)/reboot_np() (DAR-334): Apple's own libsyscall wrapper
# (wrappers/reboot.c:28-40) over the raw __reboot trap. syscalls.master:108
# marks #55 NO_SYSCALL_STUB, so the generator emits only ___reboot (built
# in the stub loop above) and the public reboot() comes from this file.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I "$XNU/libsyscall/mach" -idirafter "$XNU/osfmk" -idirafter "$XNU/bsd" -I "$LIBC/include" -I "$LIBC/darwin" \
    -I "$XNU/libsyscall/wrappers" \
    "$XNU/libsyscall/wrappers/reboot.c" -o "$OUT/libsystem_kernel/reboot_wrap.o"
# setpriority(2) (DAR-334, shutdown(8)): Apple's wrappers/setpriority.c over
# the raw __setpriority (#96, NO_SYSCALL_STUB, syscalls.master:149). Its
# PRIO_DARWIN_THREAD branch calls libsystem_pthread's _pthread_clear_qos_tsd,
# which resolves at load like this dylib's other pthread references.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I "$XNU/libsyscall/mach" -idirafter "$XNU/osfmk" -idirafter "$XNU/bsd" -I "$LIBC/include" -I "$LIBC/darwin" \
    -I "$XNU/libsyscall/wrappers" \
    "$XNU/libsyscall/wrappers/setpriority.c" -o "$OUT/libsystem_kernel/setpriority_wrap.o"
# fchmod/getrlimit/setrlimit (DAR-387): Apple's wrappers/unix03/ over the
# raw traps from the stub loop, compiled like chmod.c above. fchmod.c
# retries without S_ISUID/S_ISGID on EPERM; getrlimit.c/setrlimit.c OR in
# _RLIMIT_POSIX_FLAG (getrlimit.c:36-43).
for w in fchmod getrlimit setrlimit; do
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
        -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
        -I "$XNU/libsyscall/mach" -idirafter "$XNU/osfmk" -I "$LIBC/include" -I "$LIBC/darwin" -I "$OUT/compat_include" \
        -I "$OUT/malloc_mig" -I "$XNU/libsyscall/wrappers" \
        "$XNU/libsyscall/wrappers/unix03/$w.c" -o "$OUT/libsystem_kernel/${w}_wrap.o"
done
# mach_boottime_usec/mach_continuous_time/mach_continuous_approximate_time/
# mach_approximate_time (DAR-387): Apple's own libsyscall wrappers, which is
# where macOS exports them. Libc/gen/clock_gettime.c needs them; until now
# build_sshd.sh and build_chrony.sh each compiled private copies of both.
# (clock_gettime itself is not in libsystem_c yet: its clock_settime()
# needs Libc/sys/settimeofday.c, which calls notify_post(), and
# libsystem_notify.dylib already links libsystem_c.dylib. DAR-410.) They read the commpage through
# <machine/cpu_capabilities.h>, whose content in this checkout is
# osfmk/arm/cpu_capabilities.h, and all of it sits behind `#ifdef PRIVATE`
# (its line 28), so -DPRIVATE and a forwarding header are needed, as in
# build_sshd.sh.
mkdir -p "$OUT/libsystem_kernel/compat_include/machine"
printf '#include <arm/cpu_capabilities.h>\n' > "$OUT/libsystem_kernel/compat_include/machine/cpu_capabilities.h"
# mach_continuous_time.c and mach_approximate_time.c call
# mach_absolute_time(), whose Apple home is this dylib too
# (libsyscall/wrappers/mach_absolute_time.s). Here only
# libsystem_platform.dylib exported it (build_libsystem_platform.sh), which
# this dylib cannot link (libsystem_kernel is the acyclic base, see the
# geteuid comment above), so the same Apple .s is assembled here as well.
# The libsystem_platform copy is a non-owner duplicate, left in place.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    -I "$XNU/libsyscall/mach" -idirafter "$XNU/osfmk" -I "$OUT/libsystem_kernel/compat_include" -DPRIVATE=1 \
    "$XNU/libsyscall/wrappers/mach_absolute_time.s" -o "$OUT/libsystem_kernel/mach_absolute_time.o"
# ___thread_selfusage: the raw trap behind clock_gettime(CLOCK_THREAD_CPUTIME_ID)
# (Libc/gen/clock_gettime.c:124), Apple's libsyscall/custom/__thread_selfusage.s;
# exported here, its Apple owner, for when clock_gettime moves (DAR-410).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    -I "$XNU/libsyscall/custom" -I "$XNU/libsyscall/mach" -idirafter "$XNU/osfmk" -idirafter "$MACSDK/usr/include" \
    "$XNU/libsyscall/custom/__thread_selfusage.s" -o "$OUT/libsystem_kernel/thread_selfusage_trap.o"
for w in mach_boottime mach_continuous_time mach_approximate_time; do
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
        -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
        -I "$XNU/libsyscall/mach" -idirafter "$XNU/osfmk" -I "$LIBC/include" -I "$LIBC/darwin" \
        -I "$OUT/libsystem_kernel/compat_include" \
        "$XNU/libsyscall/wrappers/$w.c" -o "$OUT/libsystem_kernel/${w}.o"
done
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
    -Wl,-U,_abort -Wl,-U,_pthread_tsd_shim_bootstrap \
    `# __semwait_signal: nanosleep_compat.o's own real dependency, see` \
    `# that compile step's own comment just above -- real canonical copy` \
    `# is libsystem_malloc.dylib's sys_semwait_signal.o, always loaded` \
    `# alongside this dylib (same class of deferral as the 3 just above).` \
    -Wl,-U,___semwait_signal \
    `# mmap/munmap $UNIX2003 spellings, which this port's xnu-cdefs builds` \
    `# bind (e.g. crontab); libsystem_pthread used to carry these aliases.` \
    -Wl,-alias,_mmap,'_mmap$UNIX2003' -Wl,-alias,_munmap,'_munmap$UNIX2003' \
    -Wl,-alias,___settimeofday,_settimeofday \
    `# (_memcpy and __pthread_clear_qos_tsd are no longer deferred:` \
    `# DAR-416 1b builds Apple's _libc_funcptr.c, whose hidden thunks` \
    `# resolve both inside this dylib.)` \
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
    `# DAR-387: getrlimit/setrlimit $UNIX2003 spellings. libsystem_c's` \
    `# compiles (-D__DARWIN_UNIX03=1) reference _getrlimit$UNIX2003 from` \
    `# Libc/gen/FreeBSD/sysconf.c; bases are this link's own` \
    `# wrappers/unix03/{getrlimit,setrlimit}.c objects.` \
    -Wl,-alias,_getrlimit,'_getrlimit$UNIX2003' -Wl,-alias,_setrlimit,'_setrlimit$UNIX2003' \
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
    `# _ptrace added 2026-09-20 (DAR-342): __ptrace.s (ptrace_trap.o, just` \
    `# added above) exports only the private-indirection name ___ptrace,` \
    `# the same real Apple naming convention as ___pipe/__lchown. Real` \
    `# Apple's own libsystem_kernel.dylib exports BOTH names at one` \
    `# identical address (nm -gU: 0000000000008cc8 T ___ptrace / T` \
    `# _ptrace), so this alias is Apple parity, not a local convenience.` \
    `# Base is genuinely local to this link (ptrace_trap.o), same` \
    `# nonlocal-base requirement as every other alias here.` \
    -Wl,-alias,___ptrace,_ptrace \
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
    "$OUT/libsystem_kernel/sys___accept.o" "$OUT/libsystem_kernel/sys___listen.o" \
    "$OUT/libsystem_kernel/sys___socketpair.o" "$OUT/libsystem_kernel/sys___getattrlist.o" \
    "$OUT/libsystem_kernel/sys___getlogin.o" "$OUT/libsystem_kernel/sys___setlogin.o" \
    "$OUT/libsystem_kernel/sys_fpathconf.o" "$OUT/libsystem_kernel/nanosleep_compat.o" \
    "$OUT/libsystem_kernel/sys_kevent.o" "$OUT/libsystem_kernel/sys_kqueue.o" \
    "$OUT/libsystem_kernel/pipe_trap.o" "$OUT/libsystem_kernel/pipe_forwarder.o" \
    "$OUT/libsystem_kernel/ptrace_trap.o" \
    "$OUT/libsystem_kernel/chmod_wrap.o" "$OUT/libsystem_kernel/xpc_syscall_wrappers_compat.o" \
    "$OUT/libsystem_kernel/select_wrap.o" "$OUT/libsystem_kernel/sendto_wrap.o" \
    "$OUT/libsystem_kernel/recvfrom_wrap.o" \
    "$OUT/libsystem_kernel/mach_semaphore_traps.o" "$OUT/libsystem_kernel/semaphore_wrap.o" \
    "$OUT/libsystem_kernel/mach_error_string.o" "$OUT/libsystem_kernel/mach_error_string_minimal_compat.o" \
    "$OUT/libsystem_kernel/mach_port_gap_closures2.o" "$OUT/libsystem_kernel/mach_port_gap_closures2_traps.o" \
    "$OUT/libsystem_kernel/cf_mach_port_syscalls.o" "$OUT/libsystem_kernel/mach_port_traps.o" \
    "$OUT/libsystem_kernel/thread_get_special_reply_port_trap.o" \
    "$OUT/libsystem_kernel/mach_portUser.o" "$OUT/libsystem_kernel/mach_port_set_attributes.o" \
    "$OUT/libsystem_kernel/mig_reply_setup.o" \
    "$OUT/libsystem_kernel/sys___sigaction.o" \
    "$OUT/libsystem_kernel/proc_pidinfo_compat.o" \
    $LIBKERNEL_OBJS \
    "$OUT/libsystem_kernel/sys_setegid.o" "$OUT/libsystem_kernel/sys_seteuid.o" \
    "$OUT/libsystem_kernel/sys_getgroups.o" "$OUT/libsystem_kernel/sys_setitimer.o" \
    "$OUT/libsystem_kernel/sys_flock.o" \
    `# sync(2): generated stub, #36 (bsd/kern/syscalls.master:89, no` \
    `# NO_SYSCALL_STUB marker). Real Apple exports it from` \
    `# libsystem_kernel; sync(8) (system_cmds/sync.tproj) needs it.` \
    "$OUT/libsystem_kernel/sys_sync.o" \
    `# reboot(2) (DAR-334): raw ___reboot (#55, NO_SYSCALL_STUB,` \
    `# syscalls.master:108) plus Apple's wrappers/reboot.c. Real Apple` \
    `# exports both from libsystem_kernel. (___getlogin is linked with` \
    `# DAR-346's socket/login traps above.)` \
    "$OUT/libsystem_kernel/sys___reboot.o" "$OUT/libsystem_kernel/reboot_wrap.o" \
    `# DAR-387: see the stub loop's and the wrappers' comments.` \
    "$OUT/libsystem_kernel/sys_chdir.o" "$OUT/libsystem_kernel/sys_readlink.o" \
    "$OUT/libsystem_kernel/sys_fchown.o" "$OUT/libsystem_kernel/sys_getpgrp.o" \
    "$OUT/libsystem_kernel/sys_getppid.o" "$OUT/libsystem_kernel/sys_setgid.o" \
    "$OUT/libsystem_kernel/sys_setuid.o" "$OUT/libsystem_kernel/sys_setpgid.o" \
    "$OUT/libsystem_kernel/sys___fchmod.o" "$OUT/libsystem_kernel/sys___getrlimit.o" \
    "$OUT/libsystem_kernel/sys___setrlimit.o" "$OUT/libsystem_kernel/sys___msync.o" \
    "$OUT/libsystem_kernel/fchmod_wrap.o" "$OUT/libsystem_kernel/getrlimit_wrap.o" \
    "$OUT/libsystem_kernel/setrlimit_wrap.o" \
    "$OUT/libsystem_kernel/mach_boottime.o" "$OUT/libsystem_kernel/mach_continuous_time.o" \
    "$OUT/libsystem_kernel/mach_approximate_time.o" \
    "$OUT/libsystem_kernel/mach_absolute_time.o" \
    "$OUT/libsystem_kernel/sys_csops.o" "$OUT/libsystem_kernel/sys_csops_audittoken.o" \
    "$OUT/libsystem_kernel/sys_sigprocmask.o" \
    $PT416_OBJS \
    $D423_OBJS \
    "$OUT/libsystem_kernel/thread_selfusage_trap.o" \
    `# setsid(2) (#147, plain stub) and setpriority(2) (raw #96 + Apple's` \
    `# wrappers/setpriority.c), for shutdown(8). Real Apple exports both` \
    `# from libsystem_kernel.` \
    "$OUT/libsystem_kernel/sys_setsid.o" \
    "$OUT/libsystem_kernel/sys___setpriority.o" "$OUT/libsystem_kernel/setpriority_wrap.o" \
    `# dup2(2) (#90, plain stub), for shutdown(8)'s popen(). Real Apple` \
    `# exports it from libsystem_kernel.` \
    "$OUT/libsystem_kernel/sys_dup2.o" \
    `# X11 client libraries (DAR-383): see the stub loop's comment.` \
    "$OUT/libsystem_kernel/sys_link.o" "$OUT/libsystem_kernel/sys_pathconf.o" \
    "$OUT/libsystem_kernel/sys_readv.o" "$OUT/libsystem_kernel/sys_poll.o" \
    "$OUT/libsystem_kernel/sys_getsockopt.o" "$OUT/libsystem_kernel/sys_shutdown.o" \
    "$OUT/libsystem_kernel/sys_umask.o" \
    `# (___listen/___accept are linked with DAR-346's traps above.)` \
    `# Font/2D stack (DAR-384): see the stub loop's comment.` \
    "$OUT/libsystem_kernel/sys_utimes.o" "$OUT/libsystem_kernel/sys_shmat.o" \
    "$OUT/libsystem_kernel/sys_shmdt.o" "$OUT/libsystem_kernel/sys_shmget.o" \
    "$OUT/libsystem_kernel/sys___shmctl.o" \
    `# SysV sem/msg + varargs shims (DAR-407): see the stub loop's comment.` \
    "$OUT/libsystem_kernel/sys_semget.o" "$OUT/libsystem_kernel/sys_semop.o" \
    "$OUT/libsystem_kernel/sys___semctl.o" "$OUT/libsystem_kernel/sys_msgget.o" \
    "$OUT/libsystem_kernel/sys_msgsnd.o" "$OUT/libsystem_kernel/sys_msgrcv.o" \
    "$OUT/libsystem_kernel/sys___msgctl.o" \
    "$OUT/libsystem_kernel/sys___msgsnd_nocancel.o" "$OUT/libsystem_kernel/sys___msgrcv_nocancel.o" \
    "$OUT/libsystem_kernel/sys___semsys.o" "$OUT/libsystem_kernel/sys___msgsys.o" \
    "$OUT/libsystem_kernel/sys___shmsys.o" \
    "$OUT/libsystem_kernel/sys___sem_open.o" "$OUT/libsystem_kernel/sys___shm_open.o" \
    "$OUT/libsystem_kernel/varargs_wrappers.o" \
    "$OUT/libsystem_kernel/__sigaltstack_trap.o" "$OUT/libsystem_kernel/__sigreturn_trap.o" \
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
