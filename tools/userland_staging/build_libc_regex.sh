#!/bin/bash
# Builds libc_build/libc_regex.a: real POSIX regcomp/regexec/regfree/
# regerror -- Phase 4.6 item 3 (docs/roadmap.md). Real, unmodified Apple
# source: the TRE engine (third_party/Libc/regex/TRE/lib/{regcomp,regexec,
# tre-ast,tre-compile,tre-match-backtrack,tre-match-parallel,tre-mem,
# tre-parse,tre-stack}.c) plus FreeBSD's regerror.c
# (third_party/Libc/regex/FreeBSD/regerror.c, Apple's own vendored
# fallback for that one function). Same Csu-static-target/curated-libc-
# stack pattern every other tier in this directory follows (see
# build_libc_locale.sh for the fullest-commented reference version of
# this CFLAGS recipe) -- not a new build methodology.
#
# Real dependency ordering: needs libc_locale.a (real
# __collate_equiv_value, tre-parse.c's locale-aware character-class/
# equivalence-class support) and libmalloc.a (xmalloc.h falls through to
# plain malloc/calloc/free/realloc when MALLOC_DEBUGGING is undefined --
# confirmed by reading that header, not assumed) already built.
#
# Real gaps found and fixed here (see individual comments below for detail):
#   - TRE's own headers (tre.h via tre-config.h) expect real system
#     <sys/types.h>/<wchar.h>/<wctype.h> to already be reachable the
#     normal hosted way; -ffreestanding does NOT remove those from the
#     search path (confirmed: a bare `#include <string.h>` compiles fine
#     with this exact SDK/target) -- the very first attempt's
#     "'string.h' file not found" turned out to be a shell quoting bug in
#     an ad hoc test invocation, not a real header gap. No fix needed in
#     this script itself.
#   - regcomp(3) is __DARWIN_ALIAS'd in this checkout's own vendored
#     third_party/Libc/include/sys/cdefs.h (found via -I $LIBC/include
#     search-path precedence over the SDK's own cdefs.h) to the
#     $UNIX2003-suffixed symbol even on this LP64 target -- unlike
#     regexec/regfree, which carry no such alias. Real callers built
#     against this same header (e.g. bash's lib/sh/shmatch.c) therefore
#     reference `_regcomp$UNIX2003`, not plain `_regcomp`. TRE's own
#     regcomp.c, compiled standalone, exports plain `_regcomp` (its
#     `#define tre_regcomp regcomp` in tre.h is a C-level rename, not an
#     asm-level alias). bash_syscalls.c's regcomp_u2003 trampoline
#     (already present, previously forwarding to the narrow REG_BADPAT
#     stub) is repointed to forward to this archive's real regcomp
#     instead -- see that file's own updated comment.
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
OUT="libc_build"
mkdir -p "$OUT/apple_regex"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
for a in libc_locale libmalloc libplatform; do
    if [ ! -f "$OUT/$a.a" ]; then
        echo "error: $OUT/$a.a not found -- run build_${a}.sh first" >&2
        exit 1
    fi
done

# Same real flags every other tier uses (see build_libc_locale.sh's own
# header comment for the full reasoning behind each one) plus this tier's
# own -I paths: TRE's lib/ dir first (its internal headers #include
# "tre.h"/"tre-internal.h"/etc with no subdirectory prefix), TRE's top
# level (tre-config.h, config.h, tre-last-matched.h), and FreeBSD/ (for
# regerror.c's own cname.h/utils.h).
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O2 \
    -Wno-nullability-completeness \
    -include regex_availability_override.h -include libc_build_prototypes.h -include $LIBC/regex/TRE/config.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -D_USE_EXTENDED_LOCALES_ \
    -I $OUT/compat_include \
    -I $LIBC/regex/TRE/lib -I $LIBC/regex/TRE -I $LIBC/regex/FreeBSD \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/osfmk -I $XNU/libsyscall \
    -I ../../generated"

