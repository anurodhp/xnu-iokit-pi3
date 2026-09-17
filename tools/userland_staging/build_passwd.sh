#!/bin/bash
# Builds passwd.macho: real, unmodified Apple system_cmds/passwd.tproj/
# passwd.c + file_passwd.c (DAR-174, Phase 5 account-provisioning
# tools). Same Csu-dynamic-link, -Dmain=..., TSD-bootstrap-wrapper shape
# as build_getty.sh/build_login.sh/build_vipw.sh (read build_getty.sh's
# own header before touching this one).
#
# THE REAL "WHICH INFOSYSTEM" DECISION -- read before assuming NIS/
# OpenDirectory/PAM support is missing by accident:
#
# Real, unmodified passwd.h (system_cmds/passwd.tproj/passwd.h) defines
# INFO_NIS/INFO_OPEN_DIRECTORY/INFO_PAM (and passwd.c's own switch cases
# for them) ONLY when `!(TARGET_OS_IPHONE && !TARGET_OS_SIMULATOR)` --
# i.e. only on a macOS or simulator build, NEVER on a real iOS DEVICE
# target (read directly from passwd.h, not assumed). This script builds
# with `-target arm64-apple-ios14.4` against the real iPhoneOS SDK (same
# toolchain every tier in this project uses, see CLAUDE.md), which sets
# TARGET_OS_IPHONE=1 and TARGET_OS_SIMULATOR=0 -- so INFO_NIS/
# INFO_OPEN_DIRECTORY/INFO_PAM are correctly, mechanically undefined for
# THIS build, and passwd.c's own real, designed-in `infosystem =
# INFO_FILE` default (the one remaining branch, passwd.h's own
# `#define INFO_FILE 1` is unconditional) is exactly what runs. This is
# not a workaround -- it is the same real Apple source Apple's own
# device build takes, and it is also the only real fit for this
# project's actual account backend (flat /etc/master.passwd via
# Libinfo, no opendirectoryd/NIS/PAM -- see docs/multiuser-userland-
# plan.md). nis_passwd.c/od_passwd.c/pam_passwd.c are consequently NOT
# compiled at all here: passwd.c's own `#ifdef INFO_NIS`/`#ifdef
# INFO_OPEN_DIRECTORY`/`#ifdef INFO_PAM` switch cases that would call
# them are dead code for this exact real build, not a skipped gap (same
# shape as build_login.sh's own USE_PAM discussion, and as
# build_getty.sh's klogin.c exclusion).
#
# file_passwd.c itself has NO Berkeley DB dependency (confirmed by
# reading it directly) -- it edits /etc/master.passwd in place via
# plain stdio (fgetln/fwrite/rename), unlike vipw's pw_mkdb() which
# execs a separate pwd_mkdb binary. Real, honest gap this leaves,
# already true of real Apple's own file_passwd.c and not something this
# port changes: running `passwd` via the FILE infosystem updates
# /etc/master.passwd but does NOT regenerate /etc/pwd.db / /etc/spwd.db
# (only pwd_mkdb -- or vipw's own pw_mkdb() call -- does that, and this
# project has not yet ported Berkeley DB; see build_vipw.sh's own header
# for the full writeup). Any consumer that reads password data via the
# db-backed path rather than a live /etc/master.passwd scan would see
# stale data until pwd_mkdb exists -- a real, named, pre-existing Apple
# behavior, not introduced here.
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
PASSWD="../../third_party/system_cmds/passwd.tproj"
OUT="libc_build"
rm -rf "$OUT/passwd_obj"
mkdir -p "$OUT/passwd_obj" "$OUT/compat_include"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

for dep in libplatform.a libmalloc.a libpthread.a libclosure.a libc_gen.a libc_stdio.a libc_printf.a \
           libc_locale_for_stdio.a liblaunchd_syscalls.a libmach_ipc.a libc_regex.a; do
    if [ ! -f "$OUT/$dep" ]; then
        echo "error: $OUT/$dep not found -- run its build_*.sh first" >&2
        exit 1
    fi
done

# Same real flags as build_getty.sh/build_login.sh/build_vipw.sh -- same
# tier, same gen/locale/stdtime -I set, plus -I $PASSWD for passwd's own
# passwd.h.
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h -include $OUT/compat_include/stdio_flockfile_compat.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I $PASSWD \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD -I $LIBC/gen \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall -I $XNU/libsyscall/wrappers \
    -I ../libinfo_custom \
    -I ../../generated"

