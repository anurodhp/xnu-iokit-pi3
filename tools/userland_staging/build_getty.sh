#!/bin/bash
# Builds getty.macho: real, unmodified Apple system_cmds/getty.tproj
# (main.c/init.c/subr.c/chat.c), Phase 5.5 item 1 (docs/roadmap.md).
#
# IMPORTANT scope note (read before assuming this is wired into the boot
# path -- it deliberately is NOT): real getty's own contract (main.c,
# after getname() collects just a login NAME) is to
# `execle(LO, "login", ..., name, ..., env)` -- i.e. hand off to a
# SEPARATE real login(1) binary, which then does the actual password
# prompt/account verification/credential drop/shell exec. This project's
# actual login flow (tools/init_binary/init.c's do_login(), invoked
# in-process by single_user_child() before it execve()s straight to
# /bin/bash) does username+password+account-lookup+shell-exec as ONE
# step in ONE process -- there is no separate real login(1) binary at
# all (third_party/system_cmds/login.tproj sits unbuilt too, its own
# undertaking: real PAM/OpenPAM, utmp/wtmp/lastlog writing, /etc/motd --
# comparable in scope to Phase 5's still-open pwd_mkdb/vipw/chpass/passwd
# item). Wiring getty in for real would mean building login.tproj too
# AND splitting this project's current one-shot credential flow across
# the getty|login exec boundary -- a real architectural change, not a
# normal command port -- for a payoff (real per-tty getty spawning) that
# only matters once this project has more than the one real console it
# has today (docs/roadmap.md's own Phase 5.5 item 1 already flags this:
# "low urgency alone"). Per this pass's own explicit scope, that's
# deferred rather than rushed; this script only proves real getty.c
# itself genuinely compiles/links against this project's real Libc
# (needing three syscalls -- revoke/socket/connect -- this project had
# never wired before, see getty_syscalls.c) and documents exactly what
# integration would need. getty.macho is built and installed at /bin/
# getty (via inject_into_sd_image.sh) but nothing execs it yet.
#
# REAL BUG FOUND+FIXED 2026-08-21 (QEMU boot-test): real getty's own
# main() has zero knowledge of this project's TSD-bootstrap machinery --
# gettable("default")'s cgetent() call dereferences __current_locale()
# (xlocale_private.h) off an uninitialized TSD base, an immediate,
# zero-output SIGSEGV, confirmed via bisection with temporary write()
# markers (scratch, git-reverted, never committed). Same fix shape as
# uname's own historic identical bug (see uname_main_wrapper.c): main.c
# is compiled below with -Dmain=getty_cmd_real_main so
# getty_main_wrapper.c's main() -- which calls
# pthread_main_thread_bootstrap() first -- is the real process entry
# point.
#
# real cgetent/cgetstr/cgetnum (Libc/gen/FreeBSD/getcap.c -- gettable(),
# the real /etc/gettytab parser) degrade gracefully to this program's own
# compiled-in defaults (gettystrs[]/gettynums[]/gettyflags[] in
# getty.tproj/init.c) when /etc/gettytab is missing or an entry isn't
# found (cgetent() returns -1, gettable() just syslog()s and returns --
# confirmed by reading subr.c's gettable() directly, not assumed) -- no
# /etc/gettytab needed on the image for this to run.
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
GETTY="../../third_party/system_cmds/getty.tproj"
OUT="libc_build"
rm -rf "$OUT/getty_obj"
mkdir -p "$OUT/getty_obj" "$OUT/compat_include"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

for dep in libplatform.a libmalloc.a libpthread.a libclosure.a libc_gen.a libc_stdio.a libc_printf.a libc_locale_for_stdio.a; do
    if [ ! -f "$OUT/$dep" ]; then
        echo "error: $OUT/$dep not found -- run its build_*.sh first" >&2
        exit 1
    fi
done

# Same real flags as build_ps.sh (see that script's own CFLAGS comment)
# -- same tier, same real gen/locale/stdtime -I set. -I $GETTY for
# getty's own gettytab.h/extern.h/pathnames.h.
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h -include $OUT/compat_include/stdio_flockfile_compat.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I $GETTY \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD -I $LIBC/gen \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall -I $XNU/libsyscall/wrappers \
    -I ../../generated"

