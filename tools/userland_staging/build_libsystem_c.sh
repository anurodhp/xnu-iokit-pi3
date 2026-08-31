#!/bin/bash
# Builds libc_build/system/libsystem_c.dylib: static-to-dylib migration
# step 4 (docs/daemon-roadmap.md's "Static-vs-dylib architecture debt"
# section).
#
# Combines libc_string.a + libc_gen.a + libc_stdio.a + libc_printf.a +
# libc_locale_for_stdio.a + libc_regex.a into ONE real dylib, matching
# real Apple's actual granularity -- real macOS does NOT split these
# into separate libsystem_string.dylib/libsystem_stdio.dylib/etc images,
# they're all one real libsystem_c.dylib. This project's own build
# scripts split them into separate .a files purely for build-pipeline
# convenience (see docs/roadmap.md Phase 4's tier breakdown); that
# internal grouping doesn't have to (and, given the real duplicate-
# global-state risk below, must not) become separate dylib images.
#
# THIS ONE IS ALSO A REAL CORRECTNESS FIX, same class as
# libsystem_malloc.dylib's g_zone and libsystem_pthread.dylib's thread
# registry. Audited before writing this script: libc_stdio's real
# `__stdinp`/`__stdoutp`/`__stderrp` (the actual FILE* globals every
# printf/fprintf/fflush call in this port goes through) are plain BSS
# globals. Splitting stdio from printf (or either from anything else
# that touches these three FILE*s) into separate dylibs would give each
# image its own private stdin/stdout/stderr, with independently
# buffered, divergent state -- writes from one dylib's copy could
# interleave incorrectly with or clobber the other's buffered data.
# Bundling all of Libc into one real dylib avoids ever creating that
# split in the first place, same real fix shape as batch 2's malloc/
# pthread findings, just avoided by not fragmenting to begin with
# rather than fixed after the fact.
#
# Real, unmodified Apple/FreeBSD source: same file sets
# build_libc_string.sh/build_libc_gen.sh/build_libc_stdio.sh/
# build_libc_printf.sh/build_libc_locale.sh/build_libc_regex.sh already
# established for their own static archives (each script's own header
# comment has the full scope/gap writeup, not re-derived here).
#
# Real correction made while writing this script: a first attempt
# linked the 5 archives directly (not pre-extracted) hoping `ld`'s
# normal selective archive-member-pulling would just replicate today's
# already-working static link. It compiled, but left this dylib
# missing basic, obviously-needed exports (_getenv, _memchr, _memmove,
# _snprintf, _strcmp, _strlcpy, _vfprintf, _fprintf, _mkstemp) --
# confirmed via nm -g showing them absent. Root cause: selective
# scanning only pulls a member when something ALREADY IN THAT SAME LINK
# needs it; in the monolithic static build these get pulled because
# libcxx.a/libcxxabi.a/libunwind.a are linked in the SAME invocation and
# need them, but a real shared dylib has to export a real, complete API
# surface for ANY future consumer, not just what happens to be needed by
# whichever specific other archives are present today. Fixed by
# extracting every member (`ar x`, same approach every dylib since
# libsystem_malloc.dylib already uses) and closing the real gaps that
# approach surfaces one at a time below, same discipline
# build_libsystem_pthread.sh's pthread_atfork.o gap already used.
#
# One real, pre-existing overlap to get right: 38 string primitives
# (strlen.o, memcpy.o, etc) exist in BOTH libc_string.a and
# libc_locale_for_stdio.a. The current static link lists
# libcxx_locale_for_stdio_scratch.a (== libc_locale_for_stdio.a minus
# pthread_tsd_shim.o) BEFORE libc_string.a, so locale's copies are what
# actually link today -- extraction order below matches (locale
# extracted after string, so its copies overwrite on disk, the same
# "last write wins" outcome `ld`'s left-to-right archive scan gives the
# earlier-listed archive).
#
# Real, honestly-scoped exclusions (not silently worked around) -- each
# pulls in a real dependency this project's build has never established
# for reasons already documented elsewhere, confirmed real via a grep
# for source, not guessed. mktemp.o/tmpfile.o/tmpnam.o/tempnam.o are
# NOT excluded, despite needing real arc4random_uniform()/
# open_dprotected_np()/__sigprocmask -- see libsystem_c_syscall_compat.c's
# own comment for why (real dyld's own LibSystemHelpers::mkstemp() needs
# a real, working mkstemp(), discovered when this dylib was first linked
# against libdyld.dylib for real, same shape as batch 3's pthread_atfork
# discovery):
#   - machdep_ldisQ.o: needs strtopQ, real gdtoa Quad(128-bit)-precision
#     long double support -- genuinely inapplicable to this target:
#     arm64's `long double` is IEEE double (64-bit), identical to
#     `double`, so the whole Quad code path is real but dead code for
#     this architecture specifically, not a missing port.
#   - wcsftime.o (needs strftime_l) and the whole wide-char scanf family
#     (wscanf.o/vwscanf.o/fwscanf.o/swscanf.o/vswscanf.o/vfwscanf.o, all
#     cascading to the same wcstod_l/wcstof_l/wcstold_l family): real
#     source exists (stdtime/FreeBSD/strftime.c, locale/FreeBSD/
#     wcstod.c) but pulling it in is a real, substantial locale-
#     integration undertaking (this project's locale work has
#     repeatedly hit exactly this class of gap before, see project
#     memory on wide-char/locale work) -- scoped out rather than guessed
#     at under time pressure; a real follow-up, not a permanent ceiling.
# Neither of these was reachable in the current, already-working static
# link either (confirmed: it links today with zero undefined symbols
# without them) -- excluding them here preserves that exact behavior
# rather than changing it.
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
XNU="../../third_party/xnu-7195"
OUT="libc_build"
EXTRACT="$OUT/libsystem_c/extracted"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

