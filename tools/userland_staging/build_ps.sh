#!/bin/bash
# Builds ps.macho: real, unmodified Apple `ps` (third_party/adv_cmds/ps/,
# tag adv_cmds-176, verified via `git ls-tree macos-112 -- adv_cmds`
# matching this checkout's real commit 44d0e70 exactly) -- this project's
# first Phase 4 step 3 ("real commands") component besides bash itself.
# Same Csu-static-link pattern as every other tier (no dyld).
#
# Real Apple ps.c is ALREADY sysctl-only in its real, shipped form: every
# kvm_openfiles()/libkvm call site in ps.c/print.c/keyword.c is inside a
# real `#if FIXME` block (FIXME is never #defined anywhere in this
# checkout, so `#if FIXME` == `#if 0` -- confirmed by grep, not guessed).
# Real process data comes entirely from a real
# `sysctl(CTL_KERN, KERN_PROC, KERN_PROC_ALL, ...)` call (ps.c ~line 580),
# backed by this project's own real, unmodified
# third_party/xnu-7195/bsd/kern/kern_sysctl.c's sysctl_prochandle() (stock
# xnu source, uses real proc_iterate() -- this project's fork/exec/wait
# already exercises the real process table this reads from real Phase 4
# PID-1 work, so this sysctl path was expected to work without any kernel
# change, and QEMU-testing below confirms it does).
#
# DAR-143 (2026-09-04): real tasks.c (get_task_info(): task_for_pid/
# task_info/task_threads/thread_info, real mach_msg()-based MIG RPC) IS
# NOW compiled -- ps_task_info_stub.c's honest-zeros degradation path is
# retired (git-removed, not just unused) now that real userland Mach IPC
# for the task/thread subsystem exists (build_libmach_ipc.sh, extended
# for this ticket: task_for_pid is a raw trap -45; task_info/task_threads
# were already mig-regenerated+archived into libmach_ipc.a by that
# script's original Phase 4.6 item 5 pass, just unused until now;
# thread_info needed a new thread_act.defs mig regen, added by this
# ticket). CPU%/RSS/VSZ/thread-state columns now come from real, live
# kernel data via real Mach IPC -- see tasks.c's own real get_task_info()
# body, completely unmodified. ps_mach_task_self_decl.h is the one real
# compat shim tasks.c itself needs (see that file's own header comment:
# the mach_task_self() MACRO, not the raw global this project's other
# Mach-IPC code already uses, is shadowed by XNU's own osfmk/mach/mach.h
# beating the SDK's user-facing copy under this script's -I ordering).
#
# Real privilege model (task_for_pid_posix_check(), bsd/vm/vm_unix.c,
# unmodified): passes unconditionally for root, same-uid otherwise --
# this project's userland runs single-user as root today, so every
# process ps lists is queryable; no restriction actually bites here.
#
# ps_passwd_compat.c wires real getpwnam/getpwuid/user_from_uid/
# group_from_gid to Phase 5's real tools/libinfo_custom/ flat-file
# resolver against real /etc/master.passwd -- the same account DB login/
# su/pwtool already use -- rather than needing real Apple Libinfo (Phase 4
# step 4, not attempted, correctly low-priority per docs/roadmap.md).
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
PS="../../third_party/adv_cmds/ps"
OUT="libc_build"
rm -rf "$OUT/ps_obj"
mkdir -p "$OUT/ps_obj" "$OUT/compat_include"

# Real libproc.h stand-in: no libproc repo cloned (separate
# apple-oss-distributions repo, not part of this project's Libc pull) and
# not in the iPhoneOS SDK either (confirmed absent from both). Real
# print.c has exactly one real call site needing it (proc_pidinfo(), for
# the PROC_PIDWORKQUEUEINFO "wq" column) -- see ps_libproc_syscall.c for
# the real syscall-backed implementation and its own header comment for
# the real, cross-checked PROC_INFO_CALL_PIDINFO value.
# PROC_PIDWORKQUEUEINFO/struct proc_workqueueinfo themselves need no
# stand-in -- they're already real, unmodified kernel source at
# $XNU/bsd/sys/proc_info.h, already on this build's -I path.
cat > "$OUT/compat_include/libproc.h" <<'EOF'
#ifndef _COMPAT_LIBPROC_H_
#define _COMPAT_LIBPROC_H_
#include <stdint.h>
/* Real Apple libproc.h itself #includes sys/proc_info.h for the
 * PROC_PID-family constants and struct proc_..info types -- print.c
 * relies on that transitive include (calls PROC_PIDWORKQUEUEINFO /
 * struct proc_workqueueinfo without including sys/proc_info.h itself),
 * so this stand-in must too. Real, unmodified kernel source, already on
 * this build's -I path (XNU/bsd). */
