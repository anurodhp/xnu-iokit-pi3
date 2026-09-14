#!/bin/bash
# Builds cron.macho and crontab.macho: real, unmodified Apple `cron`/
# `crontab` (third_party/cron, tag cron-40, matched via
# `git ls-tree macos-112 -- cron` against apple-oss-distributions/
# distribution-macOS, same method as every other Phase 4/5.5 pull -- see
# setup_third_party.sh's own header comment). Same Csu-static-link pattern
# as every other tier (no dyld, see architecture.md).
#
# One real source patch (third_party-patches/cron.patch, applied by
# setup_third_party.sh): do_command.c's IOKit/CoreFoundation battery-check
# and per-user-launchd-bootstrap-switch blocks needed headers
# (IOKit/*, CoreFoundation/*, vproc.h, vproc_priv.h, bootstrap_priv.h) that
# don't exist anywhere in this project (no CoreFoundation, no launchd,
# no battery on this board) -- real scope decisions, not gaps, see the
# patch's own commit message. Everything else in do_command.c, and every
# other file in this tree, is untouched real Apple source.
#
# Real gaps this pull needed that prior tiers already solved or newly
# solve here:
#   - get_compat.c (third_party/Libc/gen/get_compat.{h,c}) -- real,
#     already used by build_uname.sh, same reuse here (crontab.c's
#     COMPAT_MODE()).
#   - syslog(3)/openlog/closelog -- real Apple syslog(3) has no source
#     anywhere in this project's Libc pull (see
#     ifconfig_syslog_compat.c's own header comment); reusing that exact
#     file here too (its openlog/closelog/syslog/vsyslog are fully
#     generic, not ifconfig-specific) rather than duplicating it.
#     cron/config.h defines SYSLOG unconditionally (real Apple default,
#     confirmed by reading it -- the Makefile's own lack of -DSYSLOG is
#     not the real story), so this project's already-real syslog(3)
#     fallback makes every cron job start/end genuinely loggable instead
#     of the silent no-op it would otherwise be built with LOG_FILE also
#     unset (also real Apple's default, per pathnames.h).
#   - CC_MD5() (crontab.c's MD5File(), the legacy one-shot CommonDigest.h
#     wrapper, distinct from md5.c's CCDigest*() SPI usage) -- newly added
#     to md5_commoncrypto_shim.c, see that file's own header comment.
#   - libutil/pidfile.c (third_party/libutil, tag libutil-58.40.2) -- real
#     Apple source, newly pinned this pass (do_command.c/cron.c's
#     pidfile_open/_write/_close/_remove); only pidfile.c itself is
#     compiled, not the rest of libutil (ExtentManager/wipefs/tzlink/
#     realhostname/getmntopts are all unrelated to cron).
#   - daemon(3) (third_party/Libc/gen/FreeBSD/daemon.c) -- real source,
#     first real caller in this project. In practice cron.c's own call
#     site is a no-op here regardless (`getppid() != 1 && daemon(1,0)`
#     short-circuits since this project's custom PID-1 is always cron's
#     real parent, unlike real launchd-supervised macOS) but is still
#     compiled for real rather than stubbed, since it costs nothing and
#     keeps the source path faithful.
#
# LOGIN_CAP is NOT defined (matches real Apple's own Makefile, which
# leaves -DLOGIN_CAP commented out) -- no login.conf/login_cap subsystem
# exists in this project either way.
#
# crontab is installed setuid-root in real macOS (BINMODE=4555) so a
# non-root user can install their own crontab; this project has no
# setuid-helper story yet (docs/roadmap.md Phase 5's "setuid-root helper"
# item, still open) so crontab.macho ships without the setuid bit for
# now -- usable by root only until that lands, tracked there rather than
# invented here.
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
LIBPLATFORM="../../third_party/libplatform"
XNU="../../third_party/xnu-7195"
CRON="../../third_party/cron"
LIBUTIL="../../third_party/libutil"
COMMONCRYPTO="../../third_party/CommonCrypto"
OUT="libc_build"
rm -rf "$OUT/cron_obj" "$OUT/crontab_obj"
mkdir -p "$OUT/cron_obj" "$OUT/crontab_obj" "$OUT/commoncrypto_glue/CommonCrypto"

