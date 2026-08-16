#!/bin/bash
# Builds Tier 1 of the "remaining commands" pass (docs/roadmap.md Phase 4
# step 3, "shell_cmds/file_cmds/text_cmds/adv_cmds/..."): the simplest,
# lowest-dependency real Apple commands, ordered deliberately by
# complexity (this file) before the heavier tiers that need real file
# I/O, passwd/group lookups, or environment plumbing (separate build
# scripts, one tier at a time, same pattern as bash/uname/ps).
#
# Every one of these is a single real, unmodified Apple/FreeBSD source
# file (third_party/shell_cmds/<name>/<name>.c) needing only what's
# already in libc_build/*.a from the bash/uname/ps tiers plus the same
# generic_main_wrapper.c TSD-bootstrap fix those three needed
# individually (see that file's own header for why this is mandatory,
# not optional -- skipping it is a real, reproducible SIGSEGV).
#
# CMD_LIST format: "name:srcdir:extra_source_files" (space-separated
# extra files, may be empty). Ordered simplest-first:
#   true/false   -- 2-line files, exit(0)/exit(1), no libc surface at all
#   yes          -- trivial printf loop
#   pwd          -- getcwd()-based, real err()/getopt()
#   basename/dirname -- call real Libc/gen/FreeBSD/{basename,dirname}.c
#     (already compiled into libc_gen.a by build_libc_gen.sh -- confirmed
#     via nm, not assumed) plus real err()/getopt()
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
XNU="../../third_party/xnu-7195"
PTHREAD="../../third_party/libpthread"
SHELL_CMDS="../../third_party/shell_cmds"
TEXT_CMDS="../../third_party/text_cmds"
FILE_CMDS="../../third_party/file_cmds"
OUT="libc_build"
mkdir -p "$OUT/simple_obj"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
for dep in libmalloc libplatform libclosure libpthread libc_gen libc_stdio libc_printf libc_locale libc_locale_for_stdio; do
    if [ ! -f "$OUT/$dep.a" ]; then
        echo "error: $OUT/$dep.a not found -- run build_$dep.sh first" >&2
        exit 1
    fi
done

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -I $LIBC/include -I $LIBC/locale -I $LIBC/locale/FreeBSD -I $LIBC/darwin -I $LIBC/gen \
    -I $LIBC/fbsdcompat \
    -I $LIBC/stdtime/FreeBSD \
    -I $OUT/compat_include \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall -I $XNU/libsyscall/wrappers \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I ../../third_party/removefile"

# Real err()/getopt() -- every tier-1 command needs at least one of the
# two; harmless to always link both (unused-but-linked, same tolerance
# this project's other tiers already take with e.g. libclosure/libmalloc
# stand-ins -- confirmed no duplicate-symbol conflicts, zero of these
# tier-1 sources define their own err/getopt).
COMMON_EXTRA="$LIBC/gen/FreeBSD/err.c $LIBC/stdlib/FreeBSD/getopt.c simple_cmds_read_compat.c"
for f in $COMMON_EXTRA; do
    base=$(basename "$f" .c)
    out="$OUT/simple_obj/common_${base}.o"
    if [ ! -f "$out" ]; then
        "$CLANG" $CFLAGS "$f" -o "$out"
    fi
done

# Real sched_yield()/swtch_pri Mach trap: libpthread.a's real
# contended-rwlock slow path (__pthread_rwlock_lock_slow) needs it --
# same real object every prior tier (uname/ps/etc) already links.
if [ ! -f "$OUT/simple_sched_yield_trap.o" ]; then
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
        libunwind_sched_yield.s -o "$OUT/simple_sched_yield_trap.o"
fi
if [ ! -f "$OUT/simple_sched_yield.o" ]; then
    "$CLANG" $CFLAGS libunwind_sched_yield.c -o "$OUT/simple_sched_yield.o"
fi

# Real raw syscall stubs this project has never generated before, all
# via the same Perl generator bash/ps/uname's own SYS_STUB_NAMES passes
# already use. One generation pass, multiple .s outputs picked out of it
# -- real Apple asm, not hand-written. Generated-symbol names confirmed
# by generating and reading the real .s output, not guessed (real
# double/triple-underscore C-symbol-mangling conventions vary per
# syscall depending on whether the public Libc wrapper calls a private
# name -- e.g. `___getlogin`/`___mmap`/`___munmap` vs plain `_kevent`/
# `_kqueue`, which have no private wrapper layer at all).
SYS_STUBS_NEEDED="___getlogin ___mmap ___munmap _kevent _kqueue _ftruncate _mkdir _utimes ___chmod _sigprocmask _fchmodat _fstatat64 _mkdirat _umask _access"
if [ ! -f "$OUT/simple_getlogin_trap.o" ]; then
    mkdir -p "$OUT/sys_asm_compat/mach/arm64"
    cat > "$OUT/sys_asm_compat/mach/arm64/asm.h" <<'EOF'
