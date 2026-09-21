#!/bin/bash
# Builds pw.macho: real, unmodified FreeBSD usr.sbin/pw/*.c (DAR-341,
# DAR-329's "user management commands" parent) -- pw(8) is the real BSD
# account-management program `adduser.pl` itself shells out to; this
# project deliberately does NOT build a separate "adduser" wrapper (see
# DAR-341's own real findings: Apple never shipped a plain adduser/
# account-creation CLI at all, and real BSD base's own adduser is a Perl
# script this project has no runtime for). Same Csu-dynamic-link,
# -Dmain=..., TSD-bootstrap-wrapper shape as build_chpass.sh/build_vipw.sh/
# build_passwd.sh/build_pwd_mkdb.sh (read build_getty.sh's own header
# before touching this one).
#
# THE REAL BLOCKERS THIS SCRIPT CLOSES (each with its own dedicated
# adapted file -- see that file's own header for the full citation):
#   - pw_pw_util_freebsd.c: real FreeBSD lib/libutil/pw_util.c, adapted
#     for pw(8)'s own (wider than chpass's) real call surface --
#     pw_init/pw_lock/pw_tmp/pw_copy/pw_mkdb/pw_edit/pw_fini/pw_equal/
#     pw_make/pw_tempname/pw_dup/pw_scan/pw_initpwd, none of which exist
#     anywhere in this checkout (same real gap DAR-174 found for chpass,
#     wider here since pw(8) is a separate binary and needs the real
#     pw_copy()/pw_scan() this project's chpass build had to drop).
#   - chpass_flopen_freebsd.c: real, unmodified FreeBSD lib/libutil/
#     flopen.c (DAR-174), reused verbatim -- pw_lock()'s own dependency,
#     not chpass-specific despite the file name.
#   - chpass_pw_scan_freebsd.c: real, adapted FreeBSD lib/libc/gen/
#     pw_scan.c (DAR-174), reused verbatim -- provides __pw_scan()/
#     __pw_initpwd(), pw_pw_util_freebsd.c's own real pw_scan()/
#     pw_initpwd() wrappers call these.
#   - login_cap.c/login_crypt.c (real, unmodified FreeBSD lib/libutil,
#     pulled via setup_third_party.sh's FREEBSD_DEST block) +
#     pw_crypt_set_format_compat.c (real, narrow, cited crypt_set_format()
#     restricted to this project's one real crypt(3) format -- see that
#     file's own header): pw_user.c's own real login_getpwclass()/
#     login_setcryptfmt()/login_close() calls (6 sites total).
#   - $LIBC/gen/FreeBSD/getcap.c: real cgetent()/cgetstr()/cgetnum()/
#     cgetcap()/cgetclose(), login_cap.c's own real dependency -- not yet
#     compiled into any shipped dylib, compiled directly here (same
#     shape as pw_scan.c already is for chpass).
#   - pw_pwent_grent_compat.c: real setpwent()/getpwent()/endpwent()/
#     setgrent()/getgrent()/endgrent() -- pw.c's own real struct pwf PWF
#     initializer wires GETPWENT()/SETPWENT()/ENDPWENT()/GETGRENT()/
#     SETGRENT()/ENDGRENT() (pw_user_add's own real duplicate-name/
#     free-uid scan, pw_groupnext(), etc.) directly to these; this
#     project's only account-lookup code so far (ps_passwd_compat.c) is
#     single-record only. Real cursor over tools/libinfo_custom.c's
#     already-real li_pw_at()/li_gr_at() indexed accessors -- see that
#     file's own header.
#   - ps_passwd_compat.c (reused, unmodified -- DAR-127/tar): real
#     getpwnam()/getpwuid()/getgrnam()/getgrgid(), pw(8)'s own real
#     GETPWNAM()/GETPWUID() call sites.
#   - $LIBC/gen/FreeBSD/stringlist.c: real StringList sl_init()/sl_add()/
#     etc (BSD <stringlist.h> API, already in this project's pinned Apple
#     Libc but not yet compiled anywhere) -- pw_conf.c's own real default-
#     groups parsing, pw_user.c's own real group-membership building.
#   - $LIBC/stdlib/FreeBSD/strtonum.c: real strtonum(), pw_utils.c's own
#     real pw_checkfd()/pw_checkid() calls (already proven by
#     build_sshd.sh/build_simple_cmds.sh's xargs).
#   - pw_gr_util_freebsd.c: real FreeBSD lib/libutil/gr_util.c, adapted --
#     the group-side counterpart to pw_util.c (gr_init/gr_lock/gr_tmp/
#     gr_copy/gr_mkdb/gr_fini/gr_equal/gr_make/gr_dup/gr_add/gr_scan),
#     needed by grupd.c/pw_group.c/pw_vpw.c/pw_user.c, same real gap
#     class as pw_util.c just never hit before pw(8).
#   - pw_secure_path_freebsd.c: real, unmodified FreeBSD lib/libutil/
#     _secure_path.c -- login_cap.c's own real login_getclass()/
#     login_getpwclass() dependency.
#   - pw_reallocarray_compat.c: real, cited reallocarray() (same real
#     Apple libmalloc-derived implementation already proven by
#     removefile_syscalls.c) -- not declared/defined anywhere in this
#     project's pinned Apple Libc/iPhoneOS SDK headers, needed by
#     pw_pw_util_freebsd.c/pw_gr_util_freebsd.c/login_cap.c.
#   - pw_chflagsat_compat.c: real, narrow, cited chflagsat() built from
#     real openat()+O_SYMLINK (XNU bsd/sys/fcntl.h's own real value,
#     confirmed absent from the public SDK)+fchflags() -- cpdir.c's own
#     real copymkdir() call site; genuinely absent from Darwin (not just
#     an availability-macro block), confirmed via grep.
#   - pw_real_system_compat.c + pw_compat_include/stdlib.h's own
#     `#define system pw_real_system`: pw_user.c's own real, unmodified
#     `system(tmp)`/`system(file)` calls (best-effort at-job/crontab
#     cleanup on `pw userdel`) -- this project's pinned Apple SDK marks
#     system() `__IOS_PROHIBITED` (an App Store policy annotation, not a
#     genuine capability gap on this real-hardware/QEMU target -- see
#     that file's own header for the full citation).
#   - link-time-only real gaps (found via real compiler/linker errors,
#     not guessed): $LIBC/gen/getusershell.c (__pw_scan's own real
#     ok_shell() dependency, already proven by build_chpass.sh but not
#     pulled in here originally), $LIBC/gen/FreeBSD/setmode.c (real
#     getmode()/setmode(), pw_conf.c's own real home-directory-mode
#     parsing), $LIBC/gen/FreeBSD/popen.c (real popen()/pclose(),
#     pw_user_add's own real mail-to-new-user pipe), $LIBC/stdlib/
#     FreeBSD/strtoq.c + $LIBC/stdtime/FreeBSD/strptime.c +
#     $LIBC/stdtime/FreeBSD/timelocal.c (real strtoq()/strptime_l() and
#     the latter's own real __get_current_time_locale() dependency,
#     login_cap.c's/psdate.c's own real dependencies), libc_db.a
#     (already-built DAR-174 dependency, provides real dbopen() --
#     getcap.c's own real DB-format capability-file support), and real
#     raw syscall trap stubs (_fchownat/_readlinkat/_symlinkat --
#     cpdir.c's/pw_user.c's own real home-directory-provisioning calls;
#     ___socketpair -- popen.c's own real bidirectional-pipe setup, via
#     real Apple's own dedicated legacy wrapper libsyscall/wrappers/
#     legacy/socketpair.c, since syscalls.master marks plain
#     `socketpair` NO_SYSCALL_STUB and only the raw, double-underscore
#     `__socketpair` trap is auto-generated) generated the same real way
#     build_libsystem_c.sh's own header already documents (XNU's own
#     libsyscall/xcodescripts/create-syscalls.pl against bsd/kern/
#     syscalls.master), since none of these are exported by any dylib
#     this project has built yet (confirmed via `nm -gU` across every
#     $OUT/system/*.dylib -- plain `socketpair`'s only real export
#     anywhere in this project turned up in libCoreFoundation.dylib, not
#     worth linking the whole of CF for one syscall wrapper pw(8) doesn't
#     otherwise need). One real `-Wl,-alias` bridge is also needed at
#     link time: `_wait4` (Darwin-mangled `wait4`, already real/exported)
#     to `__wait4` (Darwin-mangled `_wait4` -- popen.c's own real,
#     namespace-protected internal call, see pw_compat_include/
#     sys/wait.h's own header). popen.c/setmode.c/strptime.c/the real
#     socketpair.c wrapper themselves need NO such alias for their OWN
#     public names -- confirmed via `nm` on each compiled .o that its own
#     definition already emits the real `$UNIX2003`-suffixed symbol
#     directly (the same asm-renaming macro governs the definition, not
#     just callers).
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
PWSRC="../../third_party/freebsd-src/usr.sbin/pw"
LOGINCAP="../../third_party/freebsd-src/lib/libutil"
OUT="libc_build"
rm -rf "$OUT/pw_obj"
mkdir -p "$OUT/pw_obj" "$OUT/compat_include"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -f "$PWSRC/pw.c" ]; then
    echo "error: $PWSRC/pw.c not found -- run setup_third_party.sh (sparse-checks out usr.sbin/pw from the freebsd-src pin)" >&2
    exit 1