cp "$COMMONCRYPTO/include/CommonDigest.h" "$OUT/commoncrypto_glue/CommonCrypto/CommonDigest.h"
cp "$COMMONCRYPTO/include/Private/CommonDigestSPI.h" "$OUT/commoncrypto_glue/CommonCrypto/CommonDigestSPI.h"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

for dep in libplatform.a libmalloc.a libpthread.a libclosure.a libc_gen.a libc_stdio.a libc_printf.a libc_string.a; do
    if [ ! -f "$OUT/$dep" ]; then
        echo "error: $OUT/$dep not found -- run its build_*.sh first" >&2
        exit 1
    fi
done

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -DPRIVATE \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I $CRON/cron \
    -I $LIBUTIL \
    -I $OUT/commoncrypto_glue \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD -I $LIBC/gen \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall \
    -idirafter $XNU/BUILD/obj/EXPORT_HDRS/osfmk"

ok=0
skip=0
for spec in \
    "$CRON/cron/cron.c:-Dmain=cron_cmd_real_main" \
    "$CRON/cron/database.c:" \
    "$CRON/cron/do_command.c:" \
    "$CRON/cron/job.c:" \
    "$CRON/cron/popen.c:" \
    "$CRON/cron/user.c:" \
    "$CRON/lib/compat.c:" \
    "$CRON/lib/entry.c:" \
    "$CRON/lib/env.c:" \
    "$CRON/lib/misc.c:" \
    "$LIBUTIL/pidfile.c:" \
    "cron_daemon_compat.c:" \
    "$LIBC/gen/get_compat.c:" \
    "$LIBC/gen/FreeBSD/err.c:" \
    "$LIBC/stdlib/FreeBSD/getopt.c:" \
    "$LIBC/stdlib/FreeBSD/atoi.c:" \
    "$LIBC/stdlib/FreeBSD/strtoul.c:" \
    "ifconfig_syslog_compat.c:" \
    "md5_commoncrypto_shim.c:" \
    "cron_extra_syscalls.c:" \
    "ps_passwd_compat.c:" \
    "../libinfo_custom/libinfo_custom.c:" \
    "ps_time_compat.c:" \
    "simple_cmds_getenv_compat.c:" \
    "md5_rfc1321_core.c:" \
    "$LIBPLATFORM/src/setjmp/generic/sigaction.c:" \
    "$LIBC/gen/FreeBSD/exec.c:" \
    "$LIBC/gen/FreeBSD/signal.c:" \
    "$LIBC/gen/FreeBSD/gethostname.c:" \
    "cron_main_wrapper.c:" \
    ; do
    f="${spec%%:*}"
    extra="${spec#*:}"
    base=$(basename "$f" .c)
    out="$OUT/cron_obj/${base}.o"
    if "$CLANG" $CFLAGS $extra "$f" -o "$out" 2>"$OUT/cron_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/cron_obj/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
        cat "$OUT/cron_obj/${base}.err" >&2
    fi
done
echo "cron (real Apple cron/*.c + lib/*.c): $ok compiled, $skip skipped"
if [ "$skip" -ne 0 ]; then
    echo "error: cron has real compile failures, not a clean tier" >&2
    exit 1
fi

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    libunwind_sched_yield.s -o "$OUT/cron_obj/cron_sched_yield_trap.o"
"$CLANG" $CFLAGS libunwind_sched_yield.c -o "$OUT/cron_obj/cron_sched_yield.o"

