#!/bin/bash
# Builds libc_build/libxpc_syscalls.a: the real syscall/Mach-trap/libc
# support surface build_libxpc.sh's own real trial link found missing
# (see that script's HONEST SCOPE STATEMENT -- 47 real, bounded,
# categorized undefined symbols). This script closes most of them with
# real Apple/FreeBSD source (curated where the full original file
# doesn't compile under this project's flags, same precedent every
# other tier in this directory already established -- see each new
# compat file's own header comment for its specific real provenance),
# not fabricated stand-ins.
#
# ============================================================
# HONEST SCOPE STATEMENT -- read this first
# ============================================================
# CLOSED this pass (real source, confirmed by real compile, not
# guessed): mach_error_string (compiles as-is), mig_deallocate.c
# (compiles as-is), CrashReporterClient.c (compiles as-is, gCRAnnotations),
# setenv.c (real FreeBSD source, gives unsetenv/setenv/putenv --
# needed __environ_lock/__environ_unlock, missing from this project's
# pinned Libc entirely, real honest no-op since no real second-thread
# caller exists yet, see environ_lock_compat.c; and __findenv_locked,
# curated from getenv.c since that file's own <os/lock_private.h> chain
# isn't vendored here, see findenv_locked_compat.c), libproc.c (real,
# unmodified -- gives proc_get_dirty/proc_set_dirty/proc_track_dirty/
# task_name_for_pid's siblings plus __process_policy/__thread_selfid/
# proc_rlimit_control, the last 3 satisfied by real already-generated
# .s stubs found sitting in libc_build/dyld_syscall_gen/ from this
# project's own dyld port -- reused, not regenerated), select() (real
# select-base.c body, curated to exclude pselect(), which this project
# has no real caller for yet -- see xpc_select_compat.c), connect()/
# recvmsg()/sendmsg() (real Apple legacy/*.c logic, curated without the
# _NONSTD_SOURCE/NO_SYSCALL_LEGACY preprocessor scaffolding those files
# use -- confirmed via real trial compile that scaffolding is
# incompatible with this project's LP64/arm64-only
# __DARWIN_ONLY_UNIX_CONFORMANCE target regardless, real Apple's own
# arm64/iOS libsyscall build excludes these files per-arch for the same
# reason -- see xpc_syscall_wrappers_compat.c), kill() (real,
# unmodified libsyscall/wrappers/kill.c), mach_msg_send() (real body,
# curated from mach_msg.c -- same already-established curation
# precedent as build_libmach_ipc.sh's own mach_msg_real.c),
# mach_port_allocate() (real fast-trap-only body, curated from
# mach_port.c -- the rare MIG-fallback path isn't ported, see
# mach_port_allocate_compat.c's own header), semaphore_signal()/
# semaphore_wait() (real bodies curated from semaphore.c, two new real
# arm64 trap stubs for semaphore_signal_trap/semaphore_wait_trap --
# mach_semaphore_traps.s/mach_semaphore_wrap_compat.c), socket()/
# fileport_makefd()/fileport_makeport() (plain create-syscalls.pl
# stubs, not NO_SYSCALL_STUB, no wrapper needed), plus the raw
# _nocancel trap stubs every wrapper above needs.
#
# STILL OPEN after this script (real, confirmed, not fabricated over --
# see build_libxpc.sh's own trial-link smoketest for the exact current
# list): the `_chk`-suffixed hardening routines (need a real
# _FORTIFY_SOURCE-shim source, not yet located); `os_log_create`/
# `os_log_type_enabled`/`_os_log_impl` (os_log's real engine --
# genuinely unbuilt, no logging daemon exists on this target);
# `mach_port_get_context`/`mach_port_set_context` (real MIG routines,
# not simple traps -- need a real mach_port.defs regen, bigger than a
# curation); `mach_msg_server_once`/`dispatch_data_create_map`/
# `kdebug_trace` (not yet investigated); `task_name_for_pid` (real,
# confirmed to exist kernel-side in bsd/vm/vm_unix.c, but has NO
# syscalls.master entry at all in this pinned xnu-7195 -- the real
# userspace call mechanism for it hasn't been identified yet, not a
# simple raw-trap gap); `uuid_unparse_upper` (the real source found,
# libkern/uuid/uuid.c, is genuinely kernel-only -- needs
# `read_random`/`nanotime`/`mount_t`, none of which exist in userland
# here; the real userland Libc equivalent hasn't been located yet); the
# two real `_OBJC_CLASS_$_OS_xpc_connection`/`_OBJC_CLASS_$_OS_xpc_object`
# symbols (`pd_xpc_object.c`'s `OS_OBJECT_OBJC_CLASS_DECL()` declares
# these unconditionally even though its own real retain/release logic
# never calls `objc_msgSend` against them -- likely closeable with a
# minimal placeholder data symbol, not yet attempted). The `$UNIX2003`-
# suffixed symbols (`_kill$UNIX2003`/`_mprotect$UNIX2003`/
# `_sleep$UNIX2003`/`_write$UNIX2003`) are NOT a real gap -- they're
# this project's own established `-Wl,-alias` pattern (see
# build_libnotify.sh's own link recipe), just need adding at whichever
# script does the FINAL launchd/libxpc executable link, not this
# static-archive-only script.
set -e
cd "$(dirname "$0")"

