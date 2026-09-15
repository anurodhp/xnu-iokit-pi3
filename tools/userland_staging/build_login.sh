#!/bin/bash
# Builds login.macho: real, unmodified Apple system_cmds/login.tproj/
# login.c (DAR-164) -- the real username+password prompt this project's
# boot path has never had. Same Csu-static-link, -Dmain=..., TSD-
# bootstrap-wrapper shape as build_getty.sh (read that script's own
# header before touching this one -- this is deliberately its sibling,
# same static (not dylib) link shape, matching build_getty.sh's own real
# `execle(LO, "login", ...)` handoff contract, not build_pd_console_login.sh's
# dylib-linked shape, which is a different, unrelated launchd helper).
#
# ===========================================================================
# THE REAL "WHICH login.c BRANCH" DECISION -- read before assuming PAM is
# needed here:
#
# Real, unmodified login.c is written to compile TWO different real, live
# code paths depending on whether USE_PAM is #defined, both real Apple/
# FreeBSD source, neither one a fabrication:
#   - USE_PAM defined:   pam_start()/pam_authenticate()/pam_acct_mgmt()
#     etc (real OpenPAM). This project's OpenPAM port
#     (build_openpam.sh) is REAL but has a REAL, documented, permanent
#     architectural gap: dlopen()-based module loading is structurally
#     impossible here (no dyld shared cache / codesigning runtime this
#     project has ever ported for that purpose -- see build_openpam.sh's
#     own "THE DLOPEN / MODULE-LOADING ARCHITECTURAL GAP" header) and
#     OpenPAM's own designed static-module escape valve
#     (OPENPAM_STATIC_MODULES) is real but NOT wired up anywhere in this
#     project yet (no static pam_unix-equivalent module exists) --
#     meaning EVERY pam_authenticate() call would structurally fail
#     right now, regardless of a correct password. Wiring that up for
#     real is its own separate, real undertaking (a genuine static PAM
#     module, a real pam.d chain pointing at it), not attempted in this
#     pass.
#   - USE_PAM undefined: login.c's own real, designed-in NON-PAM branch
#     (every `#ifndef USE_PAM` block in login.c) does exactly what this
#     project's other account-auth code already does: `crypt(getpass(...),
#     pwd->pw_passwd)` compared against the real /etc/master.passwd entry
#     (via real getpwnam()), plus real checknologin()/rootterm()
#     (/etc/ttys-backed), real pututxline()/getlastlogx() (utmpx/lastlogx),
#     and real BSM audit calls are ALSO conditional
#     (`#ifdef USE_BSM_AUDIT`, separately undefined here) so login_audit.c/
#     klogin.c (Kerberos, never referenced by login.c at all -- confirmed
#     via grep, not guessed) are correctly excluded, not silently skipped.
#
# This build takes the NON-PAM branch: real, unmodified Apple source,
# same real crypt(3)/DES-based auth this project's tools/pwtool.c and
# tools/libinfo_custom/crypt_des.h already use against the SAME real
# /etc/master.passwd (crypt_des.h's own header documents byte-for-byte
# algorithm equivalence with third_party/Libc/gen/crypt.c, verified, not
# assumed -- a password hash written by `pwtool passwd <user>` is
# genuinely readable by this login.macho's real crypt() call). This is
# the real, correct choice per this project's source-priority rule
# (CLAUDE.md): Apple's own real source already ships a real, complete,
# non-PAM auth path via this exact #ifdef -- using it is not a
# workaround or a fabrication, it's the same real vendored file taking
# its OTHER real, designed branch. USE_PAM can be revisited once a real
# static PAM module chain exists (see OPENPAM_STATIC_MODULES above) --
# tracked as follow-up, not blocking this ticket.
#
# ===========================================================================
# REAL DEPENDENCY CHAIN closed for this pass (each real, not a stub
# unless explicitly marked otherwise):
#   - crypt(3): third_party/Libc/gen/crypt.c, real unmodified Apple DES
#     crypt, zero corecrypto dependency (see crypt_des.h's own header).
#   - getpass(3): third_party/Libc/gen/FreeBSD/readpassphrase.c (real
#     OpenBSD source Apple's Libc vendors), needs real sigaction(2)
#     (launchd_libc_extras.a's Libc_sys_sigaction.o/libplatform_sigaction.o,
#     already real+built) and real termios (already in libc_gen.a per
#     build_getty.sh's own finding).
#   - getpwnam()/getgrnam(): ps_passwd_compat.c, reused verbatim (already
#     real+proven for adv_cmds/ps -- same flat-file resolver, same
#     /etc/master.passwd), plus libinfo_custom.c compiled fresh (its real
#     underlying li_pw_load()/li_getpwnam() engine).
#   - getttynam()/getttyent() (rootterm()/stypeof(), real /etc/ttys
#     parser): third_party/Libc/gen/getttyent.c, real Apple source,
#     degrades gracefully (fopen(_PATH_TTYS) failure -> NULL) exactly
#     like build_getty.sh's own gettytab finding -- no /etc/ttys needed
#     on the image for this to run without crashing (rootterm() then
#     just refuses root logins on an unlisted tty, real BSD behavior).
#   - pututxline()/getlastlogx() (real utmpx/lastlogx): third_party/
#     Libc/gen/NetBSD/utmpx.c + gen/utmpx-darwin.c, real companion pair
#     (confirmed via grep -- utmpx-darwin.c defines `_utmpx_asl()` that
#     NetBSD/utmpx.c's own `_pututxline()` calls by name, they are not
#     independent alternates). _PATH_UTMPX (/var/run/utmpx) is created
#     on first real pututxline() call via a real fopen("w+") fallback
#     (NetBSD/utmpx.c's own __getutxent(), read directly, not assumed);
#     /var/run itself already exists on this project's SD image
#     (inject_into_sd_image.sh already `mkdir -p "$MNT/var/run"` for an
#     unrelated real reason, real bind(2) target dir prep), so no new
#     provisioning needed here.
#   - Real Apple utmpx-darwin.c genuinely backs wtmp/lastlog storage via
#     real ASL (asl_new/asl_set_query/asl_append/asl_match/asl_next/
#     asl_get/asl_release/asl_open/asl_close/asl_send -- confirmed via
#     grep of every asl_*( callsite in that file, not guessed). This
#     project has no real ASL/syslogd bootstrap-namespace registrar yet
#     (docs/daemon-roadmap.md's own standing "notifyd/syslogd/ASL
#     scoping" finding) -- asl_client_compat.c already established the
#     honest-no-op shim for the base 6 ASL entry points (real, existing,
#     reused verbatim here); login_asl_extra_compat.c (new, this ticket)
#     extends it with the other 6 real entry points utmpx-darwin.c
#     specifically needs. Net real, honest, non-crashing effect: every
#     wtmp/lastlog WRITE silently no-ops (matches asl_send()'s own
#     existing no-op contract) and every lastlog READ (getlastlogx())
#     correctly reports "no prior record" (asl_match() returning NULL,
#     the real, correct answer given nothing was ever really stored) --
#     login's own dolastlog() already has a real, designed-in "don't
#     print anything" path for exactly that case, so this is graceful
#     degradation, not a crash or a fabricated login history.
#   - notify_post() (NetBSD/utmpx.c's own UTMPX_CHANGE_NOTIFICATION): a
#     real trial link of this project's already-built libnotify_client.a
#     surfaced its OWN disproportionate real dependency chain (full real
#     libdispatch + XPC + mach_port_allocate/destruct -- the same class
#     of pull as libxpc.a's vproc/MIG cascade below), so
#     login_syscalls.c instead supplies a real, narrow, honest no-op
#     (real NOTIFY_STATUS_OK return) -- see that file's own header for
#     why this is genuinely correct behavior here, not a shortcut (this
#     project has zero real subscribers for UTMPX_CHANGE_NOTIFICATION
#     regardless of whether notify_post() itself is real or stubbed).
#   - Mach "re-enable crash reporter" block (main(), unconditional under
#     `#ifdef __APPLE__`, NOT gated by USE_PAM): real
#     mach_task_self()/task_get_bootstrap_port()/bootstrap_look_up()/
#     task_set_exception_ports()/bootstrap_strerror(). mach_task_self_
#     (libmalloc.a) and task_get_special_port()/task_set_exception_ports()
#     (libmach_ipc.a) are real and already built for other consumers and
#     ARE linked here; task_get_bootstrap_port() itself needed no
#     standalone wrapper at all (real osfmk header macro, see
#     login_syscalls.c). bootstrap_look_up()/bootstrap_strerror() are
#     NOT taken from this project's real libxpc.a -- a real trial link
#     showed that pulls libxpc.a's entire real vproc/MIG client
#     subsystem (~25 more undefined symbols: every `_vproc_mig_*` stub,
#     `_proc_get/set/track_dirty`, `_task_name_for_pid`, `_recvmsg`/
#     `_sendmsg`, `_uuid_unparse_upper`), which would also break this
#     binary's deliberate static-link shape (this project's own
#     launchctl.macho no longer statically embeds that subsystem either,
#     see login_bootstrap_compat.c's own header for the full writeup).
#     login_bootstrap_compat.c instead
#     supplies a real, narrow, honest bootstrap_look_up() returning the
#     real BOOTSTRAP_UNKNOWN_SERVICE code -- correct given this project's
#     launchd never registers "com.apple.ReportCrash" (or any bootstrap
#     service reachable via this real, narrow entry point) regardless.
#     Real, honest runtime behavior either way: login.c's own real,
#     unmodified error-handling `break`s out cleanly on that failure --
#     login still completes normally, just without a live crash
#     reporter, exactly the real degraded behavior a real system missing
#     that service would also see.
#   - initgroups()/getpwuid_r() (dolastlog()'s _pwuid_r, and post-auth
#     initgroups()): liblaunchd_syscalls.a's own launchd_passwd_compat.o,
#     real, already-built, already proven for launchd's core.c -- same
#     flat-file /etc/group backing as ps_passwd_compat.c's getgrnam()
#     above (different file, same real resolver underneath).
#   - klogin.c: NOT compiled -- login.c never references it (confirmed
#     via grep for "klogin"/"KERBEROS" -- zero hits), so it is real,
#     genuinely dead weight for this checkout, not a skipped gap.
#   - login_audit.c: NOT compiled -- entirely `#ifdef USE_BSM_AUDIT`
#     gated, and USE_BSM_AUDIT is not defined here (see the USE_PAM
#     discussion above; real BSM audit is a real, separate follow-up,
#     libbsm.dylib already exists for whenever that lands).
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
LOGIN="../../third_party/system_cmds/login.tproj"
ASL_INC="../../third_party/syslog/libsystem_asl.tproj/include"
XPC_INC="../../third_party/PureDarwin/src/Libraries/XPC/libxpc/include"
OUT="libc_build"
rm -rf "$OUT/login_obj"
mkdir -p "$OUT/login_obj" "$OUT/compat_include"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

