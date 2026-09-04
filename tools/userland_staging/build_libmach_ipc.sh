#!/bin/bash
# Builds libc_build/libmach_ipc.a: this project's FIRST real userland
# exerciser of Mach IPC (mach_msg()-trap-based MIG RPC) -- docs/
# roadmap.md Phase 4.6 item 5 / docs/backlog.md item 7. Real, unmodified
# open-source kernel-side implementation has always been present
# (third_party/xnu-7195's osfmk/kern/ipc_*/clock.c), completely untouched
# by this work; this script builds the missing userland half.
#
# Concrete real chain this closes, replacing simple_cmds_nanosleep_
# compat.c's/simple_cmds_nanosleep_pub_compat.c's prior CPU-burning
# mach_absolute_time() spin-wait with a genuinely blocking sleep, real
# Apple third_party/Libc/gen/nanosleep.c's own `__DARWIN_UNIX03` (this
# project's build always sets that) algorithm:
#   mach_host_self() -> host_get_clock_service() -> semaphore_create()
#   (once, at first-use) then, per call: clock_get_time() +
#   __semwait_signal() (real BSD syscall 334, already linked via
#   libmalloc.a's sys_semwait_signal.o -- see build_libmalloc.sh).
#
# Real `mig` (present on this host at /usr/bin/mig, same tool + same
# selection precedent build_libmalloc.sh's own mach_vm_internal.h step
# already established -- see that script's header comment for why
# `$(which mig)` specifically, not `xcrun`'s copy) run fresh each build
# against this project's real, unmodified xnu-7195 .defs sources
# (osfmk/mach/clock.defs, mach_host.defs, task.defs) -- NOT committed as
# static generated files, same "real generated output, regenerated every
# build, not checked in as source" precedent as build_libmalloc.sh's own
# mach_vm_internal.h/mach_vmUser.c step.
#
# Unlike build_libmalloc.sh's mach_vm.defs step (which needed explicit
# curation to avoid a duplicate-struct conflict between mach_vm.defs and
# vm_map.defs), clock.defs/mach_host.defs/task.defs each mig-generate AND
# compile completely cleanly as their FULL real files under this
# project's exact build flags (confirmed by directly compiling each full
# generated *User.c with zero errors before deciding this, not assumed) --
# so the full, real, unmodified client stub for every routine in each of
# the 3 subsystems is compiled, even though only clock_get_time/
# host_get_clock_service/semaphore_create are ever called. The other
# ~140 compiled-but-unreferenced routines are harmless dead weight, same
# tolerance this project's build already has for mach_vm.c's own unused
# wrapper functions.
#
# mach_host.defs/task.defs (unlike clock.defs) need an explicit `-I
# $XNU/osfmk` for `mig` to resolve their real `#include
# <mach_debug/mach_debug_types.defs>`/other cross-directory .defs
# includes (`io_master_t`/`suid_cred_t` "type not defined" errors
# otherwise -- confirmed by testing both with and without -I, not
# guessed); clock.defs resolves fine without it (matching build_
# libmalloc.sh's own mach_vm.defs precedent, which also uses no -I).
#
# Real generated stubs need a small, real support surface this project
# hadn't needed before this pass -- each piece here is either genuine
# unmodified Apple source (curated down to just what's needed, same
# philosophy build_libmalloc.sh's own mach_vm_internal.h/vm_map_internal.h
# split already established) or an honest, clearly-documented dead-code
# stand-in (same "real signature, KERN_FAILURE body" pattern as
# libmalloc_vm_mig_stubs.c):
#   - mach_msg_real.c: real mach_msg()/mach_msg_destroy() bodies, curated
#     from mach_msg.c (the full file doesn't compile under this project's
#     build flags -- its mach_msg_priority_*/mach_msg_qos_t section hits
#     real, genuine errors unrelated to anything this project needs; see
#     that file's own header for the exact confirmed detail).
#   - mach_ipc_dead_stubs.c: mach_port_insert_right/mach_port_extract_right/
#     vm_deallocate/mach_port_unguard -- real signatures, referenced only
#     by mach_msg_destroy()'s reply-descriptor-shape branches that are
#     provably unreachable for this project's 3 real routines (see that
#     file's own header for the exact disposition-value proof).
#   - mach_host_self_real.c: real mach_host_self() body, curated from
#     mach_legacy.c (mach_task_self()/mach_thread_self() dropped -- this
#     project already has a real, working mach_task_self() via the
#     existing mach_task_self_ global + macro, and nothing needs
#     mach_thread_self()).
#   - NDR_record (the real global every mig-generated stub needs) needs
#     NO new file here -- third_party/xnu-7195/libsyscall/mach/mach_init.c
#     (already compiled into libc_build/libmalloc.a by build_libmalloc.sh)
#     already transitively defines a real one (confirmed via `nm`, not
#     guessed -- a real duplicate-symbol link error is what caught this
#     during this script's own development, not assumption).
#   - mig_strncpy.c: real, unmodified, used by taskUser.c's string-typed
#     routines (not the 3 this project calls, but still referenced at
#     link time from the same translation unit).
#   - mach_ipc_traps.s: 2 new real arm64 trap stubs (mach_msg_trap -31,
#     host_self_trap -29), same hand-copied-from-syscall_sw.h pattern as
#     libplatform_mach_traps.s/libmalloc_vm_traps.s. task_self_trap(-28)/
#     mach_reply_port(-26) are NOT redefined -- already in
#     libmalloc_vm_traps.o, already part of libc_build/libmalloc.a.
#   - mach_msg_trap_decl.h: compat prototype header for mach_msg_trap/
#     host_self_trap -- a plain `#include <mach/mach_traps.h>` resolves
#     against the Xcode-12 iPhoneOS SDK's OWN copy of that header under
#     this project's `-idirafter $XNU/osfmk` convention (angle-bracket
#     lookup checks the sysroot first), and that copy doesn't declare
#     these two at all under `-DPRIVATE=1` (confirmed via `clang -E`:
#     only `task_self_trap` survives from that whole block) -- same
#     class of fix as the project's existing mach_reply_port_decl.h.
#   - mach_ipc_clock_init.c: the shared one-time host_get_clock_service +
#     semaphore_create bootstrap (real Apple nanosleep.c's own
#     `_init_clock_port()`, adapted to be idempotent since this project
#     has no dyld/libSystem_initializer to call it exactly once
#     automatically -- same "must be called explicitly" pattern as
#     mach_init()/pthread_tsd_shim_bootstrap()).
#
# DAR-143 (2026-09-04) extended this script's original Phase 4.6 item 5
# scope (clock/semaphore/nanosleep) to also cover the task/thread-info
# MIG subsystem real `ps` needs for its CPU%/RSS/VSZ/thread columns
# (replacing tools/userland_staging/ps_task_info_stub.c's honest-zeros
# degradation path with real adv_cmds/ps/tasks.c). This is the natural
# extension point rather than a new script: task.defs was ALREADY being
# mig-regenerated + compiled + archived into libmach_ipc.a above for
# task_info()/task_threads() (the other ~140 task.defs routines were
# already dead weight per this file's own header above) -- it just had
# no caller yet. The only 2 real, new pieces DAR-143 needed:
#   - thread_act.defs mig regen (thread_info(), same "compile the full
#     real file, most routines unused" tolerance as clock/mach_host/task)
#   - task_for_pid(): a raw Mach TRAP (-45, kernel_trap(task_for_pid,-45,3)
#     in osfmk/mach/syscall_sw.h), not a MIG routine -- confirmed real,
#     unmodified, already wired into this exact kernel fork's trap table
#     (osfmk/kern/syscall_sw.c line 150: `MACH_TRAP(task_for_pid, 3, 3,
#     munge_www)`), so this is a pure userland wiring gap, not a kernel
#     change. Real privilege check is xnu's stock task_for_pid_posix_check()
#     (bsd/vm/vm_unix.c): passes unconditionally for root, which is this
#     project's only real user today, so no restriction actually bites
#     here -- confirmed by reading that function, not assumed.
# task_info()/task_threads()/thread_info() all return real, live kernel
# data for the calling process's own real process table entry -- no
# fabrication, no kernel change, same "real, open Apple/XNU source not
# yet built" character as every other routine in this file.
set -e
cd "$(dirname "$0")"