/* SYS.h expects the installed-SDK-style path mach/arm64/asm.h; real
 * content is osfmk/arm64/asm.h in this checkout -- same forwarding-shim
 * pattern build_bash.sh's own copy of this file already uses. */
#include <arm64/asm.h>
EOF
    SYS_TMPGEN="$(mktemp -d)"
    trap 'rm -rf "$SYS_TMPGEN"' EXIT
    ARCHS=arm64 perl "$XNU/libsyscall/xcodescripts/create-syscalls.pl" \
        "$XNU/bsd/kern/syscalls.master" \
        "$XNU/libsyscall/custom" \
        "$XNU/libsyscall/Platforms" \
        iPhoneOS \
        "$SYS_TMPGEN" >/dev/null
    for s in $SYS_STUBS_NEEDED; do
        "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -arch arm64 \
            -I"$SYS_TMPGEN" -I"$OUT/sys_asm_compat" -I"$XNU/libsyscall/custom" -I"$XNU/libsyscall/mach" -I"$XNU/osfmk" \
            "$SYS_TMPGEN/${s}.s" -o "$OUT/simple_obj/sys_${s}.o" 2>"$OUT/simple_obj/sys_${s}.err" \
            || { echo "error: $s raw syscall stub failed:" >&2; cat "$OUT/simple_obj/sys_${s}.err" >&2; exit 1; }
    done
    rm -rf "$SYS_TMPGEN"
    trap - EXIT
    touch "$OUT/simple_getlogin_trap.o" # sentinel: this whole block already ran
fi

LINK_COMMON="$OUT/libc_printf.a $OUT/libc_gen.a $OUT/libc_stdio.a $OUT/libc_locale_for_stdio.a $OUT/libc_gen.a $OUT/libc_stdio.a \
    $OUT/pthread_tsd_shim_libc_bootstrap.o $OUT/libpthread.a $OUT/simple_sched_yield_trap.o $OUT/simple_sched_yield.o \
    $OUT/simple_obj/sys____getlogin.o $OUT/simple_obj/sys____mmap.o $OUT/simple_obj/sys____munmap.o \
    $OUT/simple_obj/sys__kevent.o $OUT/simple_obj/sys__kqueue.o $OUT/simple_obj/sys__ftruncate.o \
    $OUT/simple_obj/sys__mkdir.o $OUT/simple_obj/sys__utimes.o $OUT/simple_obj/sys____chmod.o $OUT/simple_obj/sys__sigprocmask.o \
    $OUT/simple_obj/sys__fchmodat.o $OUT/simple_obj/sys__fstatat64.o $OUT/simple_obj/sys__mkdirat.o $OUT/simple_obj/sys__umask.o \
    $OUT/simple_obj/sys__access.o \
    $OUT/libclosure.a $OUT/libmalloc.a $OUT/cerror_stub.o $OUT/libplatform.a"