fi
if [ ! -f "$LOGINCAP/login_cap.c" ]; then
    echo "error: $LOGINCAP/login_cap.c not found -- run setup_third_party.sh (sparse-checks out lib/libutil/login_cap.c+.h+login_crypt.c from the freebsd-src pin)" >&2
    exit 1
fi

for dep in libplatform.a libmalloc.a libpthread.a libclosure.a libc_gen.a libc_stdio.a libc_printf.a \
           libc_locale_for_stdio.a liblaunchd_syscalls.a libmach_ipc.a libc_regex.a libc_db.a; do
    if [ ! -f "$OUT/$dep" ]; then
        echo "error: $OUT/$dep not found -- run its build_*.sh first" >&2
        exit 1
    fi
done

# Same real flags/tier as build_chpass.sh, plus:
#   -I pw_compat_include FIRST: pw(8)'s own real `#include <libutil.h>`
#     (grupd.c/pw_group.c/pw_nis.c/pw_vpw.c/pw_user.c/pwupd.c) must see
#     this build's own pw_*() prototypes (see that header's own comment).
#     Its own `#include_next <libutil.h>` must land on $OUT/compat_include's
#     minimal shared stub, NOT chpass_compat_include/libutil.h (DAR-174's
#     own pw_copy() prototype there is a real, different, incompatible
#     signature -- see that file's own header) -- so chpass_compat_include
#     is listed LAST below, after every real libutil.h-providing dir, so
#     #include_next's search-from-here-onward semantics never reach it
#     for <libutil.h> (found the hard way: an earlier draft of this script
#     listed it right after pw_compat_include and got a real
#     "conflicting types for 'pw_copy'" error).
#   -I $PWSRC: pw(8)'s own local pw.h/pwupd.h/bitmap.h/psdate.h.
#   -I $LOGINCAP: pw_user.c's own real `#include <login_cap.h>` resolves
#     to real, unmodified FreeBSD lib/libutil/login_cap.h here (Apple's
#     iPhoneOS SDK has no login_cap.h at all, confirmed via grep -- no
#     collision risk).
#   -I chpass_compat_include LAST: pw_pw_util_freebsd.c's own real,
#     quoted `#include "pw_scan.h"` still resolves here regardless of
#     position (real, unmodified FreeBSD lib/libc/gen/pw_scan.h, DAR-174
#     -- not chpass-specific despite the directory name) -- quoted
#     includes don't chain via #include_next, so position in the list
#     only matters for the <libutil.h> issue above.
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h -include $OUT/compat_include/stdio_flockfile_compat.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I pw_compat_include \
    -I $PWSRC -I $LOGINCAP \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD -I $LIBC/gen \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall -I $XNU/libsyscall/wrappers \
    -I ../libinfo_custom \
    -I ../../generated \
    -I chpass_compat_include"

