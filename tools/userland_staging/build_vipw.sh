#!/bin/bash
# Builds vipw.macho: real, unmodified Apple system_cmds/vipw.tproj/
# vipw.c + pw_util.c (DAR-174, Phase 5 account-provisioning tools). Same
# Csu-dynamic-link, -Dmain=..., TSD-bootstrap-wrapper shape as
# build_getty.sh/build_login.sh (read build_getty.sh's own header before
# touching this one).
#
# real vipw(8) flow (vipw.c's own real main(), read directly, not
# assumed): pw_init() (resource limits + signal masking) -> pw_lock()
# (O_RDONLY open + flock(LOCK_EX|LOCK_NB) on /etc/master.passwd) ->
# pw_tmp() (mkstemp a pw.XXXXXX scratch copy) -> copyfile() the real
# master.passwd into it -> pw_edit() (vfork()+execlp($EDITOR or vi) on
# the scratch file, waits for a clean exit) -> pw_mkdb() (vfork()+
# execl(_PATH_PWD_MKDB, "pwd_mkdb", "-p", tempname) to rebuild /etc/
# pwd.db + /etc/spwd.db, THEN rename() the scratch file over the real
# master.passwd -- see real pwd_mkdb.tproj/pwd_mkdb.c's own -p/"prevail"
# semantics, not this script's business).
#
# REAL, NAMED DEPENDENCY on pwd_mkdb.macho (DAR-174's OTHER real,
# currently-unmet blocker): pw_mkdb() execs a SEPARATE pwd_mkdb binary
# rather than linking Berkeley DB (db.h/dbopen) directly into vipw
# itself -- confirmed by reading pw_util.c's own pw_mkdb() body, not
# assumed. This project has never ported Berkeley DB (third_party/Libc/
# db/{db,hash,btree,recno,mpool}/FreeBSD -- ~20 real, unmodified Apple
# files, no build_libc_db.sh exists yet, confirmed via `ls
# tools/userland_staging/build_*.sh`), so pwd_mkdb.tproj itself is NOT
# built by this pass (see this ticket's own Linear comment for the full
# writeup). Net real, honest effect on vipw.macho as built here: it
# compiles and links cleanly (pw_mkdb() has zero build-time dependency on
# libdb), but at RUNTIME execl(_PATH_PWD_MKDB, ...) will fail (ENOENT,
# pwd_mkdb.macho absent from the image) -- pw_mkdb() then correctly
# returns 1 (real vipw.c: `if (!WIFEXITED(pstat) || WEXITSTATUS(pstat) !=
# 0) return (1);`), vipw.c's own real main() loop calls pw_prompt() and
# offers to re-edit rather than silently corrupting the password
# database -- graceful real degradation, not a crash, same shape as
# build_getty.sh's own /etc/gettytab-missing finding.
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
VIPW="../../third_party/system_cmds/vipw.tproj"
OUT="libc_build"
rm -rf "$OUT/vipw_obj"
mkdir -p "$OUT/vipw_obj" "$OUT/compat_include"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

for dep in libplatform.a libmalloc.a libpthread.a libclosure.a libc_gen.a libc_stdio.a libc_printf.a \
           libc_locale_for_stdio.a liblaunchd_syscalls.a libmach_ipc.a; do
    if [ ! -f "$OUT/$dep" ]; then
        echo "error: $OUT/$dep not found -- run its build_*.sh first" >&2
        exit 1
    fi
done
if [ ! -f "$OUT/liblaunchd_syscalls.a" ]; then
    echo "error: $OUT/liblaunchd_syscalls.a not found -- run build_launchd_syscalls.sh first" >&2
    exit 1
fi
for f in simple_obj/sys__umask.o; do
    if [ ! -f "$OUT/$f" ]; then
        echo "error: $OUT/$f not found -- run build_simple_cmds.sh first (shared raw syscall stub)" >&2
        exit 1
    fi
done

# Same real flags as build_getty.sh/build_login.sh (see those scripts'
# own CFLAGS comment) -- same tier, same gen/locale/stdtime -I set, plus
# -I $VIPW for vipw's own pw_util.h/pathnames.h.
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h -include $OUT/compat_include/stdio_flockfile_compat.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I $VIPW \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD -I $LIBC/gen \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall -I $XNU/libsyscall/wrappers \
    -I ../../generated"

# Real, unmodified vipw.tproj source (both files that dir has). vipw.c's
# own main() renamed out of the way (-Dmain, see vipw_main_wrapper.c's
# header) same as every other tier in this project's TSD-bootstrap
# family.
#
# real Libc/gen/FreeBSD/err.c + stdlib/FreeBSD/getopt.c: standard tier-1
# surface, already-proven reused verbatim.
# real Libc/gen/FreeBSD/signal.c: vipw's own real signal(SIGALRM,
# SIG_IGN) etc calls in pw_init().
# real Libc/gen/FreeBSD/exec.c: pw_util.c's own real execlp()
# (pw_edit()) -- execl/execle/execlp are Libc-level variadic wrappers
# over execve, not raw traps.
# real Libc/gen/raise.c: pw_cont()'s real kill(editpid, sig) -- no,
# actually pw_util.c calls kill() directly (dylib-exported, see below)
# and raise() is NOT called by vipw/pw_util at all; kept out unless a
# real link failure says otherwise (see EXTRA_FILES note below).
# cp_mv_syscalls.c: real, already-proven honest vfork()==fork() alias
# (see that file's own header -- real Apple vfork.s is hand-written
# assembly this project has never ported; every existing consumer,
# e.g. launchctl_syscall_gaps.c's own identical documented choice,
# reuses this exact real justification, not reinvented here) -- pw_tmp()
# doesn't vfork, but pw_edit()/pw_mkdb() both do.
EXTRA_FILES="
$LIBC/gen/FreeBSD/err.c
$LIBC/stdlib/FreeBSD/getopt.c
$LIBC/gen/FreeBSD/signal.c
$LIBC/gen/FreeBSD/exec.c
simple_cmds_execve_compat.c
cp_mv_syscalls.c
simple_cmds_read_compat.c
simple_cmds_getenv_compat.c
simple_cmds_mmap_compat.c
"