# Real, verified fix (2026-08-28, see docs/backlog.md item d9 for the
# full trail and tools/userland_staging/build_simple_cmds.sh's own
# NEWLD_BINDIR header comment for the isolated repro this is based on):
# Xcode 12's own bundled ld64-609 has a real internal crash under
# -Wl,-fixup_chains that a much newer ld does not. Compiling still uses
# Xcode 12's clang/SDK above (untouched -- that vintage match is a
# separate, still-load-bearing invariant); only the final link step's
# linker binary changes, via clang's -B override.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi
for a in libc_string libc_gen libc_stdio libc_printf libc_locale_for_stdio libc_regex; do
    if [ ! -f "$OUT/$a.a" ]; then
        echo "error: $OUT/$a.a not found -- run build_$a.sh first (this script reuses their already-built member objects)" >&2
        exit 1
    fi
done
if [ ! -f "$OUT/system/libsystem_kernel.dylib" ] || [ ! -f "$OUT/system/libsystem_platform.dylib" ] || \
   [ ! -f "$OUT/system/libsystem_malloc.dylib" ] || [ ! -f "$OUT/system/libsystem_pthread.dylib" ] || \
   [ ! -f "$OUT/system/libsystem_blocks.dylib" ] || [ ! -f "$OUT/system/libdyld.dylib" ]; then
    echo "error: libsystem_kernel/platform/malloc/pthread/blocks/libdyld.dylib not found -- run those build scripts first" >&2
    exit 1
fi

rm -rf "$EXTRACT"
mkdir -p "$EXTRACT"
(cd "$EXTRACT" && ar x "../../libc_string.a")
(cd "$EXTRACT" && ar x "../../libc_gen.a")
(cd "$EXTRACT" && ar x "../../libc_stdio.a")
(cd "$EXTRACT" && ar x "../../libc_locale_for_stdio.a")
# cerror_stub.o dropped 2026-08-30 (static-linking cleanup, Phase 1d):
# it defines a plain, MUTABLE `int errno` global plus __error/cerror/
# cerror_nocancel/_pthread_exit_if_canceled (see cerror_stub.c's own
# header). build_libsystem_kernel.sh compiles the exact same source
# directly into libsystem_kernel.dylib -- a real sibling on this dylib's
# own link line -- so before this fix, every process (which always loads
# both via libSystem.B.dylib) had TWO separate `errno` globals: a raw
# syscall failing in libsystem_kernel set one, while every libsystem_c
# function that reads errno via __error() read the other, never-updated
# copy. Real duplicate STATE, not just duplicate code -- the exact
# priority-1 category docs/static-linking-cleanup-plan.md's residual
# table flags ("___error/_errno/_cerror -- real duplicate-state,
# prioritise"). libc_locale_for_stdio.a's own copy stays for its
# original purpose (build_libc_stdio.sh's standalone smoketest, which
# links no libsystem_kernel.dylib at all); only this dylib's build drops
# it.
rm -f "$EXTRACT/cerror_stub.o"
(cd "$EXTRACT" && ar x "../../libc_printf.a")
(cd "$EXTRACT" && ar x "../../libc_regex.a")
rm -f "$EXTRACT/__.SYMDEF"* "$EXTRACT/pthread_tsd_shim.o" \
    "$EXTRACT/machdep_ldisQ.o" "$EXTRACT/wcsftime.o" "$EXTRACT/vfwscanf.o" "$EXTRACT/vswscanf.o" \
    "$EXTRACT/wscanf.o" "$EXTRACT/vwscanf.o" "$EXTRACT/fwscanf.o" "$EXTRACT/swscanf.o"