# Real, unmodified usr.sbin/pw/*.c -- every .c file the real FreeBSD
# Makefile's own SRCS list compiles (pw_nis.c included -- real body has
# no actual NIS/RPC dependency, see setup_third_party.sh's own comment).
PW_REAL_SRCS="pw.c pw_conf.c pw_user.c pw_group.c pw_log.c pw_nis.c pw_vpw.c \
    grupd.c pwupd.c psdate.c bitmap.c cpdir.c rm_r.c strtounum.c pw_utils.c"

# EXTRA_FILES: same-tier real Apple/FreeBSD sources + this ticket's own
# adapted/compat files (see this script's own header for each citation).
EXTRA_FILES="
$LIBC/gen/FreeBSD/err.c
$LIBC/stdlib/FreeBSD/getopt.c
$LIBC/gen/sigsetops.c
$LIBC/gen/raise.c
$LIBC/gen/crypt.c
$LIBC/gen/FreeBSD/stringlist.c
$LIBC/gen/FreeBSD/getcap.c
$LIBC/stdlib/FreeBSD/strtonum.c
$LIBC/gen/getusershell.c
$LIBC/gen/FreeBSD/setmode.c
$LIBC/gen/FreeBSD/popen.c
$LIBC/stdlib/FreeBSD/strtoq.c
$LIBC/stdtime/FreeBSD/strptime.c
$LIBC/stdtime/FreeBSD/timelocal.c
simple_cmds_read_compat.c
simple_cmds_getenv_compat.c
simple_cmds_mmap_compat.c
chpass_fork_compat.c
ps_passwd_compat.c
pw_pwent_grent_compat.c
../libinfo_custom/libinfo_custom.c
pw_pw_util_freebsd.c
pw_gr_util_freebsd.c
pw_secure_path_freebsd.c
chpass_flopen_freebsd.c
chpass_pw_scan_freebsd.c
pw_crypt_set_format_compat.c
pw_reallocarray_compat.c
pw_chflagsat_compat.c
pw_real_system_compat.c
$LOGINCAP/login_cap.c
$LOGINCAP/login_crypt.c
"