# Real gap, found 2026-09-13 via a live build_everything.sh run (first
# time cron.macho's real link with real libc_printf.a/libpthread.a was
# reached): two real issues.
#
# 1. ___vfprintf/___gdtoa_locks/_vfprintf_l undefined even though the
# providing objects (xprintf.o, gdtoa-misc.o, fprintf.o, printf.o) are
# all real members of libc_printf.a already on this link line -- real
# ld64 single-pass left-to-right archive resolution can't satisfy a
# member's own need for ANOTHER member of the SAME archive once other
# archives (libc_gen.a/libc_stdio.a) have already been scanned past it.
# Same real fix build_libc_printf.sh's own smoketest already established
# (libc_printf.a listed twice): list it again after libc_gen.a/
# libc_stdio.a.
#
# 2. _mmap undefined (libpthread.a's real pthread_create() needs it) --
# same NO_SYSCALL_STUB gap as build_libc_stdio.sh/build_libc_gen.sh/
# build_pthread_once_deep_repro.sh (mmap/munmap have no static provider
# since libSystem.B.dylib covers them dynamically only).
MACSDK="$(xcrun --sdk macosx --show-sdk-path 2>/dev/null)"
MMAP_TMPGEN="$(mktemp -d)"
trap 'rm -rf "$MMAP_TMPGEN"' EXIT
ARCHS=arm64 perl "$XNU/libsyscall/xcodescripts/create-syscalls.pl" \
    "$XNU/bsd/kern/syscalls.master" \
    "$XNU/libsyscall/custom" \
    "$XNU/libsyscall/Platforms" \
    iPhoneOS \
    "$MMAP_TMPGEN" >/dev/null
MMAP_SYSCFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -I $XNU/libsyscall/custom -I $XNU/libsyscall/mach -idirafter $XNU/osfmk -idirafter $MACSDK/usr/include"
"$CLANG" $MMAP_SYSCFLAGS "$MMAP_TMPGEN/___mmap.s" -o "$OUT/sys_mmap_for_cron.o"
"$CLANG" $MMAP_SYSCFLAGS "$MMAP_TMPGEN/___munmap.s" -o "$OUT/sys_munmap_for_cron.o"
rm -rf "$MMAP_TMPGEN"
trap - EXIT
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -o cron.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT"/cron_obj/*.o \
    "$OUT/libc_printf.a" \
    "$OUT/libc_gen.a" "$OUT/libc_stdio.a" "$OUT/libc_locale_for_stdio.a" "$OUT/libc_string.a" "$OUT/libc_gen.a" "$OUT/libc_stdio.a" \
    "$OUT/libc_printf.a" \
    "$OUT/pthread_tsd_shim_libc_bootstrap.o" \
    "$OUT/libpthread.a" \
    "$OUT/sys_mmap_for_cron.o" "$OUT/sys_munmap_for_cron.o" \
    "$OUT/libclosure.a" \
    "$OUT/libmalloc.a" "$OUT/cerror_stub.o" "$OUT/libplatform.a" \
    "$OUT/libunwind.a" \
    -Wl,-alias,___mmap,_mmap \
    -Wl,-alias,___munmap,_munmap \
    -Wl,-alias,_open,'_open$UNIX2003' \
    -Wl,-alias,_close,'_close$UNIX2003' \
    -Wl,-alias,_write,'_write$UNIX2003' \
    -Wl,-alias,___kill,'_kill$UNIX2003' \
    -Wl,-alias,___kill,_kill \
    -Wl,-alias,__mal_semwait_sleep,'_sleep$UNIX2003' \
    -Wl,-alias,___kdebug_trace,_kdebug_trace \
    -Wl,-alias,_fcntl,'_fcntl$UNIX2003' \
    -Wl,-alias,_fstat,'_fstat$INODE64' \
    -Wl,-alias,_lstat,'_lstat$INODE64' \
    -Wl,-alias,_stat,'_stat$INODE64' \
    -Wl,-alias,__err,_err \
    -Wl,-alias,__warn,_warn \
    -Wl,-alias,_fputs,'_fputs$UNIX2003' \
    -Wl,-alias,_mprotect,'_mprotect$UNIX2003' \
    -Wl,-alias,'_getopt$UNIX2003',_getopt \
    -Wl,-alias,_strerror,'_strerror$UNIX2003' \
    -Wl,-alias,_wait,'_wait$UNIX2003' \
    -Wl,-alias,_select,'_select$1050' \
    -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003',_pthread_rwlock_rdlock \
    -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003',_pthread_rwlock_unlock \
    -Wl,-alias,_closedir,'_closedir$UNIX2003' \
    -Wl,-alias,_fdopen,'_fdopen$UNIX2003' \
    -Wl,-alias,_fopen,'_fopen$UNIX2003' \
    -Wl,-alias,_opendir,'_opendir$INODE64$UNIX2003' \
    -Wl,-alias,_readdir,'_readdir$INODE64' \
    -Wl,-alias,_pwrite,'_pwrite$UNIX2003' \
    -Wl,-alias,_read,'_read$UNIX2003' \
    -Wl,-alias,_waitpid,'_waitpid$UNIX2003'