XNU="../../third_party/xnu-7195"
LIBC="../../third_party/Libc"
OUT="libc_build"
mkdir -p "$OUT/mach_ipc_mig"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -f "$OUT/libmalloc.a" ]; then
    echo "error: $OUT/libmalloc.a not found -- run build_libmalloc.sh first (this script reuses its mach_init.o/mig_reply_port.o/libmalloc_vm_traps.o/pthread_tsd_shim.o/sys_semwait_signal.o)" >&2
    exit 1
fi
# Same selection precedent as build_libmalloc.sh: deliberately
# `$(which mig)` (the currently-xcode-select'ed toolchain's copy), not
# `xcrun`'s -- see that script's header comment for the confirmed real
# reason (xcrun's copy fails to resolve its own <mach/std_types.defs>).
MIG="$(which mig)"
if [ -z "$MIG" ] || [ ! -x "$MIG" ]; then
    echo "error: mig tool not found (needed to regenerate mach/clock.h, mach/mach_host.h, mach/task.h)" >&2
    exit 1
fi

# Absolute paths for the .defs inputs -- same real mig parser quirk
# build_libmalloc.sh's own header comment documents ("no SubSystem
# declaration" on a "../../"-prefixed relative path).
XNU_ABS="$(cd "$XNU" && pwd)"
CLOCK_DEFS="$XNU_ABS/osfmk/mach/clock.defs"
MACH_HOST_DEFS="$XNU_ABS/osfmk/mach/mach_host.defs"
TASK_DEFS="$XNU_ABS/osfmk/mach/task.defs"
THREAD_ACT_DEFS="$XNU_ABS/osfmk/mach/thread_act.defs"

