#!/bin/bash
# Builds pwd_mkdb.macho: real, unmodified Apple system_cmds/
# pwd_mkdb.tproj/pwd_mkdb.c + pw_scan.c (DAR-174, Phase 5 account-
# provisioning tools -- 3 of 4; vipw/passwd already built, see commit
# c920b26). Same Csu-dynamic-link, -Dmain=..., TSD-bootstrap-wrapper
# shape as build_vipw.sh/build_passwd.sh (read build_getty.sh's own
# header before touching this one).
#
# THE REAL BLOCKER THIS SCRIPT CLOSES: pwd_mkdb.c calls real dbopen()
# with DB_HASH four times (see build_libc_db.sh's own header for the
# real citation) to write /etc/pwd.db and /etc/spwd.db -- this project
# had never ported Berkeley DB until build_libc_db.sh (this same DAR-174
# pass). With libc_build/libc_db.a now real and built, pwd_mkdb links
# clean.
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
PWDMKDB="../../third_party/system_cmds/pwd_mkdb.tproj"
OUT="libc_build"
./make_no_fork_archive.sh || exit 1  # DAR-292: fork/wait/exec family binds libsystem_c
rm -rf "$OUT/pwd_mkdb_obj"
mkdir -p "$OUT/pwd_mkdb_obj" "$OUT/compat_include"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

for dep in libplatform.a libmalloc.a libpthread.a libclosure.a libc_gen.a libc_stdio.a libc_printf.a \
           libc_locale_for_stdio.a liblaunchd_syscalls.a libmach_ipc.a libc_regex.a libc_db.a; do
    if [ ! -f "$OUT/$dep" ]; then
        echo "error: $OUT/$dep not found -- run its build_*.sh first (libc_db.a: run build_libc_db.sh)" >&2
        exit 1
    fi
done

# Same real flags as build_vipw.sh/build_passwd.sh -- same tier, same
# gen/locale/stdtime -I set, plus -I $PWDMKDB for pwd_mkdb's own
# pw_scan.h, and -I $LIBC/db/hash/FreeBSD not needed here (pwd_mkdb.c
# only ever calls the public dbopen()/DB_HASH API via <db.h>, never
# reaches into hash-internal headers).
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h -include $OUT/compat_include/stdio_flockfile_compat.h \
    -include pwd_mkdb_openbsd_pwd_compat.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I $PWDMKDB \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD -I $LIBC/gen \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall -I $XNU/libsyscall/wrappers \
    -I ../libinfo_custom \
    -I ../../generated"

# Real, unmodified pwd_mkdb.tproj source -- pwd_mkdb.c + pw_scan.c (both
# real files that dir has).
#
# real Libc/gen/FreeBSD/err.c + stdlib/FreeBSD/getopt.c: standard tier-1
# surface, already-proven reused verbatim (build_vipw.sh/build_passwd.sh).
# real Libc/gen/FreeBSD/signal.c: unused directly by pwd_mkdb.c (it calls
# sigprocmask()/sigemptyset()/sigaddset(), not signal()) but harmless if
# linked, matching build_vipw.sh's own "kept for consistency" precedent.
# Libc/gen/sigsetops.c: real sigemptyset()/sigaddset() -- pwd_mkdb.c's
# own real main() blocks SIGTSTP/SIGHUP/SIGINT/SIGQUIT/SIGTERM via
# exactly these two calls before writing the database (see its own
# source, ~line 161). Not needed by vipw/passwd (they don't call these),
# a genuinely new real gap for this tier.
# cp_mv_syscalls.c: real, already-proven honest vfork()==fork() alias
# (see build_vipw.sh's own citation) -- pwd_mkdb.c's own cp()/mv()
# helpers use real fork()/vfork()? no: they use plain read/write copy +
# rename(), no fork at all -- pulled in for EXTRA_FILES-list consistency
# only, harmless if unreferenced (same as build_passwd.sh's own note).
# ps_passwd_compat.c + ../libinfo_custom/libinfo_custom.c: real
# getpwnam()/getpwuid()/getgrnam() -- pwd_mkdb.c's own real
# `getgrnam(SHADOW_GROUP)` call (looks up the "wheel" GID to chgrp
# /etc/spwd.db), same flat-file resolver already proven for passwd.tproj
# (build_passwd.sh's own identical real gap/fix).
# Libc/gen/getusershell.c: real getusershell()/setusershell()/
# endusershell() -- pw_scan.c's own real pw_scan() calls getusershell()
# to validate an entry's shell field against /etc/shells (see pw_scan.c
# directly), a genuinely new real gap for this tier (vipw/passwd never
# call pw_scan() themselves).
EXTRA_FILES="
$LIBC/gen/FreeBSD/err.c
$LIBC/stdlib/FreeBSD/getopt.c
$LIBC/gen/sigsetops.c
$LIBC/gen/getusershell.c
simple_cmds_read_compat.c
simple_cmds_getenv_compat.c
simple_cmds_mmap_compat.c
cp_mv_syscalls.c
ps_passwd_compat.c
../libinfo_custom/libinfo_custom.c
"