for dep in libplatform.a libmalloc.a libpthread.a libclosure.a libc_gen.a libc_stdio.a libc_printf.a \
           libc_locale_for_stdio.a liblaunchd_syscalls.a launchd_libc_extras.a libmach_ipc.a \
           libc_regex.a; do
    if [ ! -f "$OUT/$dep" ]; then
        echo "error: $OUT/$dep not found -- run its build_*.sh first" >&2
        exit 1
    fi
done
# Real gap, found 2026-09-13: 2 items in the loop above derive to the
# wrong candidate -- "liblaunchd_syscalls.a" strips to
# "build_liblaunchd_syscalls.sh" (doesn't exist; real producer is
# build_launchd_syscalls.sh, no "lib" prefix on "launchd"), and
# "launchd_libc_extras.a" strips to "build_launchd_libc_extras.sh"
# (doesn't exist at all; real producer is build_launchd_core.sh, a much
# later script -- this script genuinely needs 3 of its object members,
# extracted below via `ar p`). Named individually here.
if [ ! -f "$OUT/liblaunchd_syscalls.a" ]; then
    echo "error: $OUT/liblaunchd_syscalls.a not found -- run build_launchd_syscalls.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/launchd_libc_extras.a" ]; then
    echo "error: $OUT/launchd_libc_extras.a not found -- run build_launchd_core.sh first" >&2
    exit 1