echo "wrote cron.macho"

otool -l cron.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l cron.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
if nm -u cron.macho | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u cron.macho >&2
fi

# --- crontab ---------------------------------------------------------

ok=0
skip=0
for spec in \
    "$CRON/crontab/crontab.c:-Dmain=crontab_cmd_real_main" \
    "$CRON/lib/compat.c:" \
    "$CRON/lib/entry.c:" \
    "$CRON/lib/env.c:" \
    "$CRON/lib/misc.c:" \
    "$LIBUTIL/pidfile.c:" \
    "$LIBC/gen/get_compat.c:" \
    "$LIBC/gen/FreeBSD/err.c:" \
    "$LIBC/stdlib/FreeBSD/getopt.c:" \
    "$LIBC/stdlib/FreeBSD/atoi.c:" \
    "$LIBC/stdlib/FreeBSD/strtoul.c:" \
    "ifconfig_syslog_compat.c:" \
    "md5_commoncrypto_shim.c:" \
    "cron_extra_syscalls.c:" \
    "crontab_extra_syscalls.c:" \
    "ps_passwd_compat.c:" \
    "../libinfo_custom/libinfo_custom.c:" \
    "ps_time_compat.c:" \
    "simple_cmds_getenv_compat.c:" \
    "md5_rfc1321_core.c:" \
    "$LIBC/gen/FreeBSD/exec.c:" \
    "$LIBC/gen/FreeBSD/signal.c:" \
    "$LIBPLATFORM/src/setjmp/generic/sigaction.c:" \
    "crontab_main_wrapper.c:" \
    ; do
    f="${spec%%:*}"
    extra="${spec#*:}"
    base=$(basename "$f" .c)
    out="$OUT/crontab_obj/${base}.o"
    if "$CLANG" $CFLAGS $extra "$f" -o "$out" 2>"$OUT/crontab_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/crontab_obj/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
        cat "$OUT/crontab_obj/${base}.err" >&2
    fi
done
echo "crontab (real Apple crontab/crontab.c + lib/*.c): $ok compiled, $skip skipped"
if [ "$skip" -ne 0 ]; then
    echo "error: crontab has real compile failures, not a clean tier" >&2
    exit 1
fi

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    libunwind_sched_yield.s -o "$OUT/crontab_obj/crontab_sched_yield_trap.o"
"$CLANG" $CFLAGS libunwind_sched_yield.c -o "$OUT/crontab_obj/crontab_sched_yield.o"

# Same real gaps as cron.macho's own link above (this file's own header
# comment has the full citation): libc_printf.a listed twice for its own
# internal cross-references, plus the real ___mmap/___munmap raw trap
# for libpthread.a's real pthread_create().
MMAP_TMPGEN2="$(mktemp -d)"
trap 'rm -rf "$MMAP_TMPGEN2"' EXIT
ARCHS=arm64 perl "$XNU/libsyscall/xcodescripts/create-syscalls.pl" \
    "$XNU/bsd/kern/syscalls.master" \
    "$XNU/libsyscall/custom" \
    "$XNU/libsyscall/Platforms" \
    iPhoneOS \
    "$MMAP_TMPGEN2" >/dev/null