MIGOUT="$OUT/mach_ipc_mig"
"$MIG" -arch arm64 \
    -header "$MIGOUT/clock.h" \
    -user "$MIGOUT/clockUser.c" -server "$MIGOUT/clockServer.c" -sheader "$MIGOUT/clockServer.h" \
    "$CLOCK_DEFS" >/dev/null
"$MIG" -arch arm64 -I"$XNU_ABS/osfmk" \
    -header "$MIGOUT/mach_host.h" \
    -user "$MIGOUT/mach_hostUser.c" -server "$MIGOUT/mach_hostServer.c" -sheader "$MIGOUT/mach_hostServer.h" \
    "$MACH_HOST_DEFS" >/dev/null
"$MIG" -arch arm64 -I"$XNU_ABS/osfmk" \
    -header "$MIGOUT/task.h" \
    -user "$MIGOUT/taskUser.c" -server "$MIGOUT/taskServer.c" -sheader "$MIGOUT/taskServer.h" \
    "$TASK_DEFS" >/dev/null
# thread_act.defs (DAR-143): same -I$XNU/osfmk requirement as mach_host/
# task.defs (cross-directory #include resolution), confirmed by testing.
"$MIG" -arch arm64 -I"$XNU_ABS/osfmk" \
    -header "$MIGOUT/thread_act.h" \
    -user "$MIGOUT/thread_actUser.c" -server "$MIGOUT/thread_actServer.c" -sheader "$MIGOUT/thread_actServer.h" \
    "$THREAD_ACT_DEFS" >/dev/null
echo "mig: regenerated clock.h/mach_host.h/task.h/thread_act.h + *User.c from real xnu-7195 .defs"

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I $XNU/libsyscall/mach -idirafter $XNU/osfmk -I $LIBC/include -I $LIBC/darwin -I libc_build/compat_include"