# Real gap: sys_siglist (real strsignal_r() dependency) and strvisx
# (real ___printf_render_vis() dependency) have real FreeBSD source
# neither build_libc_gen.sh nor any other tier's own file list happened
# to include yet -- added directly here, same real source, not stubbed.
# Same real CFLAGS/include set build_libc_locale.sh already established
# for locale-touching real Apple/FreeBSD source (vis.c pulls in
# ctype.h's real xlocale machinery) -- not re-derived twice.
PTHREAD="../../third_party/libpthread"
SIGLIST_CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall \
    -I ../../generated"
# _FORTIFY_SOURCE _chk tier + the environment-variable subsystem, moved
# here 2026-08-29 (static-linking cleanup, Phase 1a).
#
# Both belong in libsystem_c on real Apple, and this project already had
# part of the _chk family here (chk_fail_compat.o/strcpy_chk.o/
# strncpy_chk.o, extracted from libxpc_syscalls2.a below). The rest was
# being compiled separately into libdispatch.dylib AND libxpc.dylib, and
# the environment subsystem existed ONLY in those two -- so a process
# loading both had two setenv()/unsetenv() implementations racing over
# one real `environ` array, each with its own ___environ_lock. That is
# duplicate mutable state, not just duplicate code.
#
# SIGLIST_CFLAGS already carries -D_FORTIFY_SOURCE=0, which is
# load-bearing for the _chk files specifically and NOT cosmetic: this
# SDK defaults it to 2, which macro-rewrites each _chk implementation's
# own internal call (vsnprintf_chk.c's `vsnprintf(...)`) straight back
# into itself -- unbounded self-recursion into a stack-guard-page
# SIGSEGV. That is the exact root cause of launchd bug #6 (see
# docs/backlog.md item d11 and this tree's own postmortem); do not
# compile these without it.
#
# memmove_chk/stpncpy_chk/strncat_chk added 2026-08-30 (static-linking
# cleanup, Phase 1d): these three were the only members of the _chk
# family libsystem_c did NOT own, so libxml2.dylib and libxpc.dylib each
# compiled their own private copy from the same Libc/secure/ source.
# Same real _FORTIFY_SOURCE=0 requirement applies to them.
for f in snprintf_chk vsnprintf_chk strlcpy_chk memmove_chk stpncpy_chk strncat_chk; do
    "$CLANG" $SIGLIST_CFLAGS -I "$LIBC/secure" "$LIBC/secure/$f.c" -o "$EXTRACT/$f.o"
done
"$CLANG" $SIGLIST_CFLAGS "$LIBC/stdlib/FreeBSD/setenv.c" -o "$EXTRACT/setenv.o"
"$CLANG" $SIGLIST_CFLAGS "$LIBC/stdlib/FreeBSD/putenv.c" -o "$EXTRACT/putenv.o"
# os_assumes: the 7 real extern functions third_party/Libc/os/assumes.h's
# macros call (see os_assumes_compat.c's own header for why the real
# assumes.c body is curated rather than compiled). Moved here 2026-08-29
# (static-linking cleanup, Phase 1a): real Apple ships this in
# libsystem_c, and every one of its own dependencies (___snprintf_chk,
# ___stderrp, _abort, _fprintf, _strdup) is already local to this dylib --
# confirmed via nm, not assumed. It was previously compiled separately
# into both libdispatch.dylib and libxpc.dylib.
"$CLANG" $SIGLIST_CFLAGS -I "$LIBC" os_assumes_compat.c -o "$EXTRACT/os_assumes_compat.o"
"$CLANG" $SIGLIST_CFLAGS environ_lock_compat.c -o "$EXTRACT/environ_lock_compat.o"
"$CLANG" $SIGLIST_CFLAGS findenv_locked_compat.c -o "$EXTRACT/findenv_locked_compat.o"
# _NSGetEnviron: real Apple's setenv.c reaches `environ` through this
# indirection (crt_externs.h #defines environ to (*_NSGetEnviron())), and
# real dyld-940 in this checkout does not define it -- confirmed by
# grepping third_party/dyld, not assumed; only `_environ` itself comes
# from dyld's real libdyldGlue.o. simple_cmds_getenv_compat.c already has
# a real one, narrowed here to that single symbol via `ld -r
# -exported_symbol` because the rest of that file is this project's
# pre-dyld single-threaded getenv/setenv simplification, superseded by
# the real Apple setenv.c linked just above (same narrowing precedent as
# build_libdispatch_dylib.sh's _mach_make_memory_entry_64 extraction).
LD_FOR_NARROW="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
"$CLANG" $SIGLIST_CFLAGS simple_cmds_getenv_compat.c -o "$EXTRACT/nsgetenviron_raw.o"
"$LD_FOR_NARROW" -r -arch arm64 -exported_symbol __NSGetEnviron \
    "$EXTRACT/nsgetenviron_raw.o" -o "$EXTRACT/nsgetenviron.o"