MMAP_SYSCFLAGS2="-isysroot $SDK -target arm64-apple-ios14.4 -c -I $XNU/libsyscall/custom -I $XNU/libsyscall/mach -idirafter $XNU/osfmk -idirafter $MACSDK/usr/include"
"$CLANG" $MMAP_SYSCFLAGS2 "$MMAP_TMPGEN2/___mmap.s" -o "$OUT/sys_mmap_for_crontab.o"
"$CLANG" $MMAP_SYSCFLAGS2 "$MMAP_TMPGEN2/___munmap.s" -o "$OUT/sys_munmap_for_crontab.o"
rm -rf "$MMAP_TMPGEN2"
trap - EXIT
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -o crontab.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT"/crontab_obj/*.o \
    "$OUT/libc_printf.a" \
    "$OUT/libc_gen.a" "$OUT/libc_stdio.a" "$OUT/libc_locale_for_stdio.a" "$OUT/libc_string.a" "$OUT/libc_gen.a" "$OUT/libc_stdio.a" \
    "$OUT/libc_printf.a" \
    "$OUT/pthread_tsd_shim_libc_bootstrap.o" \
    "$OUT/libpthread.a" \
    "$OUT/sys_mmap_for_crontab.o" "$OUT/sys_munmap_for_crontab.o" \
    "$OUT/libclosure.a" \
    "$OUT/libmalloc.a" "$OUT/cerror_stub.o" "$OUT/libplatform.a" \
    "$OUT/libunwind.a" \
    -Wl,-alias,___mmap,_mmap \
    -Wl,-alias,___munmap,_munmap \
    -Wl,-alias,_open,'_open$UNIX2003' \
    -Wl,-alias,_close,'_close$UNIX2003' \
    -Wl,-alias,_write,'_write$UNIX2003' \
    -Wl,-alias,___kill,'_kill$UNIX2003' \
    -Wl,-alias,___kill,_kill \
    -Wl,-alias,__mal_semwait_sleep,'_sleep$UNIX2003' \
    -Wl,-alias,___kdebug_trace,_kdebug_trace \
    -Wl,-alias,_fcntl,'_fcntl$UNIX2003' \
    -Wl,-alias,_fstat,'_fstat$INODE64' \
    -Wl,-alias,_lstat,'_lstat$INODE64' \
    -Wl,-alias,_stat,'_stat$INODE64' \
    -Wl,-alias,__err,_err \
    -Wl,-alias,__warn,_warn \
    -Wl,-alias,_fputs,'_fputs$UNIX2003' \
    -Wl,-alias,_mprotect,'_mprotect$UNIX2003' \
    -Wl,-alias,'_getopt$UNIX2003',_getopt \
    -Wl,-alias,_strerror,'_strerror$UNIX2003' \
    -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003',_pthread_rwlock_rdlock \
    -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003',_pthread_rwlock_unlock \
    -Wl,-alias,_closedir,'_closedir$UNIX2003' \
    -Wl,-alias,_fdopen,'_fdopen$UNIX2003' \
    -Wl,-alias,_fopen,'_fopen$UNIX2003' \
    -Wl,-alias,_opendir,'_opendir$INODE64$UNIX2003' \
    -Wl,-alias,_readdir,'_readdir$INODE64' \
    -Wl,-alias,_pwrite,'_pwrite$UNIX2003' \
    -Wl,-alias,_read,'_read$UNIX2003' \
    -Wl,-alias,_waitpid,'_waitpid$UNIX2003' \
    -Wl,-alias,_wait,'_wait$UNIX2003' \
    -Wl,-alias,_fchmod,'_fchmod$UNIX2003' \
    -Wl,-alias,_mmap,'_mmap$UNIX2003' \
    -Wl,-alias,_munmap,'_munmap$UNIX2003' \
    -Wl,-alias,'_strftime$UNIX2003',_strftime
echo "wrote crontab.macho"

otool -l crontab.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l crontab.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
if nm -u crontab.macho | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u crontab.macho >&2
fi