LINK_ALIASES="-Wl,-alias,_open,_open\$UNIX2003 \
    -Wl,-alias,_close,_close\$UNIX2003 \
    -Wl,-alias,_write,_write\$UNIX2003 \
    -Wl,-alias,___kill,_kill\$UNIX2003 \
    -Wl,-alias,___kill,_kill \
    -Wl,-alias,__mal_semwait_sleep,_sleep\$UNIX2003 \
    -Wl,-alias,___kdebug_trace,_kdebug_trace \
    -Wl,-alias,_fcntl,_fcntl\$UNIX2003 \
    -Wl,-alias,_fstat,_fstat\$INODE64 \
    -Wl,-alias,_lstat,_lstat\$INODE64 \
    -Wl,-alias,_stat,_stat\$INODE64 \
    -Wl,-alias,__err,_err \
    -Wl,-alias,__warn,_warn \
    -Wl,-alias,_fputs,_fputs\$UNIX2003 \
    -Wl,-alias,_mprotect,_mprotect\$UNIX2003 \
    -Wl,-alias,_pthread_rwlock_rdlock\$UNIX2003,_pthread_rwlock_rdlock \
    -Wl,-alias,_pthread_rwlock_unlock\$UNIX2003,_pthread_rwlock_unlock \
    -Wl,-alias,_getopt\$UNIX2003,_getopt \
    -Wl,-alias,_strerror,_strerror\$UNIX2003 \
    -Wl,-alias,_fwrite,_fwrite\$UNIX2003 \
    -Wl,-alias,_strtod,_strtod\$UNIX2003 \
    -Wl,-alias,_fdopen,_fdopen\$UNIX2003 \
    -Wl,-alias,_read,_read\$UNIX2003 \
    -Wl,-alias,_connect,_connect\$UNIX2003 \
    -Wl,-alias,_fstatfs,_fstatfs\$INODE64 \
    -Wl,-alias,_fopen,_fopen\$UNIX2003 \
    -Wl,-alias,__flockfile,_flockfile \
    -Wl,-alias,__funlockfile,_funlockfile \
    -Wl,-alias,_mmap,_mmap\$UNIX2003 \
    -Wl,-alias,_munmap,_munmap\$UNIX2003 \
    -Wl,-alias,_pthread_rwlock_rdlock\$UNIX2003,__pthread_rwlock_rdlock \
    -Wl,-alias,_pthread_rwlock_wrlock\$UNIX2003,__pthread_rwlock_wrlock \
    -Wl,-alias,_pthread_rwlock_unlock\$UNIX2003,__pthread_rwlock_unlock \
    -Wl,-alias,_fstatat64,_fstatat\$INODE64 \
    -Wl,-alias,_closedir,_closedir\$UNIX2003 \
    -Wl,-alias,_opendir,_opendir\$INODE64\$UNIX2003 \
    -Wl,-alias,_readdir,_readdir\$INODE64 \
    -Wl,-alias,_fsync,_fsync\$UNIX2003 \
    -Wl,-alias,_reallocarray,_reallocarray\$DARWIN_EXTSN"

# Bash array, one element per command: "name:srcfile:extra1|extra2|..."
# (srcfile is the FULL real source path -- not just relative to
# $SHELL_CMDS -- so this list can span multiple third_party/*_cmds
# top-level dirs, not just shell_cmds. extras are '|'-separated, not
# space-separated, so a multi-file extra list can't collide with the
# outer per-command split -- a real bug this script had at first: a
# space-separated top-level CMD_LIST string silently split a command's
# own multi-file "extra" field into bogus extra top-level entries,
# confirmed via a real, reproducible garbled-path link failure for
# hostname's two-file gethostname.c/sethostname.c extra list).
CMD_LIST=(
    "true:$SHELL_CMDS/true/true.c:"
    "false:$SHELL_CMDS/false/false.c:"
    "yes:$SHELL_CMDS/yes/yes.c:"
    "pwd:$SHELL_CMDS/pwd/pwd.c:"
    "basename:$SHELL_CMDS/basename/basename.c:"
    "dirname:$SHELL_CMDS/dirname/dirname.c:"
    "echo:$SHELL_CMDS/echo/echo.c:"
    "printf:$SHELL_CMDS/printf/printf.c:$LIBC/stdlib/FreeBSD/atoi.c"
    "hostname:$SHELL_CMDS/hostname/hostname.c:$LIBC/gen/FreeBSD/gethostname.c|$LIBC/gen/FreeBSD/sethostname.c"
    "logname:$SHELL_CMDS/logname/logname.c:$LIBC/gen/FreeBSD/getlogin.c"
    "cat:$TEXT_CMDS/cat/cat.c:cat_socket_stub.c"
    "wc:$TEXT_CMDS/wc/wc.c:"
    "head:$TEXT_CMDS/head/head.c:"
    "tail:$TEXT_CMDS/tail/tail.c:$TEXT_CMDS/tail/forward.c|$TEXT_CMDS/tail/reverse.c|$TEXT_CMDS/tail/misc.c|$TEXT_CMDS/tail/read.c|$LIBC/stdio/FreeBSD/_flock_stub.c|$LIBC/stdio/FreeBSD/freopen.c|$LIBC/stdlib/FreeBSD/strtoll.c|$LIBC/gen/FreeBSD/usleep.c|simple_cmds_nanosleep_compat.c|simple_cmds_mmap_compat.c"
    "mkdir:$FILE_CMDS/mkdir/mkdir.c:$XNU/libsyscall/wrappers/unix03/chmod.c|$LIBC/util/mkpath_np.c|$LIBC/gen/FreeBSD/setmode.c|simple_cmds_sigprocmask_compat.c"
    "touch:$FILE_CMDS/touch/touch.c:$XNU/libsyscall/wrappers/unix03/chmod.c|simple_cmds_time_compat.c|$LIBC/sys/gettimeofday.c|ps_commpage_gettimeofday_stub.c|$LIBC/stdtime/FreeBSD/localtime.c|$LIBC/stdtime/timezone_unix03.c|$LIBC/stdtime/FreeBSD/asctime.c|$LIBC/stdtime/FreeBSD/strftime.c|$LIBC/stdtime/FreeBSD/timelocal.c|simple_cmds_lutimes_compat.c"
    "rm:$FILE_CMDS/rm/rm.c:$LIBC/gen/get_compat.c|ps_passwd_compat.c|../libinfo_custom/libinfo_custom.c|simple_cmds_rpmatch_compat.c|simple_cmds_undelete_stub.c|$LIBC/gen/strtofflags.c"
)

