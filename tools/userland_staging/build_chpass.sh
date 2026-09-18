#!/bin/bash
# Builds chpass.macho: real, unmodified Apple system_cmds/chpass.tproj/
# {chpass,edit,field,table,util,pw_copy}.c (DAR-174, Phase 5 account-
# provisioning tools -- 4 of 4; vipw/passwd already built, see commit
# c920b26, pwd_mkdb built earlier in this same pass). Same
# Csu-dynamic-link, -Dmain=..., TSD-bootstrap-wrapper shape as
# build_vipw.sh/build_passwd.sh/build_pwd_mkdb.sh (read build_getty.sh's
# own header before touching this one).
#
# THE REAL BLOCKER THIS SCRIPT CLOSES -- read before assuming OPEN_DIRECTORY
# support is missing by accident: real, unmodified chpass.tproj/chpass.c's
# own non-OPEN_DIRECTORY branch calls pw_init(NULL,NULL)/pw_fini()/
# pw_tempname()/pw_equal() (plus edit.c's own real pw_tmp()/pw_edit()/
# pw_dup()/pw_make() calls), none of which are defined ANYWHERE in this
# checkout (confirmed via exhaustive grep). Real Apple's own build always
# defines OPEN_DIRECTORY for this target (confirmed via a real `grep
# OPEN_DIRECTORY system_cmds.xcodeproj/project.pbxproj`:
# GCC_PREPROCESSOR_DEFINITIONS unconditionally adds it, both Debug and
# Release) -- so this branch was apparently never build-tested by Apple
# either, same real finding class as build_libc_db.sh's
# __DBINTERFACE_PRIVATE gap and build_pwd_mkdb.sh's _PW_NAME_LEN/
# _PW_YPTOKEN gap.
#
# This project's real account backend is flat /etc/master.passwd via
# Libinfo (no opendirectoryd -- see docs/multiuser-userland-plan.md), so
# building with OPEN_DIRECTORY (which needs a real OpenDirectory
# framework + CFDictionary-based record editing this project doesn't
# have) is not an option; the non-OPEN_DIRECTORY branch is the only real
# fit, same decision build_passwd.sh already made for passwd.tproj's own
# INFO_FILE default.
#
# tools/userland_staging/chpass_pw_util_freebsd.c (real, adapted FreeBSD
# lib/libutil/pw_util.c -- see that file's own header for the full real
# citation) + chpass_flopen_freebsd.c (real, essentially-unmodified
# FreeBSD lib/libutil/flopen.c, pw_lock()'s own dependency) close this
# gap. Both pulled from this project's own pinned third_party/
# freebsd-src @ release/13.2.0 (setup_third_party.sh's FREEBSD_DEST
# block).
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
CHPASS="../../third_party/system_cmds/chpass.tproj"
VIPW="../../third_party/system_cmds/vipw.tproj"
OUT="libc_build"
rm -rf "$OUT/chpass_obj"
mkdir -p "$OUT/chpass_obj" "$OUT/compat_include"

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

# Same real flags as build_vipw.sh/build_passwd.sh/build_pwd_mkdb.sh --
# same tier, same gen/locale/stdtime -I set, plus:
#   -I chpass_compat_include FIRST: chpass.c's/edit.c's own real
#     `#include <libutil.h>` must see the pw_*() prototypes this build
#     adds (see that header's own comment -- it #include_next's the real
#     shared compat_include/libutil.h afterward, nothing `ls` needs is
#     lost).
#   -I $CHPASS: chpass's own chpass.h/pw_copy.h.
#   chpass.c's/edit.c's own real `#include <pw_scan.h>`
#     (non-OPEN_DIRECTORY branch) resolves to chpass_compat_include/
#     pw_scan.h -- real FreeBSD lib/libc/gen/pw_scan.h, NOT
#     system_cmds/pwd_mkdb.tproj/pw_scan.h (same filename, incompatible
#     3-arg-pw_scan API -- see chpass_compat_include/pw_scan.h's own
#     header for the full citation). pwd_mkdb.tproj is deliberately NOT
#     added to this build's -I list.
#   -I $VIPW: pw_copy.c's own real `#include "pw_util.h"` (a quoted,
#     local-search include -- chpass.tproj itself has NO pw_util.h,
#     confirmed via `ls`) resolves to vipw.tproj's real pw_util.h here,
#     which already declares pw_error() with the exact signature
#     pw_copy.c calls (matches chpass_pw_util_freebsd.c's own real
#     pw_error() -- see that file's header). vipw's pw_util.h also
#     declares a 0-arg pw_init()/pw_lock()/etc that pw_copy.c never
#     calls -- harmless, different translation unit, C doesn't
#     cross-check signatures across .o files.
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h -include $OUT/compat_include/stdio_flockfile_compat.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I chpass_compat_include \
    -I $CHPASS -I $VIPW \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD -I $LIBC/gen \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall -I $XNU/libsyscall/wrappers \
    -I ../libinfo_custom \
    -I ../../generated"