ok=0
skip=0
for f in "$PWDMKDB/pwd_mkdb.c" "$PWDMKDB/pw_scan.c"; do
    base=$(basename "$f" .c)
    out="$OUT/pwd_mkdb_obj/${base}.o"
    extra_def=""
    if [ "$base" = "pwd_mkdb" ]; then
        extra_def="-Dmain=pwd_mkdb_cmd_real_main"
    fi
    if "$CLANG" $CFLAGS $extra_def "$f" -o "$out" 2>"$OUT/pwd_mkdb_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/pwd_mkdb_obj/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
        cat "$OUT/pwd_mkdb_obj/${base}.err" >&2
    fi
done
if "$CLANG" $CFLAGS pwd_mkdb_main_wrapper.c -o "$OUT/pwd_mkdb_obj/pwd_mkdb_main_wrapper.o" 2>"$OUT/pwd_mkdb_obj/pwd_mkdb_main_wrapper.err"; then
    ok=$((ok + 1))
    rm -f "$OUT/pwd_mkdb_obj/pwd_mkdb_main_wrapper.err"
else
    skip=$((skip + 1))
    echo "  skip: pwd_mkdb_main_wrapper"
    cat "$OUT/pwd_mkdb_obj/pwd_mkdb_main_wrapper.err" >&2
fi
for f in $EXTRA_FILES; do
    base=$(basename "$f" .c)
    out="$OUT/pwd_mkdb_obj/${base}.o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>"$OUT/pwd_mkdb_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/pwd_mkdb_obj/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
        cat "$OUT/pwd_mkdb_obj/${base}.err" >&2
    fi
done

echo "pwd_mkdb (real Apple system_cmds/pwd_mkdb.tproj): $ok compiled, $skip skipped"
if [ "$skip" -ne 0 ]; then
    echo "error: pwd_mkdb has real compile failures, not building pwd_mkdb.macho" >&2
    exit 1
fi

# Real sched_yield trap + C wrapper -- same real files every richer tier
# already links.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    libunwind_sched_yield.s -o "$OUT/pwd_mkdb_obj/pwd_mkdb_sched_yield_trap.o"
"$CLANG" $CFLAGS libunwind_sched_yield.c -o "$OUT/pwd_mkdb_obj/pwd_mkdb_sched_yield.o"
# DAR-314: locale globals are LOCAL inside libsystem_c.dylib, not exports.
"$CLANG" $CFLAGS libc_locale_globals_compat.c -o "$OUT/pwd_mkdb_obj/libc_locale_globals_compat.o"
"$CLANG" $CFLAGS "$LIBC/locale/FreeBSD/table.c" -o "$OUT/pwd_mkdb_obj/table.o"
"$CLANG" $CFLAGS "$LIBC/locale/FreeBSD/none.c" -o "$OUT/pwd_mkdb_obj/none.o"

# ___gdtoa_locks (COMMON/tentative symbol, glue.o): same real gap already
# fixed in build_cron.sh/build_getty.sh/build_login.sh/build_vipw.sh/
# build_passwd.sh -- `ar x` it directly (never -force_load).
mkdir -p "$OUT/pwd_mkdb_glue_extract"
(cd "$OUT/pwd_mkdb_glue_extract" && ar x "../libc_printf.a" glue.o)