fi
for f in simple_obj/sys____chmod.o \
         simple_obj/sys__chown.o simple_obj/sys__dup2.o simple_obj/sys__execve.o \
         simple_obj/sys__chflags.o simple_obj/sys__setgid.o simple_obj/sys__setuid.o \
         simple_obj/sys____mmap.o simple_obj/sys__geteuid.o simple_obj/sys__getuid.o \
         simple_obj/sys__getpriority.o; do
    if [ ! -f "$OUT/$f" ]; then
        echo "error: $OUT/$f not found -- run build_simple_cmds.sh (or build_getty.sh, which shares the same simple_obj/ raw stubs) first" >&2
        exit 1
    fi
done

# Same real flags as build_getty.sh (see that script's own CFLAGS
# comment) -- same tier, same gen/locale/stdtime -I set, plus -I $LOGIN
# for login's own login.h/pathnames.h and -I $ASL_INC for real asl.h
# (utmpx-darwin.c's own #include <asl.h>).
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h -include $OUT/compat_include/stdio_flockfile_compat.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -D__APPLE__=1 -DLOGIN_REAL_UTMPX_PUTUTXLINE=1 \
    -I $LOGIN \
    -I $ASL_INC \
    -I $XPC_INC \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/include/NetBSD -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/nbsdcompat \
    -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD -I $LIBC/gen \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall -I $XNU/libsyscall/wrappers \
    -I ../libinfo_custom \
    -I ../../generated"