ok=0
fail=0
for f in "$LIBC"/regex/TRE/lib/*.c "$LIBC/regex/FreeBSD/regerror.c"; do
    base=$(basename "$f" .c)
    out="$OUT/apple_regex/${base}.o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>"$OUT/apple_regex/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/apple_regex/${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OUT/apple_regex/${base}.err")"
    fi
done
echo "libc_regex: $ok compiled, $fail gaps (see libc_build/apple_regex/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi

rm -f "$OUT/libc_regex.a"
ar rcs "$OUT/libc_regex.a" "$OUT"/apple_regex/*.o
echo "wrote $OUT/libc_regex.a"

# Real smoketest: links a tiny real regcomp/regexec/regfree/regerror
# consumer against this archive plus the same libc_locale/libmalloc/
# libplatform/libc_string/libc_stdio/libpthread stack build_bash.sh
# itself links against, to prove real symbol resolution (including the
# $UNIX2003 regcomp alias discussed above) works standalone before ever
# touching bash. Degrades to a skip (not a hard failure) if any
# prerequisite archive this needs isn't built yet, matching every other
# tier's from-scratch-checkout-ordering convention.
NEED_OK=1
for a in libc_string libc_stdio libc_gen libc_printf libpthread libclosure libunwind; do
    [ -f "$OUT/$a.a" ] || NEED_OK=0
done
if [ "$NEED_OK" -eq 1 ]; then
    rm -f libc_regex_smoketest.macho
    "$CLANG" $CFLAGS libc_regex_smoketest.c -o "$OUT/libc_regex_smoketest.o"
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
        -o libc_regex_smoketest.macho \
        ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
        "$OUT/libc_regex_smoketest.o" "$OUT/libc_regex.a" \
        "$OUT/libc_locale.a" "$OUT/libc_gen.a" "$OUT/libc_stdio.a" "$OUT/libc_printf.a" \
        "$OUT/libc_string.a" "$OUT/libunwind.a" "$OUT/libpthread.a" "$OUT/libclosure.a" \
        "$OUT/libmalloc.a" "$OUT/libplatform.a" \
        -Wl,-alias,_mprotect,'_mprotect$UNIX2003' \
        -Wl,-alias,___kill,'_kill$UNIX2003' \
        -Wl,-alias,___kill,_kill \
        -Wl,-alias,_write,'_write$UNIX2003' \
        -Wl,-alias,__mal_semwait_sleep,'_sleep$UNIX2003' \
        -Wl,-alias,___kdebug_trace,_kdebug_trace \
        -Wl,-alias,_pthread_testcancel,'_pthread_testcancel$UNIX2003' \
        -Wl,-alias,_setenv,'_setenv$UNIX2003' \
        -Wl,-alias,_fopen,'_fopen$UNIX2003' \
        -Wl,-alias,'_pthread_rwlock_wrlock$UNIX2003',_pthread_rwlock_wrlock \
        -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003',_pthread_rwlock_rdlock \
        -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003',_pthread_rwlock_unlock \
        2>"$OUT/libc_regex_smoketest_link.err" && echo "wrote libc_regex_smoketest.macho" || echo "WARNING: smoketest link failed (libc_regex.a itself is still built -- see $OUT/libc_regex_smoketest_link.err)" >&2

    if [ -f libc_regex_smoketest.macho ]; then
        otool -l libc_regex_smoketest.macho | grep -q LC_LOAD_DYLIB && \
            echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
        otool -l libc_regex_smoketest.macho | grep -q LC_UNIXTHREAD || \
            echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
        if nm -u libc_regex_smoketest.macho | grep -q .; then
            echo "WARNING: undefined symbols present:" >&2
            nm -u libc_regex_smoketest.macho >&2
        fi
    else
        tail -60 "$OUT/libc_regex_smoketest_link.err" >&2
    fi
else
    echo "skip: libc_regex_smoketest.macho (needs libc_string/libc_stdio/libc_gen/libc_printf/libpthread/libclosure/libunwind .a -- run their build_*.sh first)"
fi
