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
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall \
    -I $PTHREAD/include -I $PTHREAD/private"

# Real err()/getopt() -- every tier-1 command needs at least one of the
# two; harmless to always link both (unused-but-linked, same tolerance
# this project's other tiers already take with e.g. libclosure/libmalloc
# stand-ins -- confirmed no duplicate-symbol conflicts, zero of these
# tier-1 sources define their own err/getopt).
COMMON_EXTRA="$LIBC/gen/FreeBSD/err.c $LIBC/stdlib/FreeBSD/getopt.c"
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

LINK_COMMON="$OUT/libc_printf.a $OUT/libc_gen.a $OUT/libc_stdio.a $OUT/libc_locale_for_stdio.a $OUT/libc_gen.a $OUT/libc_stdio.a \
    $OUT/pthread_tsd_shim_libc_bootstrap.o $OUT/libpthread.a $OUT/simple_sched_yield_trap.o $OUT/simple_sched_yield.o \
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
    -Wl,-alias,_fputs,_fputs\$UNIX2003 \
    -Wl,-alias,_mprotect,_mprotect\$UNIX2003 \
    -Wl,-alias,_pthread_rwlock_rdlock\$UNIX2003,_pthread_rwlock_rdlock \
    -Wl,-alias,_pthread_rwlock_unlock\$UNIX2003,_pthread_rwlock_unlock \
    -Wl,-alias,_getopt\$UNIX2003,_getopt \
    -Wl,-alias,_strerror,_strerror\$UNIX2003 \
    -Wl,-alias,_fwrite,_fwrite\$UNIX2003 \
    -Wl,-alias,_strtod,_strtod\$UNIX2003"

CMD_LIST="true:true: false:false: yes:yes: pwd:pwd: basename:basename: dirname:dirname: echo:echo: printf:printf:$LIBC/stdlib/FreeBSD/atoi.c"

built=0
failed=0
for spec in $CMD_LIST; do
    name="${spec%%:*}"
    rest="${spec#*:}"
    srcdir="${rest%%:*}"
    extra="${rest#*:}"

    real_main="${name}_cmd_real_main"
    obj="$OUT/simple_obj/${name}.o"
    wrapper_obj="$OUT/simple_obj/${name}_wrapper.o"

    src="$SHELL_CMDS/$srcdir/$name.c"
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

    if ! "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
        -o "${name}.macho" \
        ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
        "$wrapper_obj" "$obj" $extra_objs \
        "$OUT/simple_obj/common_err.o" "$OUT/simple_obj/common_getopt.o" \
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
