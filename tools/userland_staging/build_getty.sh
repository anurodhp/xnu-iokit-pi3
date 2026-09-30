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
# actual login flow (the retired tools/init_binary/init.c's do_login(),
# see docs/static-linking-cleanup-plan.md -- invoked
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

for dep in libplatform.a libmalloc.a libpthread.a libclosure.a libc_gen.a libc_stdio.a libc_printf.a libc_locale_for_stdio.a libmach_ipc.a; do
    if [ ! -f "$OUT/$dep" ]; then
        echo "error: $OUT/$dep not found -- run its build_*.sh first (libmach_ipc.a needs build_libmach_ipc.sh)" >&2
        exit 1
    fi
done
for dep in sys____mmap.o sys____munmap.o; do
    if [ ! -f "$OUT/simple_obj/$dep" ]; then
        echo "error: $OUT/simple_obj/$dep not found -- run build_simple_cmds.sh first (shared raw syscall stub)" >&2
        exit 1
    fi
done

# Real gaps found 2026-09-07 re-verifying this script from a clean run
# -- both are reproducible link failures, not something this fix
# introduces:
#
#   "_send$UNIX2003"/"_writev$UNIX2003", referenced from _vsyslog in
#   oldsyslog.o -- same real gap as build_login.sh's own (see that
#   script's send_real.o compile step for the full why: real
#   Libc/net/FreeBSD/send.c + build_libsystem_kernel.sh's already-built
#   sys_writev.o/sys___sendto_nocancel.o raw traps).
#
#   "_sigaction", referenced from __sigaction in cp_mv_syscalls.o --
#   cp_mv_syscalls.c's own real _sigaction() (mach-o symbol __sigaction,
#   see that file's own header) forwards to the public sigaction(), same
#   real chain DAR-262 established: sigaction() -> __platform_sigaction()
#   (sets sa_tramp = _sigtramp, real trampoline safety) -> the raw
#   __sigaction trap. build_simple_cmds.sh's ~70 DYNAMIC binaries get all
#   of this for free from libsystem_c.dylib/libsystem_kernel.dylib; getty
#   is fully static (CLAUDE.md) and has never linked any of the three
#   real objects that make up this chain outside a dylib. Extracted here
#   from libncurses.a, same real technique build_libsystem_c.sh's own
#   comment already documents for the identical pair of files ("ar x
#   ../../libncurses.a Libc_sys_sigaction.o libplatform_sigaction.o").
if [ ! -f "$OUT/libsystem_kernel/sys_writev.o" ] || [ ! -f "$OUT/libsystem_kernel/sys___sendto_nocancel.o" ] || \
   [ ! -f "$OUT/libsystem_kernel/sys___sigaction.o" ]; then
    echo "error: $OUT/libsystem_kernel/sys_writev.o / sys___sendto_nocancel.o / sys___sigaction.o not found -- run build_libsystem_kernel.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/libncurses.a" ]; then
    echo "error: $OUT/libncurses.a not found -- run build_ncurses.sh first (this script reuses its already-built Libc_sys_sigaction.o/libplatform_sigaction.o members)" >&2
    exit 1
fi
# Staleness guard, generalized from build_launchd.sh's own (DAR-200) --
# see staleness_check.sh's own header for why this exists at all. This
# script extracts real, compiled members OUT of libncurses.a rather than
# compiling their real source itself, so a libncurses.a older than the
# real Libc/libplatform source it was built from would silently hand
# getty.macho a stale sigaction chain -- exactly the class of bug that
# cost real effort tonight (openpam_obj/date_compat.o predating DAR-203's
# real source fix, never recompiled, almost reused here by mistake).
# Scoped to the EXACT two real sources these two extracted members are
# compiled from, not the whole third_party/Libc or libplatform trees:
# a directory-wide check false-positived immediately in testing (a
# totally unrelated locale header touched 2026-08-31 tripped it, while
# the sigaction sources it actually matters for hadn't changed since
# 2026-08-13) -- broad enough to be useless (blocks on noise) is not
# better than the gap this closes.
if [ -x ./staleness_check.sh ]; then
    ./staleness_check.sh "$OUT/libncurses.a" GETTY_ALLOW_STALE_NCURSES \
        "$LIBC/sys" -name 'sigaction.c' || exit 1
    if [ -d "../../third_party/libplatform/src/setjmp/generic" ]; then
        ./staleness_check.sh "$OUT/libncurses.a" GETTY_ALLOW_STALE_NCURSES \
            "../../third_party/libplatform/src/setjmp/generic" -name 'sigaction.c' -o -name 'sigtramp.c' || exit 1
    fi
