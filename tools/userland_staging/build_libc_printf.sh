#!/bin/bash
# Builds libc_build/libc_printf.a: the real Apple/FreeBSD printf/scanf
# ("xprintf") family - stdio/FreeBSD/{vfprintf,vfscanf,vfwprintf,vfwscanf,
# printf-pos,xprintf,xprintf_*}.c plus every public printf*/scanf*/wprintf*/
# wscanf* wrapper - deliberately deferred by build_libc_stdio.sh (see that
# script's own header comment) as "a genuinely separate, much larger
# subsystem". Continues the same real-Apple-source-first tier-building
# pattern as build_libc_string.sh/build_libc_locale.sh/build_libc_stdio.sh
# (see docs/roadmap.md Phase 4 for the running tier breakdown).
#
# NOT included: stdio/{xprintf_domain,xprintf_comp,xprintf_exec,
# xprintf_all_in_one}.c - the NEWER "%{domain}" runtime-extensible printf
# API (separate from stdio/FreeBSD/xprintf.c's real engine, confirmed by
# vfprintf.c's own "#if 0 // xprintf pending API review" guard around its
# only use of that API - genuinely unused by anything this tier compiles,
# not a skip of working functionality).
#
# Two new real compat pieces this tier needed, each hit as a real
# compile/link error first, not guessed in advance:
#   -I .../gdtoa -I .../gdtoa/FreeBSD  - printfcommon.h's real float
#     formatting needs gdtoa.h (David Gay's real dtoa/gdtoa library, also
#     vendored real source in this Libc checkout, just not on the default
#     include path any earlier tier needed).
#   libc_printf_mach_stub.c + compat_include_printf/os/assumes.h - real
#     Apple's os/assumes.h pulls in a full ASL-logging + os_log-pack-
#     builtin subsystem this target doesn't have, just to back the
#     os_crash() abort macro vfprintf.c's real %n-write CVE mitigation
#     calls; and real vm_region_64()/mach_task_self() (Mach IPC/MIG, also
#     not ported - see libc_printf_mach_stub.c's own header comment) back
#     the actual "is this format string read-only" check that decides
#     whether to call it. This tier's own project-owned, real (not fake)
#     versions keep the SAFE, fail-closed behavior (any live %n use
#     aborts the process, since this target can't verify read-only-ness)
#     without needing full Mach IPC - see libc_printf_mach_stub.c for the
#     complete real reasoning. compat_include_printf is a SEPARATE -I dir
#     from the shared libc_build/compat_include (which build_libc_locale.sh/
#     build_libc_stdio.sh's own os/assumes.h stand-in there is
#     deliberately EMPTY for) so build order between tiers can never flip
#     this tier's os_crash between real and undefined.
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
OUT="libc_build"
mkdir -p "$OUT/apple_printf" "$OUT/compat_include_printf/os" "$OUT/compat_include"

# stdio_flockfile_compat.h: real gap, found 2026-09-13 via a live
# build_everything.sh run -- this script's own gdtoa/apple_printf compile
# loop below force-includes this header (-include, see CFLAGS further
# down) but never wrote it itself, relying entirely on build_libc_gen.sh
# (or build_libc_stdio.sh) having already run first with no check for
# that at all. Same self-sufficiency fix already applied to
# build_libc_gen.sh/build_libplatform.sh/build_libmalloc.sh for the
# identical cross-dependency class -- write this script's own idempotent
# copy rather than add another ordering dependency. Content copied
# verbatim from build_libc_gen.sh's own copy (FLOCKFILE/FUNLOCKFILE
# genuinely undefined in this checkout; true no-op is correct on this
# single-threaded target).
cat > "$OUT/compat_include/stdio_flockfile_compat.h" <<'EOF'
/* See build_libc_stdio.sh's own stdio_flockfile_compat.h for the full
 * real reasoning (FLOCKFILE/FUNLOCKFILE genuinely undefined in this
 * checkout; true no-op is correct on this single-threaded target).
 * Written here too since this script may run before build_libc_gen.sh/
 * build_libc_stdio.sh; do not edit here, it is regenerated on every run. */
#ifndef _COMPAT_STDIO_FLOCKFILE_H_
#define _COMPAT_STDIO_FLOCKFILE_H_
#define FLOCKFILE(fp) ((void)(fp))
#define FUNLOCKFILE(fp) ((void)(fp))
#endif
EOF

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