# Real, unmodified passwd.tproj source -- ONLY passwd.c + file_passwd.c
# (see this script's own header for why nis_passwd.c/od_passwd.c/
# pam_passwd.c are correctly excluded, not skipped).
#
# real Libc/gen/crypt.c: real, unmodified Apple DES crypt (zero
# corecrypto dependency), already proven by build_login.sh against the
# SAME /etc/master.passwd this binary also writes to.
# real Libc/gen/FreeBSD/readpassphrase.c: real getpass() (defined
# INSIDE this file, confirmed by reading it -- no separate getpass.c
# exists in this tree), same real termios-masking dance login.c's own
# "Old password:"/"New password:" prompts need.
# real Libc/gen/FreeBSD/err.c + stdlib/FreeBSD/getopt.c: standard
# tier-1 surface.
# cp_mv_syscalls.c: real, already-proven honest vfork()==fork() alias
# (see build_vipw.sh's own comment for the full citation) -- not
# actually called by passwd/file_passwd, but pulled in here for
# consistency with every other tier's shared EXTRA_FILES list; harmless
# if unreferenced.
# ps_passwd_compat.c + ../libinfo_custom/libinfo_custom.c: real
# getpwuid()/getpwnam() (passwd.c's own real `getpwuid(getuid())` no-
# argument-user lookup) -- same flat-file /etc/master.passwd resolver
# already proven for adv_cmds/ps and login.tproj, reused verbatim, not
# reintroduced (real gap found via a real link attempt: "_getpwuid",
# referenced from passwd.c's real main()).
EXTRA_FILES="
$LIBC/gen/crypt.c
$LIBC/gen/FreeBSD/readpassphrase.c
$LIBC/gen/FreeBSD/err.c
$LIBC/stdlib/FreeBSD/getopt.c
simple_cmds_read_compat.c
simple_cmds_getenv_compat.c
simple_cmds_mmap_compat.c
ps_passwd_compat.c
../libinfo_custom/libinfo_custom.c
"

ok=0
skip=0
for f in "$PASSWD/passwd.c" "$PASSWD/file_passwd.c"; do
    base=$(basename "$f" .c)
    out="$OUT/passwd_obj/${base}.o"
    extra_def=""
    if [ "$base" = "passwd" ]; then
        extra_def="-Dmain=passwd_cmd_real_main"
    fi
    if "$CLANG" $CFLAGS $extra_def "$f" -o "$out" 2>"$OUT/passwd_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/passwd_obj/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
        cat "$OUT/passwd_obj/${base}.err" >&2
    fi
done
if "$CLANG" $CFLAGS passwd_main_wrapper.c -o "$OUT/passwd_obj/passwd_main_wrapper.o" 2>"$OUT/passwd_obj/passwd_main_wrapper.err"; then
    ok=$((ok + 1))
    rm -f "$OUT/passwd_obj/passwd_main_wrapper.err"
else
    skip=$((skip + 1))
    echo "  skip: passwd_main_wrapper"
    cat "$OUT/passwd_obj/passwd_main_wrapper.err" >&2
fi
for f in $EXTRA_FILES; do
    base=$(basename "$f" .c)
    out="$OUT/passwd_obj/${base}.o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>"$OUT/passwd_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/passwd_obj/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
        cat "$OUT/passwd_obj/${base}.err" >&2
    fi
done

echo "passwd (real Apple system_cmds/passwd.tproj): $ok compiled, $skip skipped"
if [ "$skip" -ne 0 ]; then
    echo "error: passwd has real compile failures, not building passwd.macho" >&2
    exit 1
fi

# Real sched_yield trap + C wrapper -- same real files every richer tier
# already links.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    libunwind_sched_yield.s -o "$OUT/passwd_obj/passwd_sched_yield_trap.o"
"$CLANG" $CFLAGS libunwind_sched_yield.c -o "$OUT/passwd_obj/passwd_sched_yield.o"
# DAR-314: locale globals are LOCAL inside libsystem_c.dylib, not exports.
"$CLANG" $CFLAGS libc_locale_globals_compat.c -o "$OUT/passwd_obj/libc_locale_globals_compat.o"
"$CLANG" $CFLAGS "$LIBC/locale/FreeBSD/table.c" -o "$OUT/passwd_obj/table.o"
"$CLANG" $CFLAGS "$LIBC/locale/FreeBSD/none.c" -o "$OUT/passwd_obj/none.o"

# ___gdtoa_locks (COMMON/tentative symbol, glue.o): same real gap already
# fixed in build_cron.sh/build_getty.sh/build_login.sh/build_vipw.sh --
# `ar x` it directly (never -force_load).
mkdir -p "$OUT/passwd_glue_extract"
(cd "$OUT/passwd_glue_extract" && ar x "../libc_printf.a" glue.o)

# DYNAMIC LINK (DAR-314). passwd runs after launchd/dyld are up
# (invoked interactively by a logged-in shell, never before), so it is
# dynamic like every non-pwtool binary in this project.
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
    -o passwd.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT"/passwd_obj/*.o \
    "$OUT/liblaunchd_syscalls.a" \
    "$OUT/libmach_ipc.a" \
    "$OUT/libc_regex.a" \
    $LINK_DYLIBS \
    -Wl,-alias,__err,_err \
    -Wl,-alias,__warn,_warn \
    -Wl,-alias,_waitpid,'_waitpid$UNIX2003' \
    -Wl,-alias,_setrlimit,'_setrlimit$UNIX2003' \
    -Wl,-alias,_fchmod,'_fchmod$UNIX2003'
echo "wrote passwd.macho"

# Real dynamic-executable assertions (DAR-314).
bad=0
otool -l passwd.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: passwd: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
otool -l passwd.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: passwd: no LC_LOAD_DYLIB" >&2; bad=1; }
otool -l passwd.macho | grep -q "cmd LC_MAIN" || {
    echo "error: passwd: no LC_MAIN" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1

if [ -x ./dead_stub_audit.sh ]; then
    ./dead_stub_audit.sh passwd.macho
fi