fi
if [ -x ./staleness_check.sh ]; then
    # All three sys_*.o objects checked, not just sys_writev.o (code
    # review): they're normally regenerated together by the same
    # build_libsystem_kernel.sh loop run, but nothing enforces that --
    # checking only one would miss the other two going independently
    # stale (e.g. a partial rerun, or one copied in from elsewhere).
    for f in sys_writev.o sys___sendto_nocancel.o sys___sigaction.o; do
        ./staleness_check.sh "$OUT/libsystem_kernel/$f" GETTY_ALLOW_STALE_SYSCALLS \
            "$XNU/bsd/kern" -name 'syscalls.master' || exit 1
    done
fi
(cd "$OUT/getty_obj" && ar x ../libncurses.a Libc_sys_sigaction.o libplatform_sigaction.o)

# __sigtramp: libplatform_sigaction.o (just extracted) needs it, and no
# object linked here already defines it. The one real body this project
# has already compiled is bundled inside libncurses.a's own
# ncurses_extra_syscalls.o alongside ~15 unrelated syscall wrappers
# (_read/_kill/_access/_chdir/_sigprocmask/_sleep/...) that DO collide
# with getty's own simple_obj/ copies of several of those same names --
# confirmed by nm before extracting, not assumed. `ld -r -exported_symbol`
# narrows the object down to just __sigtramp (demoting everything else
# in that TU to local/hidden, same real technique as
# build_corefoundation.sh's own mach_vm_region extraction), leaving only
# __sigtramp's own real dependencies as external references
# (__mal_semwait_sleep/__read/__select/_cerror_nocancel/_strlen) --
# confirmed every one of those already resolves from objects this
# script already links (libmalloc.a/libc_gen.a/libc_locale_for_stdio.a/
# cerror_stub.o) before adding this, not assumed.
LD="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
(cd "$OUT/getty_obj" && ar p ../libncurses.a ncurses_extra_syscalls.o > ncurses_extra_syscalls_raw.o && \
    "$LD" -r -arch arm64 -exported_symbol __sigtramp \
        ncurses_extra_syscalls_raw.o -o sigtramp_only.o && \
    rm -f ncurses_extra_syscalls_raw.o)

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
# 0/1/2 -- same real function the retired tools/init_binary/init.c's
# own open_console() conceptually paralleled by hand).
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
#
# REAL LINK GAP FOUND+FIXED 2026-09-01 (DAR-165, wiring getty into boot):
# this script previously failed to link at all --
# simple_cmds_nanosleep_compat.c's `_nanosleep` (pulled in transitively
# via Libc/gen/FreeBSD/usleep.c, itself needed by getty's own DE/timeout
# sleep() calls) needs `mach_ipc_clock_init`/`g_mach_ipc_clock_port`/
# `g_mach_ipc_clock_sem`/`clock_get_time` (build_libmach_ipc.sh's real
# Mach-IPC clock chain, Phase 4.6 item 5), and libpthread.a's
# pthread_main_thread_bootstrap.o needs real `mmap`/`munmap` (getty_
# main_wrapper.c's pthread_main_thread_bootstrap() call, same TSD-bootstrap
# gap every *_main_wrapper.c in this tier documents) -- neither was linked
# in. Both are real, already-built, already-proven components (libmach_ipc.a
# built+QEMU-exercised by build_libmach_ipc.sh/build_ncurses_dylib.sh;
# simple_cmds_mmap_compat.c's real mmap()/munmap() wrapper already proven
# by build_bash.sh/build_libpthread.sh/build_ping.sh), just not previously
# added to getty's own link line -- not a new gap, a wiring omission.
EXTRA_FILES="
$LIBC/gen/FreeBSD/getcap.c
$LIBC/util/login_tty.c
$LIBC/gen/oldsyslog.c
cat_socket_stub.c
date_compat.c
$LIBC/gen/FreeBSD/err.c
$LIBC/gen/FreeBSD/gethostname.c
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
simple_cmds_mmap_compat.c
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