rm -f "$EXTRACT/nsgetenviron_raw.o"

"$CLANG" $SIGLIST_CFLAGS "$LIBC/gen/FreeBSD/siglist.c" -o "$EXTRACT/siglist.o"
"$CLANG" $SIGLIST_CFLAGS "$LIBC/gen/FreeBSD/vis.c" -o "$EXTRACT/vis.o"

# Real gap: mktemp.o (kept in this dylib -- real dyld's own
# LibSystemHelpers::mkstemp() needs it, see libsystem_c_syscall_compat.c's
# own comment) needs real arc4random_uniform(). Real Apple source
# (gen/FreeBSD/arc4random.c) compiles clean under this project's
# standard -DVARIANT_STATIC (already proven by build_libresolv.sh's own
# identical compile, same real files reused verbatim, not re-derived).
"$CLANG" $SIGLIST_CFLAGS -include stdlib.h "$LIBC/gen/FreeBSD/arc4random.c" -o "$EXTRACT/arc4random.o"
"$CLANG" $SIGLIST_CFLAGS libresolv_getentropy_syscall.c -o "$EXTRACT/getentropy_syscall.o"
# NOT libresolv_arc4random_deps_compat.o here (unlike build_libresolv.sh):
# that file's real open$UNIX2003/read$UNIX2003 forwarders exist only
# because libresolv's own narrower build never otherwise pulls in a real
# definition of either -- this dylib already has one for real, from
# libc_gen_syscalls.o (part of libc_gen.a, extracted above), so
# compiling this file too would just duplicate-define the same symbols.
"$CLANG" $SIGLIST_CFLAGS libsystem_c_syscall_compat.c -o "$EXTRACT/syscall_compat.o"

# Real __cxa_atexit/__cxa_finalize -- see libsystem_c_cxa_atexit_compat.c's
# own comment (moved here from libdyld_compat_misc.c during migration
# step 5, real Apple's own architecture: __cxa_atexit lives in Libc).
"$CLANG" $SIGLIST_CFLAGS libsystem_c_cxa_atexit_compat.c -o "$EXTRACT/cxa_atexit_compat.o"

# Real gaps found booting launchd as PID 1 (docs/backlog.md item d10):
# ICU's own dylibs reference ___chkstk_darwin/strstr/os_log's real
# never-open-sourced-engine stubs directly, expected from libSystem.
"$CLANG" $SIGLIST_CFLAGS libsystem_c_gap_compat.c -o "$EXTRACT/libsystem_c_gap_compat.o"
"$CLANG" $SIGLIST_CFLAGS os_log_compat.c -o "$EXTRACT/os_log_compat.o"

# Real gap: ICU's own dylibs also reference acos/asin/fabs/sqrt/trunc/
# fmod/modf, expected from libSystem. Real, already-built Apple/FreeBSD
# Libm objects (build_libm.sh) -- extracted rather than re-compiled,
# same real selective-extraction pattern this script already uses for
# libncurses.a/libremovefile.a elsewhere in this project. pow/scalbn
# also come along for free (same source objects bundle them, see
# libm_freebsd_fallback.o) -- harmless, this is a separate dylib image
# from launchd.macho's own already-extracted libm_scalbn_only_compat.o,
# no duplicate-symbol conflict is possible across separate Mach-O images.
if [ ! -f "$OUT/libm.a" ]; then
    echo "error: $OUT/libm.a not found -- run build_libm.sh first" >&2
    exit 1
fi
(cd "$EXTRACT" && ar x "../../libm.a" acos_freeBSD.o asin_freeBSD.o libm_freebsd_fallback.o sqrt.o fmod.o)