# DYNAMIC LINK (DAR-314). pwd_mkdb runs after launchd/dyld are up
# (invoked by vipw/chpass/passwd's own pw_mkdb() helper, or by hand from
# a logged-in root shell -- never before dyld), so it is dynamic like
# every non-pwtool binary in this project.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE)" >&2
    exit 1
fi
# libcopyfile.dylib/libremovefile.dylib listed explicitly (DAR-217):
# real fchmod()/fchown()/rename() live there in this project's current
# dylib layout (confirmed via `nm -m` -- absent from libsystem_c.dylib/
# libsystem_kernel.dylib/libSystem.B.dylib's own export trie, a known
# flattened-umbrella architecture gap, see CLAUDE.md's "Static-vs-dylib
# architecture debt"), NOT libsystem_kernel where real Apple would put
# them. Listing only libSystem.B.dylib and letting ld64 resolve through
# ITS transitive plain-link closure would record the bind against
# libSystem -- exactly the DAR-217 hazard (a clean link that aborts at
# real dyld load, since dyld-940 only searches an image's own re-export
# closure, not arbitrary transitively-linked images).
# Real prerequisite check (2026-09-21) -- see build_ifconfig.sh's
# identically-structured block for why this is needed: without it, a
# from-scratch build hits a bare clang "no such file" pile instead of one
# clear message, and gen_build_order.py never learns this is a real
# dependency.
for d in libsystem_c libsystem_kernel libsystem_pthread libsystem_malloc libsystem_platform; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2
        exit 1
    fi
done
if [ ! -f "$OUT/system/libdyld.dylib" ]; then
    echo "error: $OUT/system/libdyld.dylib not found -- run build_libdyld.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libcopyfile.dylib" ]; then
    echo "error: $OUT/system/libcopyfile.dylib not found -- run build_copyfile_dylib.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libremovefile.dylib" ]; then
    echo "error: $OUT/system/libremovefile.dylib not found -- run build_removefile_dylib.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libSystem.B.dylib" ]; then
    echo "error: $OUT/system/libSystem.B.dylib not found -- run build_libsystem.sh first" >&2
    exit 1
fi
LINK_DYLIBS="$OUT/system/libsystem_c.dylib $OUT/system/libsystem_kernel.dylib $OUT/system/libsystem_pthread.dylib $OUT/system/libsystem_malloc.dylib $OUT/system/libsystem_platform.dylib $OUT/system/libdyld.dylib $OUT/system/libcopyfile.dylib $OUT/system/libremovefile.dylib $OUT/system/libSystem.B.dylib"

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o pwd_mkdb.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT"/pwd_mkdb_obj/*.o \
    "$OUT/liblaunchd_syscalls_no_fork.a" \
    "$OUT/libmach_ipc.a" \
    "$OUT/libc_regex.a" \
    "$OUT/libc_db.a" \
    $LINK_DYLIBS \
    -Wl,-alias,__err,_err \
    -Wl,-alias,__warn,_warn \
    -Wl,-alias,_setrlimit,'_setrlimit$UNIX2003' \
    -Wl,-alias,_fchmod,'_fchmod$UNIX2003' \
    -Wl,-alias,_pread,'_pread$UNIX2003' \
    -Wl,-alias,_pwrite,'_pwrite$UNIX2003' \
    -Wl,-alias,_stat,'_stat$INODE64'
echo "wrote pwd_mkdb.macho"

# Real dynamic-executable assertions (DAR-314).
bad=0
otool -l pwd_mkdb.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: pwd_mkdb: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
otool -l pwd_mkdb.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: pwd_mkdb: no LC_LOAD_DYLIB" >&2; bad=1; }
otool -l pwd_mkdb.macho | grep -q "cmd LC_MAIN" || {
    echo "error: pwd_mkdb: no LC_MAIN" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1

if [ -x ./bind_target_audit.sh ]; then
    ./bind_target_audit.sh pwd_mkdb.macho
fi
if [ -x ./dead_stub_audit.sh ]; then
    ./dead_stub_audit.sh pwd_mkdb.macho
fi