# Real, unmodified chpass.tproj source -- chpass.c/edit.c/field.c/
# table.c/util.c/pw_copy.c (every real .c file the Apple xcodeproj's own
# chpass target compiles, confirmed via a real grep of
# system_cmds.xcodeproj/project.pbxproj's Sources build phase -- NOT
# open_directory.c, which is OPEN_DIRECTORY-only and pulls in real
# OpenDirectory framework types this project doesn't have).
#
# real Libc/gen/FreeBSD/err.c + stdlib/FreeBSD/getopt.c: standard
# tier-1 surface, already-proven reused verbatim.
# Libc/gen/sigsetops.c: real sigemptyset()/sigaddset() -- pw_edit()
# (chpass_pw_util_freebsd.c) calls both.
# Libc/gen/raise.c: real raise() -- pw_edit()'s own
# `raise(WSTOPSIG(pstat))` call (WIFSTOPPED path). Real Apple source,
# already-proven-buildable tier (confirmed via a real compile here).
# Libc/gen/getusershell.c: real getusershell()/setusershell()/
# endusershell() -- util.c's own real ok_shell()/dup_shell() call these
# to validate/offer a shell against /etc/shells (same real gap already
# closed for build_pwd_mkdb.sh's own pw_scan.c).
# cp_mv_syscalls.c (the whole file, as vipw/passwd pull in) is
# deliberately NOT used here: its own real `_sigaction` wrapper
# duplicate-symbols against liblaunchd_syscalls.a's
# private_names_compat.o, found via a real `-Wl,-why_load` link trace --
# chpass_pw_util_freebsd.c's real execl()/execlp() calls need
# `_execve`/`__execve` from that same archive member (vipw/passwd never
# need `__execve`, only `_execve`, so they never pull that member and
# never hit this). chpass_fork_compat.c below extracts JUST that file's
# real fork()/vfork() (needed for an unrelated second real reason -- see
# that file's own header) without its conflicting `_sigaction`.
# ps_passwd_compat.c + ../libinfo_custom/libinfo_custom.c: real
# getpwnam()/getpwuid() -- chpass.c's own real
# `getpwuid(uid)`/`getpwnam(*argv)` target-user lookups, same flat-file
# resolver already proven for passwd.tproj/pwd_mkdb.tproj.
# Libc/gen/crypt.c + Libc/gen/FreeBSD/readpassphrase.c: real DES
# crypt()/getpass() -- chpass.c's own real password-reentry check
# (`crypt(password, old_pw->pw_passwd)`/`getpass("Password: ")`), same
# real files already proven by build_login.sh/build_passwd.sh against
# this same /etc/master.passwd.
# chpass_pw_copy_tempname_compat.c: real, honest link-satisfying stub
# for pw_copy.c's own extern `tempname` (see that file's own header for
# the full citation -- pw_copy() itself is dead code in this build).
EXTRA_FILES="
$LIBC/gen/FreeBSD/err.c
$LIBC/stdlib/FreeBSD/getopt.c
$LIBC/gen/sigsetops.c
$LIBC/gen/raise.c
$LIBC/gen/getusershell.c
$LIBC/gen/crypt.c
$LIBC/gen/FreeBSD/readpassphrase.c
simple_cmds_read_compat.c
simple_cmds_getenv_compat.c
simple_cmds_mmap_compat.c
chpass_fork_compat.c
ps_passwd_compat.c
../libinfo_custom/libinfo_custom.c
chpass_pw_util_freebsd.c
chpass_flopen_freebsd.c
chpass_pw_scan_freebsd.c
chpass_pw_copy_tempname_compat.c
"

ok=0
skip=0
for f in "$CHPASS/chpass.c" "$CHPASS/edit.c" "$CHPASS/field.c" "$CHPASS/table.c" "$CHPASS/util.c" "$CHPASS/pw_copy.c"; do
    base=$(basename "$f" .c)
    out="$OUT/chpass_obj/${base}.o"
    extra_def=""
    if [ "$base" = "chpass" ]; then
        extra_def="-Dmain=chpass_cmd_real_main"
    fi
    if "$CLANG" $CFLAGS $extra_def "$f" -o "$out" 2>"$OUT/chpass_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/chpass_obj/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
        cat "$OUT/chpass_obj/${base}.err" >&2
    fi
