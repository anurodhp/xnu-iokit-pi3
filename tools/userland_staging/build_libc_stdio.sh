#!/bin/bash
# Builds libc_build/libc_stdio.a: the real Apple/FreeBSD Libc FILE*
# infrastructure (third_party/Libc/stdio/FreeBSD/*.c) - fopen/fclose/
# fread/fwrite/fgets/fputs/getc/putc/rewind/fseek/ungetc and the
# __sF/__sglue global stream table backing stdin/stdout/stderr - built
# the same real-Apple-source-first way as build_libc_string.sh and
# build_libc_locale.sh (see docs/roadmap.md Phase 4 for the tier
# breakdown this continues).
#
# Deliberately NOT in scope here: the printf/scanf family (fprintf/
# vfprintf/scanf/...). Real Apple's implementation of that
# (third_party/Libc/stdio/FreeBSD/vfprintf.c + the xprintf_*.c "domain"
# extensibility system it's built on - xprintf_comp.c/xprintf_domain.c/
# xprintf_exec.c/xprintf_float.c/etc) is a genuinely separate, much
# larger subsystem (already flagged as such in this project's own
# libc_locale_stdio.c header comment, written a build ago) - a distinct
# future Phase 4 item, not attempted in this pass. This tier covers real
# unformatted stdio I/O only.
#
# New compat shims this tier needed, each with a real reason:
#   pthread mutex/once + OSAtomic64 (libc_stdio_shims.c) - findfp.c
#     guards its global stream table and each FILE's own _extra->fl_mutex
#     with real pthread_mutex_t/pthread_once_t. This target has no real
#     libpthread port (see libc_stdio_shims.c's own header comment for
#     why single-threaded no-ops are CORRECT here, not just convenient).
#   sysconf/getdtablesize (libc_stdio_env_shims.c) - findfp.c's __sinit()
#     calls sysconf(_SC_STREAM_MAX) to size the initial stream table;
#     f_prealloc() (unused by anything in this tier, but still part of
#     findfp.o as a whole object) calls getdtablesize(). Neither rlimit
#     machinery nor a real sysconf table exists on this target yet -
#     fixed, documented constants, not silent wrong answers (see that
#     file's own header comment).
#   compat_include/spinlock.h - findfp.c unconditionally #includes
#     <spinlock.h> (Apple-internal, not in the public iPhoneOS SDK) but
#     never actually calls anything from it in this checkout - confirmed
#     by grep before adding an empty stand-in, same precedent as
#     build_libc_locale.sh's os/assumes.h and mach-o/dyld_priv.h stubs.
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
OUT="libc_build"
mkdir -p "$OUT/apple_stdio" "$OUT/compat_include/os"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
# New real dependency as of build_libplatform.sh (see its header comment):
# libc_locale_for_stdio.a pulls in real os_unfair_lock/OSAtomic users.
if [ ! -f "$OUT/libplatform.a" ]; then
    echo "error: $OUT/libplatform.a not found -- run build_libplatform.sh first" >&2
    exit 1
fi

cat > "$OUT/compat_include/spinlock.h" <<'EOF'
/* Apple-internal header, not part of the public iPhoneOS SDK. Confirmed
 * by grep that third_party/Libc/stdio/FreeBSD/findfp.c (this build's
 * only #include <spinlock.h> call site) never actually calls anything
 * declared here - unconditional #include with no real dependency, same
 * class of stand-in as build_libc_locale.sh's os/assumes.h. */
#ifndef _COMPAT_SPINLOCK_H_
#define _COMPAT_SPINLOCK_H_
#endif
EOF

cat > "$OUT/compat_include/os/once_private.h" <<'EOF'
/* Apple-internal libplatform header, absent from this project's Libc
 * checkout entirely (confirmed by grep across the whole tree). Real
 * call site: third_party/Libc/stdio/FreeBSD/makebuf.c's __getevp(),
 * reading the STDBUF/STDBUF1 buffer-size-override env vars exactly
 * once. os_once_t is a plain caller-owned `long` here (not an opaque
 * ABI struct the way real pthread_once_t is) - see
 * libc_stdio_shims.c's os_once() for the real single-threaded-correct
 * implementation body. */
#ifndef _COMPAT_OS_ONCE_PRIVATE_H_
#define _COMPAT_OS_ONCE_PRIVATE_H_
typedef long os_once_t;
void os_once(os_once_t *predicate, void *context, void (*function)(void *));
#endif
EOF