# Real gap: ICU also references qsort, expected from libSystem. Real
# Apple source (qsort_b.c) already built once for libremovefile.a
# (build_removefile.sh) -- qsort.o itself only needs heapsort.o (also
# extracted) + flsl (small enough to define directly in
# libsystem_c_gap_compat.c above rather than pull in
# removefile_syscalls.o's own much larger, already-duplicate-prone
# object for one 6-line function).
if [ ! -f "$OUT/libremovefile.a" ]; then
    echo "error: $OUT/libremovefile.a not found -- run build_removefile.sh first" >&2
    exit 1
fi
(cd "$EXTRACT" && ar x "../../libremovefile.a" qsort.o heapsort.o)

# flsl: qsort.o (just extracted) calls it for its introsort recursion-
# depth heuristic, and this dylib had NO local definition -- it was
# binding cross-image to libremovefile.dylib, an inverted dependency
# (libsystem_c reaching UP into a libSystem sub-dylib for a 4-line
# bit-twiddle). Real Apple's Libc checkout ships no flsl.c at all, only
# the <strings.h> declaration, so this project's own last-resort
# implementation is the source -- the same one libiconv and
# removefile_syscalls.c each carried privately. Moved here 2026-08-30
# (static-linking cleanup, Phase 1d) so there is one owner: this file's
# consumer (qsort) lives here, so this is where it belongs.
"$CLANG" $SIGLIST_CFLAGS libiconv_flsl_compat.c -o "$EXTRACT/flsl.o"

# rand/srand/rand_r/sranddev + random/srandom/initstate/setstate: real
# Apple source (Libc/stdlib/FreeBSD/rand.c + random.c), already compiled
# once by build_removefile.sh -- extracted here, same real pattern as the
# libm/qsort pulls above. Moved here 2026-08-30 (static-linking cleanup,
# Phase 1d): real Apple ships the whole rand family in libsystem_c, but
# in this tree the only copies lived in libremovefile.dylib (for
# removefile_random.c's non-cryptographic buffer filler) and in a
# hand-written rand_r in cf_compat_headers/libxml2_libc_extras.c. Both
# rand.o's `_next` and random.o's state array are real MUTABLE PRNG
# state, so those were two independently-seeded generators in one
# process -- the same duplicate-state shape as g_zone/mach_task_self_,
# not merely duplicate code.
(cd "$EXTRACT" && ar x "../../libremovefile.a" rand.o random.o)

# Real gap: ICU also references mktime, expected from libSystem. Real
# Apple source (stdtime/FreeBSD/localtime.c) already compiled once for
# liblaunchd_core.a (build_launchd_core.sh's own real LOCALTIME_FLAGS
# recipe -- -fno-stack-check + a real libc-features.h regen, not
# re-derived here, just reusing the already-built object) -- extracted
# rather than recompiled, same pattern as the libm/libremovefile pulls
# above. Needs real __strcat_chk/__strcpy_chk/__strncpy_chk (also
# already-built elsewhere, pulled alongside) and asctime/asctime_r/
# __isthreaded (this file's own real definitions above).
if [ ! -f "$OUT/liblaunchd_core.a" ] || [ ! -f "$OUT/libxpc_syscalls2.a" ]; then
    echo "error: $OUT/liblaunchd_core.a or libxpc_syscalls2.a not found -- run build_launchd_core.sh first" >&2
    exit 1
fi
(cd "$EXTRACT" && ar x "../../liblaunchd_core.a" localtime.o timezone_unix03.o strcat_chk.o)
(cd "$EXTRACT" && ar x "../../libxpc_syscalls2.a" chk_fail_compat.o strcpy_chk.o strncpy_chk.o)

# uuid_unparse/uuid_unparse_lower/uuid_unparse_upper/uuid_copy/uuid_parse/
# uuid_pack: real duplicate-ownership investigation 2026-08-31
# (static-linking cleanup) -- dup_symbol_audit.sh flagged these as
# ambiguous between libcopyfile.dylib and libxpc.dylib, "neither side
# obviously real Apple's home." Checked the real source both dylibs
# actually compile: third_party/Libc/uuid/uuidsrc/{copy,parse,unparse,
# pack}.c -- genuinely part of apple-oss-distributions Libc (this
# project's own real Csu/Libc pin), i.e. real Apple's actual canonical
# home for this whole family is libsystem_c.dylib, same as every other
# Libc-sourced tier already consolidated here (env vars, _chk, os_assumes,
# etc.) -- NOT libcopyfile or libxpc, which are both usurpers. Extracted
# (not recompiled) from libcopyfile.a's own already-built copy.o/parse.o/
# unparse.o/pack.o -- same already-established "reuse a sibling archive's
# verified object" pattern as localtime.o/chk_fail_compat.o just above.
# libcopyfile.a itself is untouched (dyld.macho's real static link still
# needs it); only this dylib's own extract dir gains these 4 objects, and
# build_copyfile_dylib.sh/build_libxpc_syscalls2.sh drop their own
# extracted/compiled copies (see those files' own comments).
if [ ! -f "$OUT/libcopyfile.a" ]; then
    echo "error: $OUT/libcopyfile.a not found -- run build_copyfile.sh first" >&2
    exit 1