ok=0
skip=0
for f in "$VIPW/vipw.c" "$VIPW/pw_util.c"; do
    base=$(basename "$f" .c)
    out="$OUT/vipw_obj/${base}.o"
    extra_def=""
    if [ "$base" = "vipw" ]; then
        extra_def="-Dmain=vipw_cmd_real_main"
    fi
    if "$CLANG" $CFLAGS $extra_def "$f" -o "$out" 2>"$OUT/vipw_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/vipw_obj/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
        cat "$OUT/vipw_obj/${base}.err" >&2
    fi
done
if "$CLANG" $CFLAGS vipw_main_wrapper.c -o "$OUT/vipw_obj/vipw_main_wrapper.o" 2>"$OUT/vipw_obj/vipw_main_wrapper.err"; then
    ok=$((ok + 1))
    rm -f "$OUT/vipw_obj/vipw_main_wrapper.err"
else
    skip=$((skip + 1))
    echo "  skip: vipw_main_wrapper"
    cat "$OUT/vipw_obj/vipw_main_wrapper.err" >&2
fi
for f in $EXTRA_FILES; do
    base=$(basename "$f" .c)
    out="$OUT/vipw_obj/${base}.o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>"$OUT/vipw_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/vipw_obj/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
        cat "$OUT/vipw_obj/${base}.err" >&2
    fi
done

echo "vipw (real Apple system_cmds/vipw.tproj): $ok compiled, $skip skipped"
if [ "$skip" -ne 0 ]; then
    echo "error: vipw has real compile failures, not building vipw.macho" >&2
    exit 1
fi

# Real sched_yield trap + C wrapper -- same real files every richer tier
# already links (libpthread's contended-rwlock slow path genuinely calls
# this).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    libunwind_sched_yield.s -o "$OUT/vipw_obj/vipw_sched_yield_trap.o"
"$CLANG" $CFLAGS libunwind_sched_yield.c -o "$OUT/vipw_obj/vipw_sched_yield.o"
# DAR-314: locale globals are LOCAL inside libsystem_c.dylib, not
# exports (see build_getty.sh/build_login.sh's own identical comment).
"$CLANG" $CFLAGS libc_locale_globals_compat.c -o "$OUT/vipw_obj/libc_locale_globals_compat.o"
"$CLANG" $CFLAGS "$LIBC/locale/FreeBSD/table.c" -o "$OUT/vipw_obj/table.o"
"$CLANG" $CFLAGS "$LIBC/locale/FreeBSD/none.c" -o "$OUT/vipw_obj/none.o"

# ___gdtoa_locks (COMMON/tentative symbol, glue.o): same real gap already
# fixed in build_cron.sh/build_getty.sh/build_login.sh/etc -- `ar x` it
# directly (never -force_load).
mkdir -p "$OUT/vipw_glue_extract"
(cd "$OUT/vipw_glue_extract" && ar x "../libc_printf.a" glue.o)

# DYNAMIC LINK (DAR-314). vipw runs after launchd/dyld are up (invoked
# interactively by a logged-in root shell, never before), so it is
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
    -o vipw.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT"/vipw_obj/*.o \
    `# raise()/waitpid() have no dylib equivalent (bind_target_audit.sh` \
    `# would flag UNRESOLVABLE if dropped -- real Apple objects, already` \
    `# built for launchd, reused verbatim here, same DAR-217-avoidance` \
    `# shape as build_login.sh's own sigaction extraction).` \
    "$OUT/liblaunchd_syscalls.a" \
    "$OUT/libmach_ipc.a" \
    $LINK_DYLIBS \
    `# Aliases that survive: each targets a LOCAL definition or a` \
    `# versioned dylib export whose bare name is never exported.` \
    -Wl,-alias,__err,_err \
    -Wl,-alias,__warn,_warn \
    -Wl,-alias,_waitpid,'_waitpid$UNIX2003' \
    -Wl,-alias,_setrlimit,'_setrlimit$UNIX2003'
echo "wrote vipw.macho"

# Real dynamic-executable assertions (DAR-314).
bad=0
otool -l vipw.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: vipw: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
otool -l vipw.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: vipw: no LC_LOAD_DYLIB" >&2; bad=1; }
otool -l vipw.macho | grep -q "cmd LC_MAIN" || {
    echo "error: vipw: no LC_MAIN" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1

if [ -x ./dead_stub_audit.sh ]; then
    ./dead_stub_audit.sh vipw.macho
fi