# Real, unmodified login.tproj source: main.c-equivalent is login.c
# itself (-Dmain rename, see login_main_wrapper.c's header). klogin.c/
# login_audit.c deliberately excluded, see this script's own header.
#
# Real additional POSIX/BSD surface + this ticket's new real compat
# files, same curated-file-list discipline as every other tier.
EXTRA_FILES="
$LIBC/gen/crypt.c
$LIBC/gen/FreeBSD/readpassphrase.c
$LIBC/gen/NetBSD/utmpx.c
$LIBC/gen/utmpx-darwin.c
$LIBC/gen/getttyent.c
$LIBC/gen/oldsyslog.c
cat_socket_stub.c
date_compat.c
$LIBC/gen/FreeBSD/err.c
$LIBC/gen/FreeBSD/exec.c
$LIBC/gen/FreeBSD/signal.c
$LIBC/gen/FreeBSD/usleep.c
simple_cmds_read_compat.c
simple_cmds_nanosleep_compat.c
simple_cmds_getenv_compat.c
$LIBC/stdlib/FreeBSD/getopt.c
$LIBC/stdlib/FreeBSD/atoi.c
$LIBC/stdlib/FreeBSD/strtol.c
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
ps_passwd_compat.c
../libinfo_custom/libinfo_custom.c
asl_client_compat.c
login_asl_extra_compat.c
login_syscalls.c
login_bootstrap_compat.c
simple_cmds_mmap_compat.c
"

ok=0
skip=0
for f in "$LOGIN/login.c"; do
    base=$(basename "$f" .c)
    out="$OUT/login_obj/${base}.o"
    extra_def="-Dmain=login_cmd_real_main"
    if "$CLANG" $CFLAGS $extra_def "$f" -o "$out" 2>"$OUT/login_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/login_obj/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
        cat "$OUT/login_obj/${base}.err" >&2
    fi
done
if "$CLANG" $CFLAGS login_main_wrapper.c -o "$OUT/login_obj/login_main_wrapper.o" 2>"$OUT/login_obj/login_main_wrapper.err"; then
    ok=$((ok + 1))
    rm -f "$OUT/login_obj/login_main_wrapper.err"
else
    skip=$((skip + 1))
    echo "  skip: login_main_wrapper"
    cat "$OUT/login_obj/login_main_wrapper.err" >&2
fi
for f in $EXTRA_FILES; do
    base=$(basename "$f" .c)
    out="$OUT/login_obj/${base}.o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>"$OUT/login_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/login_obj/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
        cat "$OUT/login_obj/${base}.err" >&2
    fi
done