# _send: real Libc/net/FreeBSD/send.c (real, unmodified 4.4BSD-derived
# source), compiled separately from this script's shared $CFLAGS so it
# does NOT see -DVARIANT_CANCELABLE -- with that defined, send.c calls
# the cancelable __sendto, which this project has never generated a raw
# trap for. Without it, send.c calls __sendto_nocancel, which
# build_libsystem_kernel.sh's own loop already generates
# (sys___sendto_nocancel.o, linked below). See build_login.sh's own copy
# of this exact block for the full why -- same real gap, same real fix,
# NOT reused from libc_build/openpam_obj/date_compat.o (a stale build
# artifact predating DAR-203's removal of that file's own fake send/
# writev stubs -- see build_login.sh's comment for the full story).
if "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I "$LIBC/fbsdcompat" -I "$LIBC/include" -I "$LIBC/darwin" \
    "$LIBC/net/FreeBSD/send.c" -o "$OUT/getty_obj/send_real.o" 2>"$OUT/getty_obj/send_real.err"; then
    ok=$((ok + 1))
    rm -f "$OUT/getty_obj/send_real.err"
else
    skip=$((skip + 1))
    echo "  skip: send_real"
    cat "$OUT/getty_obj/send_real.err" >&2
fi

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
# DAR-314: __locale_key/__global_locale and _DefaultRuneXLocale are LOCAL
# inside libsystem_c.dylib, not exports, so the dynamic link must provide
# them. Already-proven shared compat file, not a narrower one (DAR-169).
"$CLANG" $CFLAGS libc_locale_globals_compat.c -o "$OUT/getty_obj/libc_locale_globals_compat.o"
"$CLANG" $CFLAGS "$LIBC/locale/FreeBSD/table.c" -o "$OUT/getty_obj/table.o"
"$CLANG" $CFLAGS "$LIBC/locale/FreeBSD/none.c" -o "$OUT/getty_obj/none.o"

# ___gdtoa_locks (COMMON/tentative symbol, glue.o): same real gap
# already fixed in build_cron.sh/build_removefile.sh/build_uname.sh/
# build_bash.sh/build_copyfile.sh -- libc_printf.a's vfprintf.o/
# xprintf_float.o reference it, but a COMMON symbol reference doesn't
# trigger ld64's normal on-demand archive re-scan, so listing
# libc_printf.a again wouldn't pull glue.o in. `ar x` it directly
# instead (never -force_load).
mkdir -p "$OUT/getty_glue_extract"
(cd "$OUT/getty_glue_extract" && ar x "../libc_printf.a" glue.o)

# DYNAMIC LINK (DAR-314, 2026-09-15). getty was one of the binaries
# CLAUDE.md previously listed as "deliberately, fully static and must
# stay that way". That is superseded: the only real reason to be static
# is running before dyld exists, and getty does not -- it is exec'd by
# launchd, which is itself dynamically linked, so dyld is already up and
# serving. pd_console_login.macho is already dynamic inside this very
# launchd -> getty -> login -> bash chain, which is the direct proof.
# Only tools/pwtool/build.py stays static now.
#
# See build_ifconfig.sh's own block for the full reasoning on what gets
# dropped (the static libc archive stack + the generic UNIX2003/INODE64
# alias block, all covered by real libSystem.B.dylib) and what does not.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE)" >&2
    exit 1