XNU="../../third_party/xnu-7195"
LIBC="../../third_party/Libc"
CR="../../third_party/PureDarwin/src/Libraries/CrashReporterClient"
OUT="libc_build"

if [ ! -d "$OUT/dyld_compat_sys_override" ]; then
    echo "error: $OUT/dyld_compat_sys_override not found -- run build_dyld_core.sh first (reused here)" >&2
    exit 1
fi
if [ ! -d "$OUT/dyld_syscall_gen" ]; then
    echo "error: $OUT/dyld_syscall_gen not found -- run build_dyld_link.sh first (reused here for __process_policy/__thread_selfid/proc_rlimit_control)" >&2
    exit 1
fi
# sched_yield/getenv/_NSGetEnviron used to be reused from
# build_simple_cmds.sh's own output (a real dependency edge that, once
# named for gen_build_order.py, formed a genuine ordering cycle:
# build_simple_cmds.sh's own per-command dynamic-link tier needs the
# real libsystem_c.dylib/libremovefile.dylib/libsystem_m.dylib tier,
# which needs this script's own libxpc_syscalls.a, which needed
# build_simple_cmds.sh -- see build_simple_cmds.sh's own comment on its
# libremovefile.dylib/libsystem_m.dylib checks, added 2026-09-14).
# Fixed by compiling these same few small, self-contained sources
# directly here instead -- same "recompile the source directly in the
# consumer" pattern already used by ~14 other scripts for
# libunwind_sched_yield.c/.s (see build_uname.sh/build_ps.sh/etc), so
# this script no longer needs build_simple_cmds.sh to have run at all.
# (sys__dup.o/sys__kevent.o/etc further below are used only by this
# script's own optional, non-fatal trial re-link -- never a real
# prerequisite for the actual libxpc_syscalls.a archive built above
# them -- so no compile/check for those is needed here.)

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi

# Start clean: the archive below globs xpc_syscall_obj/*.o, so an object
# this script no longer builds would otherwise stay in the archive forever
# (DAR-416: a stale sys____proc_info.o kept a second ___proc_info owner).
rm -rf "$OUT/xpc_syscall_obj"
rm -f "$OUT/libxpc_syscalls.a"
mkdir -p "$OUT/xpc_syscall_obj" "$OUT/sys_asm_compat/mach/arm64"
cat > "$OUT/sys_asm_compat/mach/arm64/asm.h" <<'EOF'
#include <arm64/asm.h>
EOF