# Real gap found 2026-09-07 re-verifying this script from a clean run:
# oldsyslog.c's vsyslog() calls send()/writev() directly, and nothing on
# this link line ever defined either -- a real, reproducible link
# failure ("Undefined symbols ... _send$UNIX2003 / _writev$UNIX2003,
# referenced from _vsyslog in oldsyslog.o"), not something this fix
# introduces. The -Wl,-alias entries for both, further down this
# script's own link command, have been expecting these base symbols to
# exist since before this fix and were silently never satisfied.
#
# _writev needs no wrapper: it is a plain generated stub (real Apple
# name, no NO_SYSCALL_STUB marker in syscalls.master) -- already built
# as libsystem_kernel/sys_writev.o by build_libsystem_kernel.sh, reused
# directly on this script's own link line below.
#
# _send is different: real bsd/kern/syscalls.master marks syscall #101
# `{ old send }` -- an obsolete/removed raw syscall, not a real trap.
# Real Apple implements the public send() as a Libc-level wrapper over
# sendto(): third_party/Libc/net/FreeBSD/send.c (real, unmodified
# 4.4BSD-derived source, already pinned in this tree) does exactly that
# -- `return __sendto_nocancel(s, msg, len, flags, NULL, 0);` when
# VARIANT_CANCELABLE is not defined, which is the raw trap
# build_libsystem_kernel.sh's own loop already generates as
# libsystem_kernel/sys___sendto_nocancel.o. Compiled separately from
# this script's shared $CFLAGS specifically to NOT define
# VARIANT_CANCELABLE (which the rest of this script's compile does, for
# every OTHER real gen source) -- with it defined, send.c calls the
# cancelable __sendto instead, which this project has never generated a
# raw trap for.
#
# NOT reused from libc_build/openpam_obj/date_compat.o (which also
# defines _send/_writev): that object is a STALE build artifact --
# date_compat.c's CURRENT, checked-in source removed both functions
# under DAR-203 ("date now links libsystem_kernel.dylib directly"), and
# its compiled .o simply predates that fix. Reusing it here would have
# silently reintroduced the exact class of dead-stub shadowing this
# session's own DAR-261/DAR-263 work fixed elsewhere -- confirmed by
# reading date_compat.c's own current header before use, not assumed.
if [ ! -f "$OUT/libsystem_kernel/sys_writev.o" ] || [ ! -f "$OUT/libsystem_kernel/sys___sendto_nocancel.o" ]; then
    echo "error: $OUT/libsystem_kernel/sys_writev.o / sys___sendto_nocancel.o not found -- run build_libsystem_kernel.sh first" >&2
    exit 1
fi
# Staleness guard, generalized from build_launchd.sh's own (DAR-200) --
# see staleness_check.sh's own header for the full why. These two are
# real, pinned-upstream generated syscall stubs, so this rarely fires,
# but the check is cheap and the failure mode it prevents (a silently
# stale raw trap) is exactly the class of bug this session's own
# DAR-249/DAR-263 work spent real time on.
if [ -x ./staleness_check.sh ]; then
    # Both sys_*.o objects checked (code review): they're normally
    # regenerated together by the same build_libsystem_kernel.sh loop
    # run, but nothing enforces that -- checking only one would miss
    # the other going independently stale.
    for f in sys_writev.o sys___sendto_nocancel.o; do
        ./staleness_check.sh "$OUT/libsystem_kernel/$f" LOGIN_ALLOW_STALE_SYSCALLS \
            "$XNU/bsd/kern" -name 'syscalls.master' || exit 1
    done
fi
if "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I "$LIBC/fbsdcompat" -I "$LIBC/include" -I "$LIBC/darwin" \
    "$LIBC/net/FreeBSD/send.c" -o "$OUT/login_obj/send_real.o" 2>"$OUT/login_obj/send_real.err"; then
    ok=$((ok + 1))
    rm -f "$OUT/login_obj/send_real.err"
else
    skip=$((skip + 1))
    echo "  skip: send_real"
    cat "$OUT/login_obj/send_real.err" >&2
fi
echo "login (real Apple system_cmds/login.tproj): $ok compiled, $skip skipped"
if [ "$skip" -ne 0 ]; then
    echo "error: login has real compile failures, not building login.macho" >&2
    exit 1
fi