#include <sys/proc_info.h>
int proc_pidinfo(int pid, int flavor, uint64_t arg, void *buffer, int buffersize);
#endif
EOF

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

for dep in libplatform.a libmalloc.a libpthread.a libclosure.a libc_gen.a libc_stdio.a libc_printf.a libmach_ipc.a; do
    if [ ! -f "$OUT/$dep" ]; then
        echo "error: $OUT/$dep not found -- run its build_*.sh first (libmach_ipc.a needs build_libmach_ipc.sh, DAR-143)" >&2
        exit 1
    fi
done
# Shared raw mmap/munmap syscall stubs (build_simple_cmds.sh) --
# libpthread.a's pthread_main_thread_bootstrap.o (real crt.c now calls
# pthread_main_thread_bootstrap() unconditionally, DAR-153/164) needs a
# real mmap()/munmap() for its thread-stack allocation; same real gap
# build_getty.sh hit and fixed for DAR-165, never previously applied to
# this script -- confirmed via a real link attempt, not guessed (ps.macho
# didn't actually exist on disk before DAR-143: this script had never
# successfully linked since DAR-153/164 landed).
for dep in sys____mmap.o sys____munmap.o; do
    if [ ! -f "$OUT/simple_obj/$dep" ]; then
        echo "error: $OUT/simple_obj/$dep not found -- run build_simple_cmds.sh first (shared raw syscall stub)" >&2
        exit 1
    fi
done

# Same real flags as build_libc_gen.sh/build_libc_printf.sh (see those
# scripts' own CFLAGS comments), plus -I for adv_cmds/ps's own ps.h/
# extern.h, Libc/include (real kvm.h -- unused function bodies, type
# decls only, see header comment above) and the kernel's real
# bsd/sys/user.h + bsd/sys/resourcevar.h (kernel-private headers, not
# shipped in the iPhoneOS SDK's public usr/include -- same class of gap
# already handled elsewhere in this project via -I $XNU/... paths).
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h -include $OUT/compat_include/stdio_flockfile_compat.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I $PS \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD -I $LIBC/gen \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall \
    -I ../libinfo_custom \
    -I ../../generated"

# Real additional POSIX/BSD surface ps.c/print.c/fmt.c genuinely need
# that no prior Phase 4 tier pulled in yet (err/warn family, ttyname,
# devname, getopt, qsort/bsearch, atoi/strtoul, localtime/strftime,
# real gen/FreeBSD/time.c atop the real sys/gettimeofday.c below) -- real
# Apple/FreeBSD source, same curated-file-list pattern as every other
# tier, not a blind directory glob.
EXTRA_FILES="
$LIBC/gen/FreeBSD/err.c
$LIBC/gen/FreeBSD/ttyname.c
$LIBC/gen/FreeBSD/termios.c
$LIBC/gen/devname.c
$LIBC/gen/FreeBSD/time.c
$LIBC/gen/FreeBSD/vis.c
$LIBC/stdio/FreeBSD/perror.c
$LIBC/stdlib/FreeBSD/getopt.c
$LIBC/stdlib/FreeBSD/qsort.c
$LIBC/stdlib/FreeBSD/bsearch.c
$LIBC/stdlib/FreeBSD/heapsort.c
$LIBC/stdlib/FreeBSD/atoi.c
$LIBC/stdlib/FreeBSD/strtoul.c
$LIBC/sys/gettimeofday.c
$LIBC/gen/get_compat.c
simple_cmds_mmap_compat.c
"
# localtime.c/strftime.c deliberately NOT pulled from real Apple source
# here -- see ps_time_compat.c's own header comment for why (real tzcode
# timezone-database dependency, genuinely out of scope, same class of
# finding as the Phase 4 dyld/libdispatch investigations).

ok=0
skip=0
for spec in "$PS/ps.c:-Dmain=ps_cmd_real_main" "$PS/print.c:" "$PS/keyword.c:" "$PS/fmt.c:" "$PS/nlist.c:" "$PS/tasks.c:-include ps_mach_task_self_decl.h"; do
    f="${spec%%:*}"
    extra="${spec#*:}"
    base=$(basename "$f" .c)
    out="$OUT/ps_obj/${base}.o"
    if "$CLANG" $CFLAGS $extra "$f" -o "$out" 2>"$OUT/ps_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/ps_obj/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
    fi
done
for f in $EXTRA_FILES; do
    base=$(basename "$f" .c)
    out="$OUT/ps_obj/${base}.o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>"$OUT/ps_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/ps_obj/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
    fi
