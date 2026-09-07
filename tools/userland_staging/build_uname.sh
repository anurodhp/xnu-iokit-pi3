#!/bin/bash
# Builds uname.macho: real, unmodified Apple `uname` (third_party/shell_cmds/
# uname/uname.c) as a static Mach-O against this project's curated
# libc_build/*.a stack -- the same Csu-static pattern every prior tier uses
# (this project has no dyld; see architecture.md). This is Phase 4 step 3's
# first "real command" (docs/roadmap.md) -- previously only `bash` itself
# had been attempted from that step.
#
# Real uname() itself needs NO new work: third_party/Libc/gen/uname.c (pure
# sysctl(CTL_KERN/KERN_OSTYPE|KERN_HOSTNAME|KERN_OSRELEASE|KERN_VERSION,
# CTL_HW/HW_MACHINE)) was already compiled into libc_build/libc_gen.a by
# build_libc_gen.sh -- confirmed by grep, not assumed. What's missing is
# just the command wrapper (getopt parsing + printing) plus two real
# dependencies it drags in that were never compiled anywhere yet:
#
#   - get_compat.h/.c (third_party/Libc/gen/get_compat.{h,c}): real,
#     self-contained Apple Libc source, NOT autoconf-generated (an earlier
#     pass's build_bash_survey.sh guessed wrong about this before
#     build_bash.sh corrected it -- see that script's own header comment).
#     uname.c's `-a` flag calls COMPAT_MODE("bin/uname","Unix2003") to
#     decide whether to also print PRINT_MACHINE_ARCH. compat_mode()'s
#     first call does a real pthread_once() -- notably the SAME function
#     implicated in the still-open real bash stack-corruption bug (see
#     docs/roadmap.md Phase 4 "Eighteenth pass") -- but at trivially
#     shallow stack depth (top of uname's main(), no parser recursion),
#     making this a useful independent data point for that investigation:
#     if uname -a also corrupts, the bug isn't bash-recursion-depth
#     specific; if it doesn't, depth genuinely matters. Report this
#     either way, don't just note it and move on.
#   - getopt(3) (third_party/Libc/stdlib/FreeBSD/getopt.c): real FreeBSD
#     source (Apple's own Libc checkout has no portable getopt.c outside
#     this FreeBSD-sourced one, same class of gap as build_libc_string.sh's
#     memcpy/memset -- confirmed by grep before falling back, per this
#     project's standing Apple-first/FreeBSD-fallback rule), never
#     compiled into any prior tier.
set -e
cd "$(dirname "$0")"

UNAME_SRC="../../third_party/shell_cmds/uname"
LIBC="../../third_party/Libc"
XNU="../../third_party/xnu-7195"
PTHREAD="../../third_party/libpthread"
OUT="libc_build"
mkdir -p "$OUT/uname" "$OUT/compat_include"

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

UNAME_CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -I $LIBC/include -I $LIBC/locale -I $LIBC/locale/FreeBSD -I $LIBC/darwin -I $LIBC/gen \
    -I $LIBC/fbsdcompat \
    -I $LIBC/stdtime/FreeBSD \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $UNAME_SRC"

ok=0
skip=0
for spec in "$UNAME_SRC/uname.c:-Dmain=uname_cmd_real_main" "$LIBC/gen/get_compat.c:" "$LIBC/stdlib/FreeBSD/getopt.c:" "uname_main_wrapper.c:"; do
    f="${spec%%:*}"
    extra="${spec#*:}"
    base=$(basename "$f" .c)
    out="$OUT/uname/${base}.o"
    if "$CLANG" $UNAME_CFLAGS $extra "$f" -o "$out" 2>"$OUT/uname/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/uname/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
        cat "$OUT/uname/${base}.err" >&2
    fi
done
echo "uname: $ok compiled, $skip skipped"
if [ "$skip" -ne 0 ]; then
    echo "error: uname has real compile failures, not a clean tier" >&2
    exit 1
fi

# Real sched_yield()/swtch_pri Mach trap, needed by libpthread.a's real
# contended-rwlock slow path (__pthread_rwlock_lock_slow) -- same real
# object build_libunwind.sh introduced and build_removefile.sh/
# build_copyfile.sh already reuse, not hand-rolled again here.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    libunwind_sched_yield.s -o "$OUT/uname_sched_yield_trap.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libunwind_sched_yield.c -o "$OUT/uname_sched_yield.o"

# Same TSD-bootstrap-before-any-real-libc-call requirement every prior
# tier needs (compat_mode()'s pthread_once, setlocale(), etc all read
# TSD) -- pthread_tsd_shim_libc_bootstrap.o is the real, already-verified
# bootstrap call every prior smoketest also links in.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -o uname.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT/uname/uname_main_wrapper.o" "$OUT/uname/uname.o" "$OUT/uname/get_compat.o" "$OUT/uname/getopt.o" \
    "$OUT/libc_printf.a" \
    "$OUT/libc_gen.a" "$OUT/libc_stdio.a" "$OUT/libc_locale_for_stdio.a" "$OUT/libc_gen.a" "$OUT/libc_stdio.a" \
    "$OUT/pthread_tsd_shim_libc_bootstrap.o" \
    "$OUT/libpthread.a" "$OUT/uname_sched_yield_trap.o" "$OUT/uname_sched_yield.o" \
    "$OUT/libclosure.a" \
    "$OUT/libmalloc.a" "$OUT/cerror_stub.o" "$OUT/libplatform.a" \
    -Wl,-alias,_open,'_open$UNIX2003' \
    -Wl,-alias,_close,'_close$UNIX2003' \
    -Wl,-alias,_write,'_write$UNIX2003' \
    -Wl,-alias,___kill,'_kill$UNIX2003' \
    -Wl,-alias,___kill,_kill \
    -Wl,-alias,__mal_semwait_sleep,'_sleep$UNIX2003' \
    -Wl,-alias,___kdebug_trace,_kdebug_trace \
    -Wl,-alias,_fcntl,'_fcntl$UNIX2003' \
    -Wl,-alias,_fstat,'_fstat$INODE64' \
    -Wl,-alias,_lstat,'_lstat$INODE64' \
    -Wl,-alias,_stat,'_stat$INODE64' \
    -Wl,-alias,__err,_err \
    -Wl,-alias,_fputs,'_fputs$UNIX2003' \
    -Wl,-alias,_mprotect,'_mprotect$UNIX2003' \
    -Wl,-alias,'_mmap$UNIX2003',_mmap \
    -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003',_pthread_rwlock_rdlock \
    -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003',_pthread_rwlock_unlock
echo "wrote uname.macho"

otool -l uname.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l uname.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
if nm -u uname.macho | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u uname.macho >&2
fi