cat > "$OUT/compat_include_printf/os/assumes.h" <<'EOF'
/* Printf-tier-scoped shadow of third_party/Libc/os/assumes.h (kept in a
 * SEPARATE -I dir from the shared libc_build/compat_include used by the
 * locale/stdio tiers' own EMPTY os/assumes.h stand-in, deliberately - both
 * scripts unconditionally regenerate their compat headers on every run, so
 * sharing one path would make the last tier built silently win, flipping
 * this tier's os_crash between real and undefined depending on build
 * order. See libc_printf_mach_stub.c's header comment for the real
 * design/reasoning behind what os_crash does here. Confirmed by grep
 * (this script's own file list survey) that os_crash is the ONLY symbol
 * from this header any file in this tier actually uses - safe to shadow
 * rather than pull in the real header's full ASL-logging/os_log-pack
 * machinery. */
#ifndef _COMPAT_OS_ASSUMES_H_PRINTF_
#define _COMPAT_OS_ASSUMES_H_PRINTF_

/* Real os/assumes.h transitively brings mach_task_self() into scope for
 * vfprintf.c (which never #includes <mach/mach_init.h> itself - found by
 * hitting the exact "implicit declaration of function 'mach_task_self'"
 * error first with this header otherwise empty, not guessed). */
#include <mach/mach_init.h>

void _libc_printf_os_crash(const char *diagnostic) __attribute__((noreturn));
#define os_crash(fmt, ...) _libc_printf_os_crash(fmt)

#endif
EOF

# Same real flags as build_libc_stdio.sh (see that script's own comments -
# not re-derived here), plus this tier's own -I additions (xprintf_domain.h
# lives in stdio/ not stdio/FreeBSD/; gdtoa.h lives in gdtoa/FreeBSD/) and
# compat_include_printf listed FIRST so its os/assumes.h shadow wins over
# both the real Libc one and the shared empty stand-in.
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h -include $OUT/compat_include/stdio_flockfile_compat.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I $OUT/compat_include_printf \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD -I $LIBC/stdio -I $LIBC/stdio/FreeBSD \
    -I $LIBC/gdtoa -I $LIBC/gdtoa/FreeBSD \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/osfmk -I $XNU/libsyscall \
    -I ../../generated"