done
echo "ps (real Apple adv_cmds/ps): $ok compiled, $skip skipped (tasks.c now real -- DAR-143, see header comment)"
"$CLANG" $CFLAGS ps_main_wrapper.c -o "$OUT/ps_obj/ps_main_wrapper.o"

"$CLANG" $CFLAGS ps_passwd_compat.c -o "$OUT/ps_obj/ps_passwd_compat.o"
"$CLANG" $CFLAGS ps_libproc_syscall.c -o "$OUT/ps_obj/ps_libproc_syscall.o"
"$CLANG" $CFLAGS ps_extra_syscalls.c -o "$OUT/ps_obj/ps_extra_syscalls.o"
"$CLANG" $CFLAGS ps_err_warn_compat.c -o "$OUT/ps_obj/ps_err_warn_compat.o"
"$CLANG" $CFLAGS ps_time_compat.c -o "$OUT/ps_obj/ps_time_compat.o"
"$CLANG" $CFLAGS ps_commpage_gettimeofday_stub.c -o "$OUT/ps_obj/ps_commpage_gettimeofday_stub.o"
"$CLANG" $CFLAGS ps_fenv_compat.c -o "$OUT/ps_obj/ps_fenv_compat.o"

# Real sched_yield trap + C wrapper: same real files build_copyfile.sh's
# real libpthread integration already added (see libunwind_sched_yield.c/
# .s's own header comments) -- libpthread's contended-rwlock path
# genuinely calls this; reused verbatim rather than duplicated.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    libunwind_sched_yield.s -o "$OUT/ps_obj/ps_sched_yield_trap.o"
"$CLANG" $CFLAGS libunwind_sched_yield.c -o "$OUT/ps_obj/ps_sched_yield.o"
"$CLANG" $CFLAGS ../libinfo_custom/libinfo_custom.c -o "$OUT/ps_obj/libinfo_custom.o"

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -o ps.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT"/ps_obj/*.o \
    "$OUT/simple_obj/sys____mmap.o" "$OUT/simple_obj/sys____munmap.o" \
    "$OUT/libc_printf.a" \
    "$OUT/libc_gen.a" "$OUT/libc_stdio.a" "$OUT/libc_locale_for_stdio.a" "$OUT/libc_gen.a" "$OUT/libc_stdio.a" \
    "$OUT/pthread_tsd_shim_libc_bootstrap.o" \
    "$OUT/libpthread.a" \
    "$OUT/libclosure.a" \
    "$OUT/libmach_ipc.a" \
    "$OUT/libmalloc.a" "$OUT/cerror_stub.o" "$OUT/libplatform.a" \
    -Wl,-alias,'_mmap$UNIX2003',_mmap \
    -Wl,-alias,'_munmap$UNIX2003',_munmap \
    -Wl,-alias,'_pthread_rwlock_wrlock$UNIX2003',_pthread_rwlock_wrlock \
    -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003',_pthread_rwlock_rdlock \
    -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003',_pthread_rwlock_unlock \
    -Wl,-alias,_mprotect,'_mprotect$UNIX2003' \
    -Wl,-alias,___kill,'_kill$UNIX2003' \
    -Wl,-alias,___kill,_kill \
    -Wl,-alias,_write,'_write$UNIX2003' \
    -Wl,-alias,__mal_semwait_sleep,'_sleep$UNIX2003' \
    -Wl,-alias,___kdebug_trace,_kdebug_trace \
    -Wl,-alias,_open,'_open$UNIX2003' \
    -Wl,-alias,_close,'_close$UNIX2003' \
    -Wl,-alias,_closedir,'_closedir$UNIX2003' \
    -Wl,-alias,_opendir,'_opendir$INODE64$UNIX2003' \
    -Wl,-alias,_readdir,'_readdir$INODE64' \
    -Wl,-alias,_fcntl,'_fcntl$UNIX2003' \
    -Wl,-alias,_fstat,'_fstat$INODE64' \
    -Wl,-alias,_lstat,'_lstat$INODE64' \
    -Wl,-alias,_stat,'_stat$INODE64' \
    -Wl,-alias,_fstatfs,'_fstatfs$INODE64' \
    -Wl,-alias,_strerror,'_strerror$UNIX2003' \
    -Wl,-alias,_fputs,'_fputs$UNIX2003' \
    -Wl,-alias,'_getopt$UNIX2003',_getopt \
    -Wl,-alias,_pthread_testcancel,'_pthread_testcancel$UNIX2003'
echo "wrote ps.macho"

otool -l ps.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l ps.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
if nm -u ps.macho | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u ps.macho >&2
fi