"$CLANG" $CFLAGS -I "$MIGOUT" "$MIGOUT/clockUser.c" -o "$OUT/mach_ipc_mig/clockUser.o"
"$CLANG" $CFLAGS -I "$MIGOUT" "$MIGOUT/mach_hostUser.c" -o "$OUT/mach_ipc_mig/mach_hostUser.o"
"$CLANG" $CFLAGS -I "$MIGOUT" "$MIGOUT/taskUser.c" -o "$OUT/mach_ipc_mig/taskUser.o"
"$CLANG" $CFLAGS -I "$MIGOUT" "$MIGOUT/thread_actUser.c" -o "$OUT/mach_ipc_mig/thread_actUser.o"
"$CLANG" $CFLAGS -include mach_msg_trap_decl.h mach_msg_real.c -o "$OUT/mach_msg_real.o"
"$CLANG" $CFLAGS mach_ipc_dead_stubs.c -o "$OUT/mach_ipc_dead_stubs.o"
"$CLANG" $CFLAGS -include mach_msg_trap_decl.h mach_host_self_real.c -o "$OUT/mach_host_self_real.o"
"$CLANG" $CFLAGS "$XNU/libsyscall/mach/mig_strncpy.c" -o "$OUT/mig_strncpy.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c mach_ipc_traps.s -o "$OUT/mach_ipc_traps.o"
"$CLANG" $CFLAGS mach_ipc_clock_init.c -o "$OUT/mach_ipc_clock_init.o"

rm -f "$OUT/libmach_ipc.a"
ar rcs "$OUT/libmach_ipc.a" \
    "$OUT/mach_ipc_mig/clockUser.o" "$OUT/mach_ipc_mig/mach_hostUser.o" "$OUT/mach_ipc_mig/taskUser.o" \
    "$OUT/mach_ipc_mig/thread_actUser.o" \
    "$OUT/mach_msg_real.o" "$OUT/mach_ipc_dead_stubs.o" "$OUT/mach_host_self_real.o" \
    "$OUT/mig_strncpy.o" "$OUT/mach_ipc_traps.o" "$OUT/mach_ipc_clock_init.o"
echo "wrote $OUT/libmach_ipc.a"

# Real smoketest: exercises the exact same real chain
# simple_cmds_nanosleep_pub_compat.c/simple_cmds_nanosleep_compat.c call
# (mach_host_self -> host_get_clock_service -> semaphore_create, then
# clock_get_time + a short real nanosleep()), printing mach_absolute_time()
# deltas before/after so a QEMU run can confirm it actually blocked for
# roughly the requested duration rather than either spinning (the old
# bug) or returning instantly (a silently-broken semaphore wait).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    mach_ipc_smoketest.c -o "$OUT/mach_ipc_smoketest.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I $XNU/libsyscall/mach -idirafter $XNU/osfmk -I $LIBC/include -I $LIBC/darwin -I libc_build/compat_include \
    simple_cmds_nanosleep_pub_compat.c -o "$OUT/mach_ipc_mig/nanosleep_pub_for_smoketest.o"