fi
# Real prerequisite check (2026-09-21) -- see build_ifconfig.sh's
# identically-structured block for why this is needed: without it, a
# from-scratch build hits a bare clang "no such file" pile instead of one
# clear message, and gen_build_order.py never learns this is a real
# dependency.
for d in libsystem_c libsystem_pthread libsystem_malloc libsystem_platform; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2
        exit 1
    fi
done
if [ ! -f "$OUT/system/libdyld.dylib" ]; then
    echo "error: $OUT/system/libdyld.dylib not found -- run build_libdyld.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libSystem.B.dylib" ]; then
    echo "error: $OUT/system/libSystem.B.dylib not found -- run build_libsystem.sh first" >&2
    exit 1
fi
LINK_DYLIBS="$OUT/system/libsystem_c.dylib $OUT/system/libsystem_pthread.dylib $OUT/system/libsystem_malloc.dylib $OUT/system/libsystem_platform.dylib $OUT/system/libdyld.dylib $OUT/system/libSystem.B.dylib"

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o getty.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT"/getty_obj/*.o "$OUT/ed_setjmp_raw.o" \
    `# Real raw-syscall wrapper objects with NO dylib equivalent:` \
    `# libSystem.B.dylib does not export _chown/_dup2 (verified` \
    `# by bind_target_audit.sh, which failed UNRESOLVABLE on all three` \
    `# when they were dropped -- a clean link that would have aborted at` \
    `# dyld load, exactly the DAR-217 hazard). Kept across DAR-314.` \
    "$OUT/simple_obj/sys__chown.o" \
    "$OUT/simple_obj/sys__dup2.o" \
    "$OUT/libmach_ipc.a" \
    `# Real raw-syscall objects with no dylib equivalent here -- see the` \
    `# send_real.o compile step above. Kept across the conversion.` \
    "$OUT/libsystem_kernel/sys_writev.o" \
    "$OUT/libsystem_kernel/sys___sendto_nocancel.o" \
    "$OUT/libsystem_kernel/sys___sigaction.o" \
    $LINK_DYLIBS \
    `# Aliases that survive: each targets a LOCAL definition (getty's own` \
    `# objects / the raw-syscall objects above) or a versioned dylib` \
    `# export whose bare name libsystem_c.dylib never exports, so` \
    `# libSystem.B.dylib's baked-in set does not cover them.` \
    -Wl,-alias,__err,_err \
    -Wl,-alias,__warn,_warn \
    -Wl,-alias,_writev,'_writev$UNIX2003' \
    -Wl,-alias,_send,'_send$UNIX2003' \
    -Wl,-alias,_connect,'_connect$UNIX2003' \
    -Wl,-alias,_select,'_select$1050' \
    -Wl,-alias,_setrlimit,'_setrlimit$UNIX2003' \
    -Wl,-alias,_strchr,_index
echo "wrote getty.macho"

# Real dynamic-executable assertions (DAR-314) -- inverted, not deleted.
bad=0
otool -l getty.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: getty: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
otool -l getty.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: getty: no LC_LOAD_DYLIB" >&2; bad=1; }
otool -l getty.macho | grep -q "cmd LC_MAIN" || {
    echo "error: getty: no LC_MAIN" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1

# DAR-263 report-only gate. Same reasoning as build_login.sh's own
# comment: getty.macho carries mach_ipc_dead_stubs.o's stand-ins,
# reachable only from the provably-dead mach_msg_destroy_port()/
# mach_msg_destroy() path, and is REQUIRED to stay fully static
# (CLAUDE.md) so the reorder fix that closed DAR-261 has no dylib to
# target here. Informational only.
if [ -x ./dead_stub_audit.sh ]; then
    ./dead_stub_audit.sh getty.macho
fi