# Real getty.tproj sources (main.c/init.c/subr.c/chat.c -- all four real,
# unmodified files that dir has, confirmed by `ls`, not a partial glob).
#
# real Libc/gen/FreeBSD/getcap.c: cgetent/cgetstr/cgetnum (gettable()'s
# real /etc/gettytab parser -- degrades gracefully when missing, see
# getty_syscalls.c's dbopen() comment).
# real Libc/util/login_tty.c: opentty()'s real login_tty() (makes the
# opened tty this process's controlling tty and dup2()s it onto fd
# 0/1/2 -- same real function tools/init_binary/init.c's own
# open_console() conceptually parallels by hand).
# real Libc/gen/oldsyslog.c + cat_socket_stub.c + date_compat.c: real
# openlog()/syslog()/vsyslog()/closelog() -- getty's own openlog("getty",
# ...LOG_CONS...) call. This EXACT bundle (unmodified) is already real,
# built, and QEMU-verified via date(1)'s own build_simple_cmds.sh CMD_LIST
# entry (see that entry's own comment) -- reused verbatim, not
# reintroduced. cat_socket_stub.c's honest ENOSYS socket()/connect() (no
# syslogd exists yet, Phase 5.5 item 2) means vsyslog() takes its own
# real, designed-in LOG_CONS fallback path and writes straight to
# /dev/console instead -- getty's log lines are NOT silently lost, just
# not delivered via the syslog protocol proper.
# real Libc/gen/FreeBSD/{err,gethostname,exec,signal}.c +
# simple_cmds_execve_compat.c: standard tier-1 surface (err/warn,
# gethostname() for main.c's own hostname[] banner substitution,
# execle()/signal() for the real login-name-collected exec path and
# SIGINT/SIGQUIT/SIGALRM handling) -- all already-proven real sources
# reused verbatim from other tiers (hostname/env/nice/cp), not
# reintroduced.
# $XNU/libsyscall/wrappers/unix03/chmod.c: real chmod(2) (opentty()'s
# "chmod 0600 the tty device" step), same real wrapper mkdir/touch/rm/cp
# already use.
# ed_setjmp_compat.c (+ $OUT/ed_setjmp_raw.o at link time): real AAPCS64
# setjmp/longjmp (main.c's own login-timeout unwind, `if (setjmp(timeout))`)
# -- reused verbatim from the ed/diff/less/tar tier, not reintroduced.
# NOT included here: Libc/gen/FreeBSD/termios.c and ttyname.c -- both
# already fully present in libc_gen.a (confirmed via nm before adding,
# not assumed) from an earlier tier's build; recompiling them here would
# be a hard duplicate-symbol link error, not just redundant, and (for
# termios.c specifically) would also pull in a real, separate, genuine
# gap: __tcdrain()'s pthread_testcancel() call, which this project's
# libpthread port has never implemented (confirmed via nm -- no such
# symbol anywhere) -- moot here since getty never calls tcdrain() at all
# (confirmed by grep), so simply not recompiling the file avoids the gap
# entirely rather than needing a new stand-in for it.
EXTRA_FILES="
$LIBC/gen/FreeBSD/getcap.c
$LIBC/util/login_tty.c
$LIBC/gen/oldsyslog.c
cat_socket_stub.c
date_compat.c
$LIBC/gen/FreeBSD/err.c
$LIBC/gen/FreeBSD/gethostname.c
$LIBC/gen/FreeBSD/exec.c
simple_cmds_execve_compat.c
$LIBC/gen/FreeBSD/signal.c
cp_mv_syscalls.c
$LIBC/gen/FreeBSD/usleep.c
simple_cmds_read_compat.c
simple_cmds_nanosleep_compat.c
simple_cmds_getenv_compat.c
$LIBC/stdlib/FreeBSD/getopt.c
$LIBC/stdlib/FreeBSD/atoi.c
$LIBC/stdlib/FreeBSD/strtol.c
$LIBC/gen/get_compat.c
$XNU/libsyscall/wrappers/unix03/chmod.c
ed_setjmp_compat.c
$LIBC/gen/FreeBSD/time.c
ls_fenv_compat.c
$LIBC/sys/gettimeofday.c
ps_commpage_gettimeofday_stub.c
simple_cmds_time_compat.c
$LIBC/stdtime/FreeBSD/localtime.c
$LIBC/stdtime/timezone_unix03.c
$LIBC/stdtime/FreeBSD/asctime.c
$LIBC/stdtime/FreeBSD/strftime.c
$LIBC/stdtime/FreeBSD/timelocal.c
"