ok=0
skip=0
for f in $PW_REAL_SRCS; do
    base=$(basename "$f" .c)
    out="$OUT/pw_obj/${base}.o"
    extra_def=""
    if [ "$base" = "pw" ]; then
        extra_def="-Dmain=pw_cmd_real_main"
    fi
    if "$CLANG" $CFLAGS $extra_def "$PWSRC/$f" -o "$out" 2>"$OUT/pw_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/pw_obj/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
        cat "$OUT/pw_obj/${base}.err" >&2
    fi
done
if "$CLANG" $CFLAGS pw_main_wrapper.c -o "$OUT/pw_obj/pw_main_wrapper.o" 2>"$OUT/pw_obj/pw_main_wrapper.err"; then
    ok=$((ok + 1))
    rm -f "$OUT/pw_obj/pw_main_wrapper.err"
else
    skip=$((skip + 1))
    echo "  skip: pw_main_wrapper"
    cat "$OUT/pw_obj/pw_main_wrapper.err" >&2
fi
for f in $EXTRA_FILES; do
    base=$(basename "$f" .c)
    out="$OUT/pw_obj/${base}.o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>"$OUT/pw_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/pw_obj/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
        cat "$OUT/pw_obj/${base}.err" >&2
    fi
done

echo "pw (real FreeBSD usr.sbin/pw): $ok compiled, $skip skipped"
if [ "$skip" -ne 0 ]; then
    echo "error: pw has real compile failures, not building pw.macho" >&2
    exit 1
fi

# Real sched_yield trap + C wrapper -- same real files every richer tier
# already links.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    libunwind_sched_yield.s -o "$OUT/pw_obj/pw_sched_yield_trap.o"
"$CLANG" $CFLAGS libunwind_sched_yield.c -o "$OUT/pw_obj/pw_sched_yield.o"
# DAR-314: locale globals are LOCAL inside libsystem_c.dylib, not exports.
"$CLANG" $CFLAGS libc_locale_globals_compat.c -o "$OUT/pw_obj/libc_locale_globals_compat.o"
"$CLANG" $CFLAGS "$LIBC/locale/FreeBSD/table.c" -o "$OUT/pw_obj/locale_table.o"
"$CLANG" $CFLAGS "$LIBC/locale/FreeBSD/none.c" -o "$OUT/pw_obj/none.o"

# ___gdtoa_locks (COMMON/tentative symbol, glue.o): same real gap already
# fixed in build_cron.sh/build_getty.sh/build_login.sh/build_vipw.sh/
# build_passwd.sh/build_pwd_mkdb.sh/build_chpass.sh -- `ar x` it directly
# (never -force_load).
mkdir -p "$OUT/pw_glue_extract"
(cd "$OUT/pw_glue_extract" && ar x "../libc_printf.a" glue.o)