# Real Apple/FreeBSD source: the xprintf engine + every public printf*/
# scanf*/wprintf*/wscanf* wrapper, narrow (unformatted stdio) and
# xprintf_domain-registration (%{domain}) files excluded by name - see
# header comment for why each is genuinely out of scope.
ok=0
skip=0
for f in "$LIBC"/stdio/FreeBSD/*.c; do
    base=$(basename "$f" .c)
    case "$base" in
        *printf*|*scanf*|xprintf*) ;;  # this tier's actual target set
        *) continue ;;
    esac
    out="$OUT/apple_printf/${base}.o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>"$OUT/apple_printf/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/apple_printf/${base}.err"
    else
        skip=$((skip + 1))
        rm -f "$out"
        echo "  skip: $base"
    fi
done
echo "apple_printf: $ok compiled, $skip skipped"

# Real David Gay gdtoa library (float<->string conversion, printfcommon.h's
# real dependency) - file selection is Apple's OWN real per-arch rule, read
# directly from third_party/Libc/xcodescripts/libc.xcconfig rather than
# guessed: FreeBSD_EXCLUDED_SOURCE_GDTOA excludes every gdtoa/FreeBSD/
# machdep*.c and gdtoa-strtopx.c from the general build, then
# FreeBSD_INCLUDED_SOURCE_FILE_NAMES_arm64 (= _arm) adds back exactly
# machdep_ldisd.c - the "long double IS (the same as) double" variant,
# correct for arm64 where long double is 64-bit like double (unlike x86_64,
# which gets machdep_ldisx.c's real 80-bit extended path). Compiling more
# than one machdep_ldis*.c would double-define strtold/strtold_l.
# _ldbl_util.c is ALSO genuinely arm64-inapplicable, beyond the xcconfig's
# own machdep*/gdtoa-strtopx excludes: it operates on `union IEEEl2bits`,
# a two-word (80-bit-extended or double-double) long-double layout used on
# x86_64/PowerPC - real compile errors ("no member named 'd'"/'sign2'/
# 'exp2' in struct IEEEl2bits") confirm it structurally doesn't apply
# where long double IS double (arm64's real layout, machdep_ldisd.c's own
# premise) - not a bug to fix, a real architecture mismatch.
# gdtoa-strtoIg.c has a real, pre-existing bug in this exact checkout: it
# calls strtodg() with 5 args against gdtoa.h's own real 6-arg prototype
# (missing a locale_t) - confirmed nothing in this tier's real undefined-
# symbol surface ever references anything from this file, so it's
# genuinely dead/unused here regardless, not worth patching upstream Apple
# source for.
gdok=0
gdskip=0
for f in "$LIBC"/gdtoa/*.c "$LIBC"/gdtoa/FreeBSD/*.c; do
    base=$(basename "$f" .c)
    case "$base" in
        machdep_ldisx|machdep_ldisdd|gdtoa-strtopx|_ldbl_util|gdtoa-strtoIg) continue ;;
    esac
    out="$OUT/apple_printf/${base}.o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>"$OUT/apple_printf/${base}.err"; then
        gdok=$((gdok + 1))
        rm -f "$OUT/apple_printf/${base}.err"
    else
        gdskip=$((gdskip + 1))
        rm -f "$out"
        echo "  skip (gdtoa): $base"
    fi
done
echo "gdtoa: $gdok compiled, $gdskip skipped"

# Real strtoimax_l/strtoumax_l (vfscanf.c's %j conversion) + real
# iswspace_l/iswalpha_l/etc (vfscanf.c's %[ / whitespace-skip logic) - the
# latter via locale/iswctype.c, which (like locale/isctype.c already does
# for plain ctype_l in build_libc_locale.sh) self-defines
# _EXTERNALIZE_WCTYPE_INLINES_TOP_ before #include <wctype.h> to force real,
# externally-linkable definitions instead of the header's normal
# extern-inline-only versions (real Apple mechanism, read directly from
# third_party/Libc/include/wctype.h's own "//Begin-Libc" comment block, not
# invented) - found by hitting the exact "undefined _iswspace_l" error
# first (an ordinary -I addition doesn't fix it: the inline definition in
# the header never emits a symbol at all without this define).
for f in "$LIBC/stdlib/FreeBSD/strtoimax.c" "$LIBC/stdlib/FreeBSD/strtoumax.c" "$LIBC/locale/iswctype.c"; do
    base=$(basename "$f" .c)
    out="$OUT/apple_printf/${base}.o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>"$OUT/apple_printf/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/apple_printf/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
        cat "$OUT/apple_printf/${base}.err" >&2
    fi
done

"$CLANG" $CFLAGS libc_printf_mach_stub.c -o "$OUT/libc_printf_mach_stub.o"
# Real __fpclassifyd + real (payload-simplified) nan/nanf/nanl - see
# libc_printf_fp_shims.c's own header comment. These are genuinely libm
# functions (third_party/Libm isn't cloned into this project at all - out
# of Phase 4 step 1's library list) but small/self-contained enough to
# write for real, same "write custom only as last resort, but for real"
# precedent as this project's own from-scratch SHA-256/384/512.
"$CLANG" $CFLAGS libc_printf_fp_shims.c -o "$OUT/libc_printf_fp_shims.o"

rm -f "$OUT/libc_printf.a"
ar rcs "$OUT/libc_printf.a" "$OUT"/apple_printf/*.o "$OUT/libc_printf_mach_stub.o" "$OUT/libc_printf_fp_shims.o"
echo "wrote $OUT/libc_printf.a"

# Link the real smoketest against libc_printf.a + libc_stdio.a (real FILE*
# infra printf/scanf calls into) + libc_locale_for_stdio.a (ctype_l/
# xlocale symbols both tiers need) + this project's existing shims, same
# archive-not-glob discipline and libc_stdio.a-listed-twice fix as
# build_libc_stdio.sh's own smoketest link (circular archive dependency:
# printf needs stdio's real fopen/fwrite, stdio's locale-tier deps need
# printf's own new symbols nothing else provides).
# -D_FORTIFY_SOURCE=0 here too (real Apple stdio.h wraps snprintf() with a
# __builtin___snprintf_chk fortify call otherwise, needing a real
# __snprintf_chk from Libc/secure/ this project doesn't build - found by
# hitting the exact "undefined ___snprintf_chk" error first, not guessed;
# every archive-side CFLAGS above already sets this, only the smoketest's
# own compile line hadn't).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -include libc_build_prototypes.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -I $LIBC/include \
    libc_printf_smoketest.c -o "$OUT/libc_printf_smoketest.o"
# libc_gen.a linked in for real errx() (vfprintf.c/vfscanf.c's internal
# invariant-violation path in a couple of conversions - already real,
# built by build_libc_gen.sh, just wasn't part of any earlier tier's link
# line since nothing earlier needed it). libplatform.a (real Apple
# OSAtomic*/os_unfair_lock* - see build_libplatform.sh) is a newer sibling
# tier's real dependency of libc_stdio.a/libc_locale_for_stdio.a as of
# this session (they no longer get those symbols from compat_stubs.c's
# stand-ins) - required here too now for the same reason.
if [ ! -f "$OUT/libplatform.a" ]; then
    echo "error: $OUT/libplatform.a not found -- run build_libplatform.sh first" >&2
    exit 1