fi
(cd "$EXTRACT" && ar x "../../libcopyfile.a" copy.o parse.o unparse.o pack.o)

# Real gap: ICU also references time, expected from libSystem. Real
# Apple source (gen/FreeBSD/time.c) already compiled once for
# libopenpam.a (build_openpam.sh), pulled here the same real-extraction
# way as everything else above -- needs real gettimeofday()/
# fegetenv()/fesetenv() (also already built there) plus the raw
# ___gettimeofday/___commpage_gettimeofday traps that back it, and
# localtime.o (already extracted above) for the underlying date/time
# support real time.c depends on transitively.
if [ ! -f "$OUT/libopenpam.a" ]; then
    echo "error: $OUT/libopenpam.a not found -- run build_openpam.sh first" >&2
    exit 1
fi
(cd "$EXTRACT" && ar x "../../libopenpam.a" time.o gettimeofday.o ls_fenv_compat.o ps_commpage_gettimeofday_stub.o simple_cmds_time_compat.o)

# Real gap: real access()/fstatat()/ftruncate()/mkdirat()/rmdir()/
# unlink()/sigprocmask() raw syscall stubs -- same real create-
# syscalls.pl generator + recipe build_libmalloc.sh already established
# (see that script's own header comment), just never previously
# generated for these specific syscalls since nothing needed them
# before this dylib's own real API-completeness requirement (see this
# script's own header comment on why "ar x everything" surfaces gaps a
# narrower, selective static link never had to close).
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
# _mkdir added 2026-08-30 (static-linking cleanup, Phase 1d): plain
# mkdir(2) was defined only in libremovefile.dylib and libncurses.dylib,
# neither of which is its real home, and libsystem_c already generated
# its _mkdirat sibling from this very loop.
for s in _access _ftruncate _mkdir _mkdirat _sigprocmask _read; do
    "$CLANG" $SYSCFLAGS "$TMPGEN/${s}.s" -o "$EXTRACT/sys${s}.o"
done
# fstatat: real generated _fstatat.s is an empty placeholder (no
# MI_ENTRY_POINT at all, confirmed by reading the generator's own
# output, not guessed) -- the real implementation is _fstatat64.s,
# which also defines plain `_fstatat` itself as a real `.set` alias to
# `_fstatat64` (same real 64-bit-inode convention this project's other
# tiers already rely on, e.g. build_libc_gen.sh's own getdirentries64
# comment).
"$CLANG" $SYSCFLAGS "$TMPGEN/_fstatat64.s" -o "$EXTRACT/sys_fstatat64.o"
# rmdir/unlink are real cancelable syscalls (real Apple convention: the
# generator emits the raw, non-cancelable trap under a triple-underscore
# name, needing a public single-underscore alias -- same real shape
# build_libdyld.sh's own -Wl,-alias list already uses for ___kill/etc).
"$CLANG" $SYSCFLAGS "$TMPGEN/___rmdir.s" -o "$EXTRACT/sys_rmdir.o"
"$CLANG" $SYSCFLAGS "$TMPGEN/___unlink.s" -o "$EXTRACT/sys_unlink.o"