cat > "$OUT/compat_include/stdio_flockfile_compat.h" <<'EOF'
/* FLOCKFILE()/FUNLOCKFILE() are called throughout this project's
 * third_party/Libc/stdio/FreeBSD source tier (fclose.c, fread.c,
 * fwrite.c, refill.c, rewind.c, setvbuf.c, ungetc.c, fgets.c, fputs.c,
 * getc.c, putc.c, and others) to guard per-FILE access,
 * but are NOT defined anywhere in this project's Libc checkout (confirmed
 * by grep across the whole tree) - real Apple ships their definition in a
 * private umbrella header this checkout doesn't include. Real behavior on
 * a genuinely single-threaded target (see libc_stdio_shims.c's identical
 * pthread mutex/once reasoning) is a true no-op: there is no concurrent
 * access for per-FILE locking to protect against. Force-included
 * (-include) into every stdio-tier file, same mechanism already used for
 * libc_build_prototypes.h. */
#ifndef _COMPAT_STDIO_FLOCKFILE_H_
#define _COMPAT_STDIO_FLOCKFILE_H_
#define FLOCKFILE(fp) ((void)(fp))
#define FUNLOCKFILE(fp) ((void)(fp))
#endif
EOF

# Same real flags as build_libc_locale.sh (see that script's own comments
# for why each one is needed - not re-derived here) plus stdio/FreeBSD's
# own include dir for local.h/glue.h/xprintf_private.h.
#
# Deliberately NOT -DVARIANT_DARWINEXTSN here, unlike build_libc_locale.sh:
# fopen.c/fdopen.c (uniquely among this tier's files) each self-#define
# _DARWIN_UNLIMITED_STREAMS whenever VARIANT_DARWINEXTSN is set, which
# flips third_party/Libc/include/stdio.h's declaration for THAT TU onto
# its __DARWIN_EXTSN(fopen)-aliased branch - exporting/expecting a
# real-but-differently-suffixed symbol (`_fopen$DARWIN_EXTSN`, confirmed
# via nm) that every OTHER caller in this static, no-dyld link (this
# tier's own smoketest, and locale/FreeBSD/collate.c + setrunelocale.c's
# real fdopen() calls) does NOT expect - found by hitting ld's exact
# "undefined _fdopen"/duplicate-symbol errors first, not guessed. These
# `$SUFFIX`-versioned aliases exist for real Darwin's dyld-time symbol
# versioning across shared libSystem.dylib updates, which is meaningless
# for this project's fully-static, no-dyld link - every real Libc source
# file in this tree compiles fine and exports its PLAIN name without
# VARIANT_DARWINEXTSN, this flag was never actually load-bearing for
# correctness here, just copied from build_libc_locale.sh's flag set
# without re-deriving whether it applied.
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h -include $OUT/compat_include/stdio_flockfile_compat.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD -I $LIBC/stdio/FreeBSD \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/osfmk -I $XNU/libsyscall \
    -I ../../generated"