# Real sched_yield trap + C wrapper -- same real files every richer tier
# already links (libpthread's contended-rwlock slow path genuinely calls
# this).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    libunwind_sched_yield.s -o "$OUT/login_obj/login_sched_yield_trap.o"
"$CLANG" $CFLAGS libunwind_sched_yield.c -o "$OUT/login_obj/login_sched_yield.o"
# DAR-314: locale globals are LOCAL inside libsystem_c.dylib, not exports.
"$CLANG" $CFLAGS libc_locale_globals_compat.c -o "$OUT/login_obj/libc_locale_globals_compat.o"
"$CLANG" $CFLAGS "$LIBC/locale/FreeBSD/table.c" -o "$OUT/login_obj/table.o"
"$CLANG" $CFLAGS "$LIBC/locale/FreeBSD/none.c" -o "$OUT/login_obj/none.o"
# collate.c/collcmp.c: libc_regex.a's ___collate_equiv_value. Same
# local-not-exported situation as the locale trio above.
# NOT ldpart.c: its real __part_load_locale() pulls in ___open_path_locale
# and the rest of the setlocale()/newlocale() category-loader machinery.
# login never calls setlocale/newlocale/uselocale anywhere (verified by
# grep over third_party/system_cmds/login.tproj), so that loader is dead
# code here -- reuse the existing honest-failure stub rather than drag in
# the loader. Same real reasoning chrony's own use of this file documents.
# ldpart.c is NOT compiled: its real __part_load_locale() and collate.c's
# ___collate_load_tables both reach the setlocale()/newlocale() category-
# loader, which login never triggers (it calls no setlocale/newlocale/
# uselocale anywhere -- verified by grep over third_party/system_cmds/
# login.tproj). Reuse the two existing, already-proven honest-failure
# shims rather than dragging in setlocale.c: chrony_part_load_locale_stub.c
# for __part_load_locale/__ldpart_free_extra, and this project's shared
# simple_cmds_libsystem_c_gap_compat.c for __open_path_locale (the same
# file build_simple_cmds.sh uses for exactly this, via COMMON_EXTRA).
"$CLANG" $CFLAGS chrony_part_load_locale_stub.c -o "$OUT/login_obj/part_load_locale_stub.o"
"$CLANG" $CFLAGS simple_cmds_libsystem_c_gap_compat.c -o "$OUT/login_obj/libsystem_c_gap_compat.o"
"$CLANG" $CFLAGS "$LIBC/locale/FreeBSD/collate.c" -o "$OUT/login_obj/collate.o"
"$CLANG" $CFLAGS "$LIBC/locale/FreeBSD/collcmp.c" -o "$OUT/login_obj/collcmp.o"

# Real narrow extraction (same "ar p member > obj" pattern
# build_launchctl.sh's own header already established): real
# sigaction(2) (readpassphrase.c's own real signal-masking dance), NOT
# a whole-archive link of launchd_libc_extras.a -- that archive's OTHER
# member, ncurses_extra_syscalls.o, ALSO defines its own real
# access/kill/sigprocmask/getpgrp (a self-contained bundle built for a
# different consumer), which genuinely duplicate-symbol-conflicted
# against this script's own simple_obj/ raw stubs and login_syscalls.c's
# own getpgrp()/getpgid() in a real trial link (confirmed, not guessed).
# Libc_sys_sigaction.o + libplatform_sigaction.o are the real,
# self-contained pair that provides just `_sigaction` (cross-checked via
# nm before adding this -- no other overlapping symbols).
ar p "$OUT/launchd_libc_extras.a" Libc_sys_sigaction.o > "$OUT/login_obj/login_sigaction_a.o"
ar p "$OUT/launchd_libc_extras.a" libplatform_sigaction.o > "$OUT/login_obj/login_sigaction_b.o"
# Real narrow extraction, same reasoning: ___sigaction (private, raw)
# and __sigtramp (signal-return trampoline) that login_sigaction_b.o
# above needs are BOTH only defined in launchd_libc_extras.a's
# ncurses_extra_syscalls.o (a real, self-contained bundle built for a
# different consumer, ncurses) -- narrow-extracted rather than linking
# the whole archive, same "ar p" pattern. This bundle ALSO happens to
# provide real access/chdir/fpathconf/getpgrp/kill/link/mkdir/select/
# sigprocmask/sleep/strstr -- all real, all needed or harmless, EXCEPT
# it means this script deliberately does NOT also link simple_obj's own
# sys__access.o/sys__sigprocmask.o (would duplicate-symbol-conflict with
# this member's own real _access/_sigprocmask) and login_syscalls.c
# deliberately does NOT define its own getpgrp() (would duplicate-symbol-
# conflict with this member's own real _getpgrp) -- see that file's own
# header comment.
ar p "$OUT/launchd_libc_extras.a" ncurses_extra_syscalls.o > "$OUT/login_obj/login_sigaction_c.o"