fi
# New real dependency as of build_libmalloc.sh: real malloc/free instead
# of compat_stubs.c's now-retired placeholder bump allocator (real
# xprintf/vfprintf code genuinely calls them). Also required
# libc_printf_mach_stub.c to retire its own mach_task_self_/abort()/
# __assert_rtn() stand-ins, which real duplicate-symbol errors surfaced
# once now-real equivalents from libmalloc.a were in the same link (see
# that file's own updated comments).
if [ ! -f "$OUT/libmalloc.a" ]; then
    echo "error: $OUT/libmalloc.a not found -- run build_libmalloc.sh first" >&2
    exit 1
fi
# New real dependency as of build_copyfile.sh (later Phase 4 pass):
# libc_printf_mach_stub.c's own _Block_copy/_Block_release identity-
# passthrough stand-ins were retired once real libclosure.a existed (a
# real duplicate-symbol conflict otherwise) - see that file's own header
# comment. err.o (libc_gen.a) still references them transitively via
# err_set_exit_b/err_set_exit, so libclosure.a must be linked here now.
if [ ! -f "$OUT/libclosure.a" ]; then
    echo "error: $OUT/libclosure.a not found -- run build_libclosure.sh first" >&2
    exit 1
fi
# New real dependency as of build_libunwind.sh (later Phase 4 pass):
# libc_printf_mach_stub.c's own pthread_rwlock_rdlock/unlock no-op
# stand-ins were retired once real libpthread.a's pthread_rwlock_wrlock
# was genuinely exercised for the first time (libunwind's RWMutex) - see
# that file's updated header comment. Real pthread_rwlock's contended
# path also needs real sched_yield() (libunwind_sched_yield.c/.s - not
# libunwind-specific despite the filename, just added there first; see
# that file's own header comment for the real Apple pthread.c one-liner
# it copies), compiled fresh here too rather than depending on
# build_libunwind.sh's own output ordering (this tier legitimately builds
# before libunwind in the pipeline).
if [ ! -f "$OUT/libpthread.a" ]; then
    echo "error: $OUT/libpthread.a not found -- run build_libpthread.sh first" >&2
    exit 1
fi
# Real gap, found 2026-09-13 (proactive audit ahead of a build_everything.sh
# rerun -- this script is the immediate next real failure after
# build_launchd_mig.sh's own identical bug class): the smoketest link
# below needs libc_stdio.a/libc_locale_for_stdio.a/libc_gen.a with no
# prerequisite check at all. libc_printf.a itself (written above) needs
# neither.
if [ ! -f "$OUT/libc_stdio.a" ] || [ ! -f "$OUT/libc_locale_for_stdio.a" ]; then
    echo "error: $OUT/libc_stdio.a and $OUT/libc_locale_for_stdio.a required -- run build_libc_stdio.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/libc_gen.a" ]; then
    echo "error: $OUT/libc_gen.a not found -- run build_libc_gen.sh first" >&2
    exit 1
fi
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    libunwind_sched_yield.s -o "$OUT/libc_printf_sched_yield_trap.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libunwind_sched_yield.c -o "$OUT/libc_printf_sched_yield.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -o libc_printf_smoketest.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT/libc_printf_smoketest.o" \
    "$OUT/libc_printf.a" "$OUT/libc_stdio.a" "$OUT/libc_locale_for_stdio.a" "$OUT/libc_gen.a" "$OUT/libc_printf.a" "$OUT/libc_stdio.a" \
    "$OUT/pthread_tsd_shim_libc_bootstrap.o" \
    "$OUT/libpthread.a" "$OUT/libc_printf_sched_yield_trap.o" "$OUT/libc_printf_sched_yield.o" \
    "$OUT/libmalloc.a" "$OUT/cerror_stub.o" "$OUT/libplatform.a" "$OUT/libclosure.a" \
    -Wl,-alias,'_pthread_rwlock_wrlock$UNIX2003',_pthread_rwlock_wrlock \
    -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003',_pthread_rwlock_rdlock \
    -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003',_pthread_rwlock_unlock \
    -Wl,-alias,_mprotect,'_mprotect$UNIX2003' \
    -Wl,-alias,___kill,'_kill$UNIX2003' \
    -Wl,-alias,___kill,_kill \
    -Wl,-alias,_write,'_write$UNIX2003' \
    -Wl,-alias,__mal_semwait_sleep,'_sleep$UNIX2003' \
    -Wl,-alias,___kdebug_trace,_kdebug_trace
echo "wrote libc_printf_smoketest.macho"

otool -l libc_printf_smoketest.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l libc_printf_smoketest.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
if nm -u libc_printf_smoketest.macho | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u libc_printf_smoketest.macho >&2
fi