# _fchownat/_readlinkat/_symlinkat/_socketpair: real raw syscall traps,
# not exported by any dylib this project has built (confirmed via
# `nm -gU` across every $OUT/system/*.dylib -- unlike their siblings
# _openat/_mkdirat (libsystem_c.dylib) and _unlinkat
# (libremovefile.dylib), all already on this build's own link line;
# _socketpair's only real export anywhere in this project turned up in
# libCoreFoundation.dylib, not worth linking for one syscall wrapper).
# Generated the same real way build_libsystem_c.sh's own header
# documents: XNU's own libsyscall/xcodescripts/create-syscalls.pl
# against bsd/kern/syscalls.master -- cpdir.c's/pw_user.c's own real,
# unmodified home-directory-provisioning calls need the first three;
# popen.c's own real bidirectional-pipe setup needs the fourth (see
# build_pw.sh's own top-of-file header for its own $UNIX2003 alias).
TMPGEN="$(mktemp -d)"
trap 'rm -rf "$TMPGEN"' EXIT
ARCHS=arm64 perl "$XNU/libsyscall/xcodescripts/create-syscalls.pl" \
    "$XNU/bsd/kern/syscalls.master" \
    "$XNU/libsyscall/custom" \
    "$XNU/libsyscall/Platforms" \
    iPhoneOS \
    "$TMPGEN" >/dev/null
MACSDK="$(xcode-select -p)/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"
SYSCFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -I $XNU/libsyscall/custom -I $XNU/libsyscall/mach -idirafter $XNU/osfmk -idirafter $MACSDK/usr/include"
for s in _fchownat _readlinkat _symlinkat; do
    "$CLANG" $SYSCFLAGS "$TMPGEN/${s}.s" -o "$OUT/pw_obj/sys${s}.o"
done
# _socketpair: syscalls.master marks this NO_SYSCALL_STUB (no public-name
# raw trap generated) -- real Apple ships a dedicated legacy wrapper
# instead (libsyscall/wrappers/legacy/socketpair.c, calls the raw
# `__socketpair` trap the generator DOES produce under that double-
# underscore name). Compile both: the raw trap, then the real wrapper.
"$CLANG" $SYSCFLAGS "$TMPGEN/___socketpair.s" -o "$OUT/pw_obj/sys___socketpair.o"
"$CLANG" $CFLAGS -I "$XNU/libsyscall/wrappers" "$XNU/libsyscall/wrappers/legacy/socketpair.c" -o "$OUT/pw_obj/socketpair.o"

# DYNAMIC LINK (DAR-314). pw runs after launchd/dyld are up (invoked
# interactively by root, never before), so it is dynamic like every
# non-pwtool binary in this project.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE)" >&2
    exit 1
fi
# libcopyfile.dylib/libremovefile.dylib listed explicitly (DAR-217, see
# build_pwd_mkdb.sh/build_chpass.sh's own identical citation).
LINK_DYLIBS="$OUT/system/libsystem_c.dylib $OUT/system/libsystem_kernel.dylib $OUT/system/libsystem_pthread.dylib $OUT/system/libsystem_malloc.dylib $OUT/system/libsystem_platform.dylib $OUT/system/libdyld.dylib $OUT/system/libcopyfile.dylib $OUT/system/libremovefile.dylib $OUT/system/libSystem.B.dylib"

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o pw.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT"/pw_obj/*.o \
    "$OUT/liblaunchd_syscalls.a" \
    "$OUT/libmach_ipc.a" \
    "$OUT/libc_regex.a" \
    "$OUT/libc_db.a" \
    $LINK_DYLIBS \
    -Wl,-alias,__err,_err \
    -Wl,-alias,__warn,_warn \
    -Wl,-alias,_waitpid,'_waitpid$UNIX2003' \
    -Wl,-alias,_setrlimit,'_setrlimit$UNIX2003' \
    -Wl,-alias,_fchmod,'_fchmod$UNIX2003' \
    -Wl,-alias,_stat,'_stat$INODE64' \
    -Wl,-alias,_sigaction,'_sigaction$UNIX2003' \
    -Wl,-alias,_sigprocmask,'_sigprocmask$UNIX2003' \
    -Wl,-alias,_freopen,'_freopen$UNIX2003' \
    -Wl,-alias,_pw_real_system,'_pw_real_system$UNIX2003' \
    -Wl,-alias,_wait4,__wait4
echo "wrote pw.macho"

# Real dynamic-executable assertions (DAR-314).
bad=0
otool -l pw.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: pw: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
otool -l pw.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: pw: no LC_LOAD_DYLIB" >&2; bad=1; }
otool -l pw.macho | grep -q "cmd LC_MAIN" || {
    echo "error: pw: no LC_MAIN" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1

if [ -x ./bind_target_audit.sh ]; then
    ./bind_target_audit.sh pw.macho
fi
if [ -x ./dead_stub_audit.sh ]; then
    ./dead_stub_audit.sh pw.macho
fi