# Same extra archives + $UNIX2003-suffixed link aliases as
# build_libmalloc.sh's own libmalloc_smoketest.macho (proven working --
# confirmed via a fresh `nm -u` showing zero undefined symbols just
# before this script was written): libmalloc.a's own malloc_printf.o
# error-reporting path (referenced transitively via abort()) needs
# getenv/getprogname/isatty (libc_locale_for_stdio.a/libc_stdio.a) and
# the $UNIX2003 aliases real Apple's own libSystem umbrella link step
# would normally produce (see build_libmalloc.sh's own header comment
# for the full real reason this project needs `-alias` instead). Plus
# one NEW real gap this project's Mach-IPC work introduces:
# `voucher_mach_msg_set` -- every real `mig`-generated stub this project
# now compiles references it (weak_import-declared, gated behind
# `__has_include(<mach/mig_voucher_support.h>)`, which resolves TRUE
# against the Xcode-12 SDK's own copy of that header even though this
# project's real xnu-7195 checkout has no voucher subsystem wired up at
# all) -- confirmed by testing that ld64's static linker does NOT leave
# a weak_import symbol unresolved the way dyld would; an honest
# `return FALSE` stand-in is in mach_ipc_dead_stubs.c (this project has
# no real Mach voucher port for any of its 3 actual MIG routines to
# attach one to -- same "not yet built" scope decision as
# INET/INET6/networking, not a bug).
#
# "$OUT/pthread_tsd_shim_libc_bootstrap.o"/"$OUT/libpthread.a" below
# (DAR-143, 2026-09-04): pre-existing gap found while re-running this
# script for the first time since DAR-153/164's real crt.c changes
# (commits 271450d/5697d90) made Csu/crt.c's process entry point call
# `pthread_main_thread_bootstrap()` (weak_import) unconditionally before
# __mod_init_func constructors run. Confirmed real, not caused by this
# ticket's thread_act.defs/task_for_pid additions: ld64's static linker
# does NOT leave an unresolved weak_import symbol alone the way dyld
# would, so any `-static` Csu/crt.c consumer needs a real definition at
# link time. Every other real static binary in this project already
# carries libpthread.a for exactly this (see build_ps.sh/build_bash.sh/
# build_getty.sh) -- this smoketest's own link line just predates that
# requirement and had never been re-run since. Real fix, not a
# workaround: same archives every other binary already links.
# libc_gen.a + sys____mmap.o/sys____munmap.o (same DAR-143 finding):
# pthread_main_thread_bootstrap.o's pthread_create() also needs real
# `___isthreaded` (libc_gen.a's libc_gen_shims.o, a common/bss symbol --
# confirmed via `nm`, not guessed) and real `mmap`/`munmap` for its
# thread-stack allocation (same real gap build_getty.sh already fixed
# for DAR-165 via sys____mmap.o/sys____munmap.o + the $UNIX2003 aliases
# below).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -o mach_ipc_smoketest.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT/mach_ipc_smoketest.o" "$OUT/mach_ipc_mig/nanosleep_pub_for_smoketest.o" \
    "$OUT/simple_obj/sys____mmap.o" "$OUT/simple_obj/sys____munmap.o" \
    "$OUT/libmach_ipc.a" \
    "$OUT/pthread_tsd_shim_libc_bootstrap.o" \
    "$OUT/libpthread.a" \
    "$OUT/libclosure.a" "$OUT/libc_gen.a" "$OUT/libmalloc.a" "$OUT/cerror_stub.o" "$OUT/libplatform.a" \
    "$OUT/libc_locale_for_stdio.a" "$OUT/libc_stdio.a" \
    -Wl,-alias,'_mmap$UNIX2003',_mmap \
    -Wl,-alias,'_munmap$UNIX2003',_munmap \
    -Wl,-alias,_mprotect,'_mprotect$UNIX2003' \
    -Wl,-alias,___kill,'_kill$UNIX2003' \
    -Wl,-alias,___kill,_kill \
    -Wl,-alias,_write,'_write$UNIX2003' \
    -Wl,-alias,__mal_semwait_sleep,'_sleep$UNIX2003' \
    -Wl,-alias,___kdebug_trace,_kdebug_trace
echo "wrote mach_ipc_smoketest.macho"

otool -l mach_ipc_smoketest.macho | grep -q LC_LOAD_DYLIB && \
    { echo "error: mach_ipc_smoketest.macho unexpectedly links a dylib" >&2; exit 1; }
otool -l mach_ipc_smoketest.macho | grep -q LC_UNIXTHREAD || \
    { echo "error: mach_ipc_smoketest.macho missing LC_UNIXTHREAD (not a static Mach-O entry)" >&2; exit 1; }
if nm -u mach_ipc_smoketest.macho | grep -q .; then
    echo "error: mach_ipc_smoketest.macho has undefined symbols:" >&2
    nm -u mach_ipc_smoketest.macho >&2
    exit 1
fi
echo "mach_ipc_smoketest.macho: fully static, zero undefined symbols"