done
if "$CLANG" $CFLAGS chpass_main_wrapper.c -o "$OUT/chpass_obj/chpass_main_wrapper.o" 2>"$OUT/chpass_obj/chpass_main_wrapper.err"; then
    ok=$((ok + 1))
    rm -f "$OUT/chpass_obj/chpass_main_wrapper.err"
else
    skip=$((skip + 1))
    echo "  skip: chpass_main_wrapper"
    cat "$OUT/chpass_obj/chpass_main_wrapper.err" >&2
fi
for f in $EXTRA_FILES; do
    base=$(basename "$f" .c)
    out="$OUT/chpass_obj/${base}.o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>"$OUT/chpass_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/chpass_obj/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
        cat "$OUT/chpass_obj/${base}.err" >&2
    fi
done

echo "chpass (real Apple system_cmds/chpass.tproj): $ok compiled, $skip skipped"
if [ "$skip" -ne 0 ]; then
    echo "error: chpass has real compile failures, not building chpass.macho" >&2
    exit 1
fi

# Real sched_yield trap + C wrapper -- same real files every richer tier
# already links.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    libunwind_sched_yield.s -o "$OUT/chpass_obj/chpass_sched_yield_trap.o"
"$CLANG" $CFLAGS libunwind_sched_yield.c -o "$OUT/chpass_obj/chpass_sched_yield.o"
# DAR-314: locale globals are LOCAL inside libsystem_c.dylib, not exports.
"$CLANG" $CFLAGS libc_locale_globals_compat.c -o "$OUT/chpass_obj/libc_locale_globals_compat.o"
"$CLANG" $CFLAGS "$LIBC/locale/FreeBSD/table.c" -o "$OUT/chpass_obj/locale_table.o"
"$CLANG" $CFLAGS "$LIBC/locale/FreeBSD/none.c" -o "$OUT/chpass_obj/none.o"

# ___gdtoa_locks (COMMON/tentative symbol, glue.o): same real gap already
# fixed in build_cron.sh/build_getty.sh/build_login.sh/build_vipw.sh/
# build_passwd.sh/build_pwd_mkdb.sh -- `ar x` it directly (never
# -force_load).
mkdir -p "$OUT/chpass_glue_extract"
(cd "$OUT/chpass_glue_extract" && ar x "../libc_printf.a" glue.o)

# DYNAMIC LINK (DAR-314). chpass runs after launchd/dyld are up (invoked
# interactively by a logged-in shell, never before), so it is dynamic
# like every non-pwtool binary in this project.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE)" >&2
    exit 1
fi
# libcopyfile.dylib/libremovefile.dylib listed explicitly (DAR-217, see
# build_pwd_mkdb.sh's own identical citation): real fchmod()/rename()
# (pw_copy.c's own real fchmod(tfd, ...) + pwd_mkdb-adjacent rename use)
# live there in this project's current flattened dylib layout, not
# libsystem_kernel where real Apple would put them.
LINK_DYLIBS="$OUT/system/libsystem_c.dylib $OUT/system/libsystem_kernel.dylib $OUT/system/libsystem_pthread.dylib $OUT/system/libsystem_malloc.dylib $OUT/system/libsystem_platform.dylib $OUT/system/libdyld.dylib $OUT/system/libcopyfile.dylib $OUT/system/libremovefile.dylib $OUT/system/libSystem.B.dylib"

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o chpass.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT"/chpass_obj/*.o \
    "$OUT/liblaunchd_syscalls.a" \
    "$OUT/libmach_ipc.a" \
    "$OUT/libc_regex.a" \
    $LINK_DYLIBS \
    -Wl,-alias,__err,_err \
    -Wl,-alias,__warn,_warn \
    -Wl,-alias,_waitpid,'_waitpid$UNIX2003' \
    -Wl,-alias,_setrlimit,'_setrlimit$UNIX2003' \
    -Wl,-alias,_fchmod,'_fchmod$UNIX2003' \
    -Wl,-alias,_stat,'_stat$INODE64' \
    -Wl,-alias,_sigaction,'_sigaction$UNIX2003' \
    -Wl,-alias,_sigprocmask,'_sigprocmask$UNIX2003'
echo "wrote chpass.macho"

# Real dynamic-executable assertions (DAR-314).
bad=0
otool -l chpass.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: chpass: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
otool -l chpass.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: chpass: no LC_LOAD_DYLIB" >&2; bad=1; }
otool -l chpass.macho | grep -q "cmd LC_MAIN" || {
    echo "error: chpass: no LC_MAIN" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1

if [ -x ./bind_target_audit.sh ]; then
    ./bind_target_audit.sh chpass.macho
fi
if [ -x ./dead_stub_audit.sh ]; then
    ./dead_stub_audit.sh chpass.macho
fi