# --- real create-syscalls.pl raw trap stubs -------------------------------
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
ARCHS=arm64 perl "$XNU/libsyscall/xcodescripts/create-syscalls.pl" \
    "$XNU/bsd/kern/syscalls.master" \
    "$XNU/libsyscall/custom" \
    "$XNU/libsyscall/Platforms" \
    iPhoneOS \
    "$TMP" >/dev/null
ASMFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -arch arm64 -I"$OUT/sys_asm_compat" -I"$XNU/libsyscall/custom" -I"$XNU/libsyscall/mach" -I"$XNU/osfmk")
# ___proc_info (#336) left this list for DAR-416 step 1a: libsystem_kernel
# exports it now, as on Apple, so a second copy here would be a second owner.
for s in ___kill ___connect_nocancel ___select ___select_nocancel ___recvmsg_nocancel ___sendmsg_nocancel _socket _fileport_makefd _fileport_makeport ___proc_info_extended_id; do
    "$CLANG" "${ASMFLAGS[@]}" "$TMP/${s}.s" -o "$OUT/xpc_syscall_obj/sys_${s}.o"
done
rm -rf "$TMP"
trap - EXIT
echo "compiled: 11 raw syscall trap stubs"

# --- real, already-generated dyld-syscall-gen stubs (reused) -------------
for s in ___thread_selfid ___process_policy _proc_rlimit_control; do
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -arch arm64 -I"$OUT/sys_asm_compat" -idirafter "$XNU/osfmk" \
        "$OUT/dyld_syscall_gen/${s}.s" -o "$OUT/xpc_syscall_obj/dyldgen_${s}.o"
done
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    libunwind_sched_yield.s -o "$OUT/xpc_syscall_obj/simple_sched_yield_trap.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libunwind_sched_yield.c -o "$OUT/xpc_syscall_obj/simple_sched_yield.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    simple_cmds_getenv_compat.c -o "$OUT/xpc_syscall_obj/getenv_compat.o"
echo "reused: 3 dyld_syscall_gen stubs; compiled sched_yield pair + getenv/_NSGetEnviron directly (no longer via build_simple_cmds.sh)"

# --- real Mach trap stubs (new, hand-copied from osfmk/mach/syscall_sw.h) ---
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c mach_semaphore_traps.s -o "$OUT/xpc_syscall_obj/mach_semaphore_traps.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c mach_port_allocate_trap.s -o "$OUT/xpc_syscall_obj/mach_port_allocate_trap.o"

# --- real Apple/FreeBSD C source, compiled as-is or curated ---------------
CFLAGS_XNU=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I "$XNU/libsyscall/mach" -idirafter "$XNU/osfmk")
CFLAGS_BSD=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I "$OUT/dyld_compat_sys_override" -idirafter "$XNU/bsd" -idirafter "$XNU/osfmk" -idirafter "$XNU/libsyscall/mach")

"$CLANG" "${CFLAGS_XNU[@]}" "$XNU/libsyscall/mach/mach_error_string.c" -o "$OUT/xpc_syscall_obj/mach_error_string.o"
"$CLANG" "${CFLAGS_XNU[@]}" "$XNU/libsyscall/mach/mig_deallocate.c" -o "$OUT/xpc_syscall_obj/mig_deallocate.o"
"$CLANG" "${CFLAGS_XNU[@]}" -include mach_msg_trap_decl.h mach_msg_send_compat.c -o "$OUT/xpc_syscall_obj/mach_msg_send_compat.o"
"$CLANG" "${CFLAGS_XNU[@]}" mach_port_allocate_compat.c -o "$OUT/xpc_syscall_obj/mach_port_allocate_compat.o"
"$CLANG" "${CFLAGS_XNU[@]}" mach_semaphore_wrap_compat.c -o "$OUT/xpc_syscall_obj/semaphore_wrap.o"