# ___gdtoa_locks (COMMON/tentative symbol, glue.o): same real gap
# already fixed in build_cron.sh/build_removefile.sh/build_uname.sh/
# build_bash.sh/build_copyfile.sh/build_getty.sh/build_ifconfig.sh --
# ar x it directly (never -force_load).
mkdir -p "$OUT/login_glue_extract"
(cd "$OUT/login_glue_extract" && ar x "../libc_printf.a" glue.o)

# DYNAMIC LINK (DAR-314, 2026-09-15). login was one of the binaries
# CLAUDE.md previously listed as "deliberately, fully static and must
# stay that way". Superseded: the only real reason to be static is
# running before dyld exists, and login does not -- it is exec'd from
# the launchd -> getty -> login -> bash chain, in which launchd itself
# and pd_console_login.macho are already dynamic. Only
# tools/pwtool/build.py stays static now. See build_ifconfig.sh's block
# for the full reasoning on what is dropped and what is not.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE)" >&2
    exit 1
fi
LINK_DYLIBS="$OUT/system/libsystem_c.dylib $OUT/system/libsystem_pthread.dylib $OUT/system/libsystem_malloc.dylib $OUT/system/libsystem_platform.dylib $OUT/system/libdyld.dylib $OUT/system/libSystem.B.dylib"

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o login.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT"/login_obj/*.o "$OUT/ed_setjmp_raw.o" \
    `# Real raw-syscall wrapper object with no dylib equivalent --` \
    `# libSystem.B.dylib does not export _getpriority (bind_target_audit.sh` \
    `# flagged it UNRESOLVABLE when dropped: a clean link that would have` \
    `# aborted at dyld load, the DAR-217 hazard). Kept across DAR-314.` \
    "$OUT/simple_obj/sys__getpriority.o" \
    "$OUT/liblaunchd_syscalls.a" \
    "$OUT/libmach_ipc.a" \
    "$OUT/libc_regex.a" \
    `# Real raw-syscall objects with no dylib equivalent -- see the` \
    `# send_real.o compile step above. Kept across the conversion.` \
    "$OUT/libsystem_kernel/sys_writev.o" \
    "$OUT/libsystem_kernel/sys___sendto_nocancel.o" \
    $LINK_DYLIBS \
    `# Aliases that survive: each targets a LOCAL definition or a` \
    `# versioned dylib export whose bare name is never exported.` \
    -Wl,-alias,__err,_err \
    -Wl,-alias,__warn,_warn \
    -Wl,-alias,_writev,'_writev$UNIX2003' \
    -Wl,-alias,_send,'_send$UNIX2003' \
    -Wl,-alias,_connect,'_connect$UNIX2003' \
    -Wl,-alias,_ttyname_r,'_ttyname_r$UNIX2003' \
    -Wl,-alias,_waitpid,'_waitpid$UNIX2003'
echo "wrote login.macho"

# Real dynamic-executable assertions (DAR-314) -- inverted, not deleted.
bad=0
otool -l login.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: login: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
otool -l login.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: login: no LC_LOAD_DYLIB" >&2; bad=1; }
otool -l login.macho | grep -q "cmd LC_MAIN" || {
    echo "error: login: no LC_MAIN" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1

# DAR-263 report-only gate. login.macho links libmach_ipc.a and carries
# mach_ipc_dead_stubs.o's mach_port_insert_right/extract_right/unguard
# KERN_FAILURE stand-ins -- confirmed (2026-09-07) reachable ONLY from
# mach_msg_destroy_port()/mach_msg_destroy(), the provably-unreachable
# path they were written for, so this is informational, not a build
# failure. It cannot be upgraded to a hard gate the way build_notifyd.sh
# was: login.macho is REQUIRED to stay fully static (CLAUDE.md), so it
# has no dylib to bind libsystem_kernel.dylib's real body from -- the
# reorder fix that closed DAR-261 is categorically unavailable here.
if [ -x ./dead_stub_audit.sh ]; then
    ./dead_stub_audit.sh login.macho
fi