built=0
failed=0
for spec in "${CMD_LIST[@]}"; do
    name="${spec%%:*}"
    rest="${spec#*:}"
    src="${rest%%:*}"
    extra_raw="${rest#*:}"
    extra="${extra_raw//|/ }"

    real_main="${name}_cmd_real_main"
    obj="$OUT/simple_obj/${name}.o"
    wrapper_obj="$OUT/simple_obj/${name}_wrapper.o"

    if [ ! -f "$src" ]; then
        echo "error: $src not found" >&2
        failed=$((failed + 1))
        continue
    fi

    if ! "$CLANG" $CFLAGS "-Dmain=$real_main" "$src" -o "$obj" 2>"$OUT/simple_obj/${name}.err"; then
        echo "  skip: $name (real source)"
        cat "$OUT/simple_obj/${name}.err" >&2
        failed=$((failed + 1))
        continue
    fi
    if ! "$CLANG" $CFLAGS "-DREAL_MAIN=$real_main" "-DCMD_NAME=\"$name\"" generic_main_wrapper.c -o "$wrapper_obj" 2>"$OUT/simple_obj/${name}_wrapper.err"; then
        echo "  skip: $name (wrapper)"
        cat "$OUT/simple_obj/${name}_wrapper.err" >&2
        failed=$((failed + 1))
        continue
    fi

    extra_objs=""
    for ef in $extra; do
        ef_base=$(basename "$ef" .c)
        ef_out="$OUT/simple_obj/${name}_${ef_base}.o"
        "$CLANG" $CFLAGS "$ef" -o "$ef_out"
        extra_objs="$extra_objs $ef_out"
    done

    # Per-command extra whole archives (e.g. rm needs all of
    # libremovefile.a's fts_*/removefile symbols, not one function at a
    # time) -- keyed by name here rather than a 5th CMD_LIST field, since
    # this is rare (one command so far).
    extra_libs=""
    common_read_compat="$OUT/simple_obj/common_simple_cmds_read_compat.o"
    case "$name" in
        rm)
            # libremovefile.a's own removefile_syscalls.o already defines
            # read()/read$UNIX2003 -- a real, genuine duplicate-symbol
            # conflict against the shared compat object every other
            # command uses, confirmed via the real link error, not
            # guessed preemptively. Use its copy instead.
            extra_libs="$OUT/libremovefile.a"
            common_read_compat=""
            ;;
    esac

    if ! "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
        -o "${name}.macho" \
        ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
        "$wrapper_obj" "$obj" $extra_objs \
        "$OUT/simple_obj/common_err.o" "$OUT/simple_obj/common_getopt.o" $common_read_compat \
        $extra_libs \
        $LINK_COMMON $LINK_ALIASES 2>"$OUT/simple_obj/${name}_link.err"; then
        echo "  skip: $name (link)"
        cat "$OUT/simple_obj/${name}_link.err" >&2
        failed=$((failed + 1))
        continue
    fi

    bad=0
    if otool -l "${name}.macho" | grep -q LC_LOAD_DYLIB; then
        echo "  FAIL: $name -- LC_LOAD_DYLIB present, not dyld-free" >&2
        bad=1
    fi
    if ! otool -l "${name}.macho" | grep -q LC_UNIXTHREAD; then
        echo "  FAIL: $name -- no LC_UNIXTHREAD" >&2
        bad=1
    fi
    if nm -u "${name}.macho" 2>/dev/null | grep -q .; then
        echo "  FAIL: $name -- undefined symbols:" >&2
        nm -u "${name}.macho" >&2
        bad=1
    fi
    if [ "$bad" -ne 0 ]; then
        failed=$((failed + 1))
        continue
    fi

    echo "  ok: $name.macho ($(wc -c < "${name}.macho") bytes)"
    built=$((built + 1))
done

echo "build_simple_cmds: $built built, $failed failed"
[ "$failed" -eq 0 ]