mkdir -p "$OUT/system"
rm -f "$OUT/system/libsystem_c.dylib"
# ___progname: a real per-PROCESS global that lives in the main
# executable's own crt.c (third_party/Csu/crt.c), set from argv[0] at
# startup -- NOT something libsystem_c.dylib itself should define (real
# Apple's own architecture: __progname genuinely lives in the
# executable's own crt1.o, libSystem just references it). Marked as an
# intentional dynamic lookup (same real -Wl,-U mechanism
# _dyld_initializer already uses), resolved at runtime against whatever
# process image actually defines it.
# REAL BUG FOUND 2026-08-28 (build_simple_cmds.sh's real-dylib-consumer
# conversion): -Wl,-alias only lands in a dylib's real LC_DYLD_EXPORTS_TRIE
# (confirmed via `xcrun dyld_info -exports`, not nm -- nm's plain symbol
# table listing is misleading here, it shows alias entries regardless of
# whether they actually made it into the trie) when the alias's BASE
# symbol is genuinely LOCAL to THIS SAME ld64 invocation. Adding an alias
# for e.g. _fputs/_strerror to libSystem.B.dylib's own build (where they
# are only reachable via -reexport_library, not local) silently produces
# an alias no real consumer can ever resolve -- confirmed via a real
# trial link that failed with "symbol not found" despite `nm -gU`
# claiming the alias existed. Every base below IS local here ($EXTRACT/
# *.o, from libc_stdio.a's real fputs.c etc), so this dylib -- not
# libSystem.B.dylib -- is the correct place for all of them.
#
# _fsync/_reallocarray REMOVED from this list 2026-08-28: this dylib's
# own $EXTRACT/*.o never actually defined either base (confirmed via a
# real `nm` sweep) -- the exact same "silently dead, only tolerated by
# ld64-609" class of bug as above, just missed in the original cleanup.
# The daily-driver Xcode's newer ld (NEWLD_BINDIR below) correctly hard-
# fails on it instead of staying silent. Not a functional loss: real,
# working `_fsync$UNIX2003`/`_reallocarray$DARWIN_EXTSN` aliases already
# exist in `build_libdyld.sh` and `build_libsystem.sh` (confirmed via
# `xcrun dyld_info -exports`, both real), and `less.macho` (this
# project's one real fsync(2) consumer) already binds to `libdyld.dylib`
# for it at real chained-fixups load time (confirmed via
# `xcrun dyld_info -fixups`), not this dylib -- removing the dead copy
# here changes nothing observable. `__pthread_cond_wait` (aliased to
# plain `_pthread_cond_wait`) removed the same way: not local here
# either, and plain `_pthread_cond_wait` is already a real, direct
# export of the sibling `libsystem_pthread.dylib` this link already
# includes -- any real caller resolves it via normal two-level-
# namespace lookup with no alias needed at all.
    # -not_for_dyld_shared_cache: real, newly-surfaced requirement of
    # the daily-driver Xcode's newer ld (NEWLD_BINDIR above) -- it
    # refuses "-undefined dynamic_lookup"/-U on a dylib it classifies
    # as shared-cache eligible (real /usr/lib install path) unless this
    # is set, a real check Xcode 12's ld64-609 never enforced. This
    # project builds no real dyld shared cache at all, so opting every
    # real dylib out is correct, not a workaround (docs/backlog.md item
    # d9's NEWLD_BINDIR sweep, 2026-08-28).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libsystem_c.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-U,___progname \
    -Wl,-alias,___kill,_kill \
    -Wl,-alias,___rmdir,_rmdir \
    -Wl,-alias,___unlink,_unlink \
    -Wl,-alias,___kill,'_kill$UNIX2003' \
    -Wl,-alias,_mprotect,'_mprotect$UNIX2003' \
    -Wl,-alias,_write,'_write$UNIX2003' \
    -Wl,-alias,_open,'_open$UNIX2003' \
    -Wl,-alias,_close,'_close$UNIX2003' \
    -Wl,-alias,_fcntl,'_fcntl$UNIX2003' \
    -Wl,-alias,_fstat,'_fstat$INODE64' \
    -Wl,-alias,_lstat,'_lstat$INODE64' \
    -Wl,-alias,_stat,'_stat$INODE64' \
    -Wl,-alias,_read,'_read$UNIX2003' \
    -Wl,-alias,'_mmap$UNIX2003',_mmap \
    -Wl,-alias,'_munmap$UNIX2003',_munmap \
    -Wl,-alias,_pthread_testcancel,'_pthread_testcancel$UNIX2003' \
    -Wl,-alias,'_pthread_rwlock_wrlock$UNIX2003',_pthread_rwlock_wrlock \
    -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003',_pthread_rwlock_rdlock \
    -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003',_pthread_rwlock_unlock \
    -Wl,-alias,_fputs,'_fputs$UNIX2003' \
    -Wl,-alias,_strerror,'_strerror$UNIX2003' \
    -Wl,-alias,_fwrite,'_fwrite$UNIX2003' \
    -Wl,-alias,_fstatfs,'_fstatfs$INODE64' \
    -Wl,-alias,_fopen,'_fopen$UNIX2003' \
    -Wl,-alias,_fstatat,'_fstatat$INODE64' \
    -Wl,-alias,_closedir,'_closedir$UNIX2003' \
    -Wl,-alias,_opendir,'_opendir$INODE64$UNIX2003' \
    -Wl,-alias,_readdir,'_readdir$INODE64' \
    -Wl,-alias,_readdir_r,'_readdir_r$INODE64' \
    -Wl,-alias,_fdopendir,'_fdopendir$INODE64$UNIX2003' \
    -Wl,-alias,_regcomp,'_regcomp$UNIX2003' \
    -Wl,-alias,_fdopen,'_fdopen$UNIX2003' \
    -Wl,-alias,_strtod,'_strtod$UNIX2003' \
    -Wl,-alias,___strtok_r,_strtok_r \
    `# Plain, non-$UNIX2003 spellings of the environment setters. This` \
    `# dylib compiles real Apple setenv.c with -D__DARWIN_UNIX03=1, which` \
    `# emits ONLY the asm-labeled $UNIX2003 names, but real callers` \
    `# (libxpc.dylib's own xpc objects among them) reference the plain` \
    `# ones. Each alias base below is genuinely local to this link` \
    `# ($EXTRACT/setenv.o) -- required, see docs/backlog.md item d10 for` \
    `# the ld64 bug where a non-local alias base silently produces a` \
    `# malformed ordinal-0 re-export instead of an error.` \
    -Wl,-alias,'_setenv$UNIX2003',_setenv \
    -Wl,-alias,'_unsetenv$UNIX2003',_unsetenv \
    -Wl,-alias,'_putenv$UNIX2003',_putenv \
    -o "$OUT/system/libsystem_c.dylib" \
    "$EXTRACT"/*.o \
    "$OUT/system/libsystem_kernel.dylib" "$OUT/system/libsystem_platform.dylib" \
    "$OUT/system/libsystem_malloc.dylib" "$OUT/system/libsystem_pthread.dylib" "$OUT/system/libsystem_blocks.dylib" \
    "$OUT/system/libremovefile.dylib" \
    -Wl,-reexport_library,"$OUT/system/libdyld.dylib"
    # ^ REAL BUG FOUND+FIXED 2026-08-28 (static-linking cleanup pass,
    # Stage B): this dylib's own qsort.o (extracted from libremovefile.a
    # above, real BSD qsort) directly calls real `flsl()` -- also from
    # libremovefile.a's own real source, now libremovefile.dylib
    # (docs/backlog.md item d8). Per this project's own already-
    # documented rule (d9: "any sub-dylib whose real, non-aliased
    # symbols a consumer's own compiled code calls directly must also be
    # linked directly on that consumer's own command line"), relying on
    # the transitive `libsystem_c.dylib -> libdyld.dylib (reexport) ->
    # libremovefile.dylib (reexport)` chain alone is NOT enough here: a
    # real, separate bug surfaced trying that -- `-isysroot`-relative
    # transitive dependency resolution for libdyld.dylib's own real
    # `/usr/lib/libresolv.9.dylib` LC_LOAD_DYLIB path silently picked up
    # the daily-driver Xcode SDK's own REAL stub `libresolv.9.dylib`
    # (confirmed via ld's own "text-based stub file ... and library file
    # ... unexpectedly found" warning) instead of this project's actual
    # `libc_build/system/libresolv.dylib` -- a real install_name/SDK-path
    # collision, not a fabricated concern (this project's own dylib
    # legitimately reuses real Apple's own absolute install_name paths).
    # Linking libremovefile.dylib directly here sidesteps that ambiguity
    # entirely for the one symbol this dylib's own code actually calls.
    # ^ real gap found booting launchd as PID 1 (docs/backlog.md item
    # d10): ICU's own libicuuc.66.dylib expects _dlclose specifically
    # from libSystem (its own two-level-namespace bind, baked in at
    # ICU's build time) even though _dlopen/_dlsym happened to already
    # resolve via libc++.dylib being a sibling -- matches real Apple's
    # actual architecture (libSystem.B.dylib really does reexport
    # dyld's dl* API on real macOS, this was simply never wired here).

echo "wrote $OUT/system/libsystem_c.dylib"
otool -hv "$OUT/system/libsystem_c.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
if nm -u "$OUT/system/libsystem_c.dylib" | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u "$OUT/system/libsystem_c.dylib" >&2
else
    echo "libsystem_c.dylib: fully self-contained (beyond libsystem_kernel/platform/malloc/pthread/blocks.dylib), zero undefined symbols"
fi