ok=0
skip=0
for f in "$GETTY/main.c" "$GETTY/init.c" "$GETTY/subr.c" "$GETTY/chat.c"; do
    base=$(basename "$f" .c)
    out="$OUT/getty_obj/${base}.o"
    # main.c only: real getty's own main() is renamed out of the way so
    # getty_main_wrapper.c's main() (below) becomes the actual process
    # entry point Csu's crt.c calls -- same real gap/fix as
    # build_uname.sh's own uname.c:-Dmain=uname_cmd_real_main entry (see
    # getty_main_wrapper.c's header comment for the exact crash this
    # fixes: __current_locale(), reached from gettable()'s cgetent(),
    # dereferencing a never-bootstrapped TSD base).
    extra_def=""
    if [ "$base" = "main" ]; then
        extra_def="-Dmain=getty_cmd_real_main"
    fi
    if "$CLANG" $CFLAGS $extra_def "$f" -o "$out" 2>"$OUT/getty_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/getty_obj/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
        cat "$OUT/getty_obj/${base}.err" >&2
    fi
done
if "$CLANG" $CFLAGS getty_main_wrapper.c -o "$OUT/getty_obj/getty_main_wrapper.o" 2>"$OUT/getty_obj/getty_main_wrapper.err"; then
    ok=$((ok + 1))
    rm -f "$OUT/getty_obj/getty_main_wrapper.err"
else
    skip=$((skip + 1))
    echo "  skip: getty_main_wrapper"
    cat "$OUT/getty_obj/getty_main_wrapper.err" >&2
fi
for f in $EXTRA_FILES; do
    base=$(basename "$f" .c)
    out="$OUT/getty_obj/${base}.o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>"$OUT/getty_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/getty_obj/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
        cat "$OUT/getty_obj/${base}.err" >&2
    fi
done
echo "getty (real Apple system_cmds/getty.tproj): $ok compiled, $skip skipped"
if [ "$skip" -ne 0 ]; then
    echo "error: getty has real compile failures, not building getty.macho" >&2
    exit 1
fi

"$CLANG" $CFLAGS getty_syscalls.c -o "$OUT/getty_obj/getty_syscalls.o"

# Real sched_yield trap + C wrapper -- same real files every richer tier
# (ps/copyfile/etc) already links (libpthread's contended-rwlock slow
# path genuinely calls this).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    libunwind_sched_yield.s -o "$OUT/getty_obj/getty_sched_yield_trap.o"
"$CLANG" $CFLAGS libunwind_sched_yield.c -o "$OUT/getty_obj/getty_sched_yield.o"

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -o getty.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT"/getty_obj/*.o "$OUT/ed_setjmp_raw.o" \
    "$OUT/simple_obj/sys__access.o" "$OUT/simple_obj/sys__sigprocmask.o" \
    "$OUT/simple_obj/sys____chmod.o" "$OUT/simple_obj/sys__chown.o" \
    "$OUT/simple_obj/sys__dup2.o" "$OUT/simple_obj/sys__execve.o" \
    "$OUT/libc_printf.a" \
    "$OUT/libc_gen.a" "$OUT/libc_stdio.a" "$OUT/libc_locale_for_stdio.a" "$OUT/libc_gen.a" "$OUT/libc_stdio.a" \
    "$OUT/pthread_tsd_shim_libc_bootstrap.o" \
    "$OUT/libpthread.a" \
    "$OUT/libclosure.a" \
    "$OUT/libmalloc.a" "$OUT/cerror_stub.o" "$OUT/libplatform.a" \
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
    -Wl,-alias,_fcntl,'_fcntl$UNIX2003' \
    -Wl,-alias,_fstat,'_fstat$INODE64' \
    -Wl,-alias,_stat,'_stat$INODE64' \
    -Wl,-alias,_strerror,'_strerror$UNIX2003' \
    -Wl,-alias,_fputs,'_fputs$UNIX2003' \
    -Wl,-alias,'_getopt$UNIX2003',_getopt \
    -Wl,-alias,_connect,'_connect$UNIX2003' \
    -Wl,-alias,_fopen,'_fopen$UNIX2003' \
    -Wl,-alias,_writev,'_writev$UNIX2003' \
    -Wl,-alias,_send,'_send$UNIX2003' \
    -Wl,-alias,_setrlimit,'_setrlimit$UNIX2003' \
    -Wl,-alias,_select,'_select$1050' \
    -Wl,-alias,_read,'_read$UNIX2003' \
    -Wl,-alias,_lstat,'_lstat$INODE64' \
    -Wl,-alias,_strchr,_index \
    -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003','__pthread_rwlock_rdlock' \
    -Wl,-alias,'_pthread_rwlock_wrlock$UNIX2003','__pthread_rwlock_wrlock' \
    -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003','__pthread_rwlock_unlock'
echo "wrote getty.macho"

otool -l getty.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l getty.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
if nm -u getty.macho | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u getty.macho >&2
fi