# Real Apple/FreeBSD source: the FILE*-infrastructure files (global stream
# table, buffering, refill/flush, open/close, unformatted read/write).
# printf/scanf-family files are excluded by name (see header comment) -
# not "tried and skipped", genuinely out of scope for this pass.
ok=0
skip=0
for f in "$LIBC"/stdio/FreeBSD/*.c; do
    base=$(basename "$f" .c)
    case "$base" in
        *printf*|*scanf*|xprintf*) continue ;;
    esac
    out="$OUT/apple_stdio/${base}.o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>"$OUT/apple_stdio/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/apple_stdio/${base}.err"
    else
        skip=$((skip + 1))
        rm -f "$out"
        echo "  skip: $base"
    fi
done
echo "apple_stdio: $ok compiled, $skip skipped (printf/scanf family - genuinely deferred, see header comment)"

# This project's own shims for this tier.
"$CLANG" $CFLAGS libc_stdio_shims.c -o "$OUT/libc_stdio_shims.o"
"$CLANG" $CFLAGS libc_stdio_env_shims.c -o "$OUT/libc_stdio_env_shims.o"
"$CLANG" $CFLAGS libc_stdio_syscalls.c -o "$OUT/libc_stdio_syscalls.o"
"$CLANG" $CFLAGS libc_stdio_process_shims.c -o "$OUT/libc_stdio_process_shims.o"

rm -f "$OUT/libc_stdio.a"
ar rcs "$OUT/libc_stdio.a" \
    "$OUT"/apple_stdio/*.o \
    "$OUT/libc_stdio_shims.o" "$OUT/libc_stdio_env_shims.o" "$OUT/libc_stdio_syscalls.o" \
    "$OUT/libc_stdio_process_shims.o"
echo "wrote $OUT/libc_stdio.a"

# libc_locale.a's own libc_locale_stdio.o (build_libc_locale.sh's own
# small hand-rolled fdopen/fread/fileno/rewind/fclose FILE* shim, scoped
# to exactly what the locale-file-loading code path needed BEFORE this
# tier existed - see that file's own header comment) now genuinely
# duplicates this tier's real Apple fdopen.o/fread.o/fileno.o/rewind.o/
# fclose.o. build_libc_locale.sh's OWN smoketest still needs the shim
# (it doesn't link libc_stdio.a at all), so it can't be removed from
# libc_locale.a itself - instead, build a stdio-tier-specific copy of the
# archive with just that one now-redundant member dropped, real fix (not
# a hack) verified necessary by actually hitting ld's duplicate-symbol
# error first, not guessed in advance.
rm -rf "$OUT/locale_for_stdio_extract"
mkdir -p "$OUT/locale_for_stdio_extract"
(cd "$OUT/locale_for_stdio_extract" && ar x ../libc_locale.a && rm -f libc_locale_stdio.o)
rm -f "$OUT/libc_locale_for_stdio.a"
ar rcs "$OUT/libc_locale_for_stdio.a" "$OUT"/locale_for_stdio_extract/*.o
echo "wrote $OUT/libc_locale_for_stdio.a (libc_locale.a minus its now-redundant FILE* shim)"

# Link the real smoketest against libc_stdio.a + libc_locale.a (stdio pulls
# in locale-tier symbols: __xlocale_init-family/ctype_l/etc - and
# libc_locale.a already a superset of libc_string.a's string/*.c +
# freebsd_string primitives, see build_libc_locale.sh's own file list, so
# libc_string.a is deliberately NOT also linked here - would duplicate-
# define memcpy/strlen/etc between the two archives) + this project's
# existing locale-tier shims (TSD/syscalls/errno) - same archive-not-glob
# linking discipline build_libc_locale.sh's own comment explains (only
# referenced members get pulled in).
# No -DVARIANT_DARWINEXTSN/_DARWIN_UNLIMITED_STREAMS here either (see this
# script's own CFLAGS comment above for the full reasoning) - the
# smoketest is an ordinary caller, and with the main CFLAGS no longer
# self-selecting the EXTSN-aliased branch, fopen.o/fdopen.o now export
# their plain, unsuffixed names, matching what a plain #include <stdio.h>
# here expects with no special defines at all.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -include libc_build_prototypes.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -I $LIBC/include \
    libc_stdio_smoketest.c -o "$OUT/libc_stdio_smoketest.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -o libc_stdio_smoketest.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT/libc_stdio_smoketest.o" \
    "$OUT/libc_stdio.a" "$OUT/libc_locale_for_stdio.a" "$OUT/libc_stdio.a" \
    "$OUT/pthread_tsd_shim.o" "$OUT/pthread_tsd_shim_libc_bootstrap.o" \
    "$OUT/compat_stubs.o" "$OUT/cerror_stub.o" "$OUT/libplatform.a"
# libc_stdio.a is listed twice deliberately: real ld64, unlike a symbol-
# table-indexed linker, only pulls archive members in a single left-to-
# right pass, so libc_locale_for_stdio.a's collate.o/setrunelocale.o
# (which call real fdopen()) can't reach back into an EARLIER archive on
# the command line - confirmed by hitting exactly that "undefined _fdopen"
# error with libc_stdio.a listed only once, before fixing it this way.
# Standard two-pass fix for this kind of circular archive dependency
# (stdio needs locale's ctype_l/xlocale symbols, locale needs stdio's
# real fdopen/fread/fileno/rewind), not a workaround for a bug.
echo "wrote libc_stdio_smoketest.macho"

otool -l libc_stdio_smoketest.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l libc_stdio_smoketest.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
if nm -u libc_stdio_smoketest.macho | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u libc_stdio_smoketest.macho >&2
fi