"$CLANG" "${CFLAGS_BSD[@]}" -I "$CR/include" "$CR/CrashReporterClient.c" -o "$OUT/xpc_syscall_obj/crc.o"
"$CLANG" "${CFLAGS_BSD[@]}" -idirafter "$LIBC/include" -idirafter "$LIBC/darwin" "$LIBC/stdlib/FreeBSD/setenv.c" -o "$OUT/xpc_syscall_obj/setenv.o"
"$CLANG" "${CFLAGS_BSD[@]}" environ_lock_compat.c -o "$OUT/xpc_syscall_obj/environ_lock_compat.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    findenv_locked_compat.c -o "$OUT/xpc_syscall_obj/findenv_locked_compat.o"
"$CLANG" "${CFLAGS_BSD[@]}" -idirafter "$XNU/libsyscall/wrappers/libproc" "$XNU/libsyscall/wrappers/libproc/libproc.c" -o "$OUT/xpc_syscall_obj/libproc.o"
"$CLANG" "${CFLAGS_BSD[@]}" xpc_select_compat.c -o "$OUT/xpc_syscall_obj/select_wrap.o"
"$CLANG" "${CFLAGS_BSD[@]}" xpc_syscall_wrappers_compat.c -o "$OUT/xpc_syscall_obj/xpc_syscall_wrappers_compat.o"
"$CLANG" "${CFLAGS_BSD[@]}" "$XNU/libsyscall/wrappers/kill.c" -o "$OUT/xpc_syscall_obj/kill_wrap.o"
echo "compiled: real Apple/FreeBSD C source (curated where documented above)"

rm -f "$OUT/libxpc_syscalls.a"
ar rcs "$OUT/libxpc_syscalls.a" "$OUT"/xpc_syscall_obj/*.o
echo "wrote $OUT/libxpc_syscalls.a ($(ls "$OUT"/xpc_syscall_obj/*.o | wc -l | tr -d ' ') objects)"

# Real trial re-link against everything (libxpc.a + liblaunchd_mig.a +
# libdispatch_min.a + this script's own new archive) to show the real,
# current remaining gap -- informational, not a hard failure (see this
# script's own HONEST SCOPE STATEMENT above for what's still open).
if [ -f "$OUT/xpc_trial_main.o" ] && [ -f "$OUT/libxpc.a" ]; then
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
        -o xpc_smoketest.macho \
        ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
        "$OUT/xpc_trial_main.o" \
        "$OUT/libxpc.a" "$OUT/liblaunchd_mig.a" "$OUT/libdispatch_real.a" "$OUT/libxpc_syscalls.a" \
        "$OUT/libmach_ipc.a" "$OUT/libclosure.a" "$OUT/libmalloc.a" "$OUT/cerror_stub.o" "$OUT/libplatform.a" \
        "$OUT/libc_string.a" "$OUT/libc_locale_for_stdio.a" "$OUT/libc_stdio.a" "$OUT/libc_gen.a" "$OUT/libc_printf.a" \
        "$OUT/libpthread.a" \
        "$OUT/simple_obj/sys__kevent.o" "$OUT/simple_obj/sys__kqueue.o" "$OUT/simple_obj/sys__dup.o" "$OUT/simple_obj/sys__geteuid.o" "$OUT/simple_obj/sys__getuid.o" \
        -Wl,-alias,_mprotect,'_mprotect$UNIX2003' \
        -Wl,-alias,___kill,'_kill$UNIX2003' \
        -Wl,-alias,___kill,_kill \
        -Wl,-alias,_write,'_write$UNIX2003' \
        -Wl,-alias,__mal_semwait_sleep,'_sleep$UNIX2003' \
        -Wl,-alias,___kdebug_trace,_kdebug_trace \
        -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003',_pthread_rwlock_rdlock \
        -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003',_pthread_rwlock_unlock \
        2>"$OUT/xpc_smoketest_link.err" && echo "wrote xpc_smoketest.macho (full real link succeeded)" \
        || { echo "expected-for-now: real trial link found $(grep -c 'Undefined symbol' "$OUT/xpc_smoketest_link.err") undefined symbol group(s) -- see this script's own header comment for the real, categorized list:"; grep '^  "' "$OUT/xpc_smoketest_link.err" | sed 's/,.*//' | sort -u; }
fi
