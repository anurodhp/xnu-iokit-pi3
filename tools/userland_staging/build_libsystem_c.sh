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
# 2026-09-14: rephrased from one combined generic-message check into
# individual "run build_X.sh first" checks so gen_build_order.py's
# parser can actually detect these real dependencies (its idiom-1
# regex needs a literal per-script phrase, not a chained `||` with one
# shared generic message -- the combined form was invisible to it,
# which is how libsystem_pthread ended up scheduled AFTER this script
# despite this real requirement, only caught by actually running the
# scripts in order rather than trusting build_everything.sh alone).
# libdyld.dylib deliberately dropped from this list: this dylib used to
# link/reexport it directly, which made libsystem_c.sh <-> libdyld.sh a
# real cycle (libdyld.sh's own dependency on libsystem_c.dylib -- for
# real, direct symbol needs -- is genuine and unavoidable). Fixed by
# moving the dyld-API reexport up to the real libSystem.B.dylib umbrella
# (build_libsystem.sh) instead, matching real Apple architecture (the
# umbrella reexports libdyld.dylib, not libsystem_c.dylib doing it
# prematurely) -- see build_libsystem.sh's own comment on that reexport.
if [ ! -f "$OUT/system/libsystem_kernel.dylib" ]; then
    echo "error: $OUT/system/libsystem_kernel.dylib not found -- run build_libsystem_kernel.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libsystem_platform.dylib" ]; then
    echo "error: $OUT/system/libsystem_platform.dylib not found -- run build_libsystem_platform.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libsystem_malloc.dylib" ]; then
    echo "error: $OUT/system/libsystem_malloc.dylib not found -- run build_libsystem_malloc.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libsystem_pthread.dylib" ]; then
    echo "error: $OUT/system/libsystem_pthread.dylib not found -- run build_libsystem_pthread.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libsystem_blocks.dylib" ]; then
    echo "error: $OUT/system/libsystem_blocks.dylib not found -- run build_libsystem_blocks.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/libncurses.a" ]; then
    echo "error: $OUT/libncurses.a not found -- run build_ncurses.sh first (this script reuses its already-built Libc_sys_sigaction.o/libplatform_sigaction.o members)" >&2
    exit 1
fi

rm -rf "$EXTRACT"
mkdir -p "$EXTRACT"
(cd "$EXTRACT" && ar x "../../libc_string.a")
(cd "$EXTRACT" && ar x "../../libc_gen.a")
(cd "$EXTRACT" && ar x "../../libc_stdio.a")
(cd "$EXTRACT" && ar x "../../libc_locale_for_stdio.a")
# Libc_sys_sigaction.o/libplatform_sigaction.o (DAR-164, real gap found
# root-causing syslogd/pd-console-login's post-exec abort -- see
# build_libsystem_kernel.sh's own updated comment for the full real
# kernel-side-probe trail): real Apple's public sigaction()/
# _sigaction_nobind() (Libc/sys/sigaction.c) and libplatform's
# __platform_sigaction() (libplatform/src/setjmp/generic/sigaction.c)
# were never part of the archive set this dylib's own header already
# lists (libc_string/libc_gen/libc_stdio/libc_printf/
# libc_locale_for_stdio/libc_regex) -- sys/sigaction.c is architecturally
# real Libc, just never one of THIS dylib's already-combined tiers.
# Both objects are already real and built (extracted here from
# libncurses.a, same real source, same real selective-extraction pattern
# build_launchd_core.sh's own launchd_libc_extras_obj already established
# for this exact pair) -- not recompiled. Needs the raw ___sigaction trap
# + __sigtramp trampoline, both real exports of the sibling
# libsystem_kernel.dylib linked below by this same fix.
(cd "$EXTRACT" && ar x "../../libncurses.a" Libc_sys_sigaction.o libplatform_sigaction.o)
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
# memcpy.o/memset.o dropped 2026-09-04 (DAR-13, real-Apple-parity fix):
# real Apple never defines plain _memcpy/_memset in libsystem_c.dylib --
# it resolves through libSystem.B.dylib's umbrella reexport to
# libsystem_platform.dylib's one real copy (see build_libsystem_platform.sh's
# own updated header comment for the full real-source trail). Placed
# AFTER both the libc_string.a AND libc_locale_for_stdio.a extractions
# above (not right after the first `ar x`) because this script's own
# header comment already documents that locale's own memcpy.o/memset.o
# copies silently overwrite string's on disk (last-extracted-wins) --
# removing right after the FIRST extraction would have left locale's
# copy behind. Every other object in $EXTRACT that calls plain
# memcpy()/memset() internally (compiler-generated struct copies,
# stdio/regex/locale source, etc) now resolves externally to the
# libsystem_platform.dylib sibling already linked below (real, direct
# link, not just transitive) -- same real "one owner per symbol" fix
# class as this dylib's own getenv/_NSGetEnviron narrowing above.
rm -f "$EXTRACT/memcpy.o" "$EXTRACT/memset.o"
# ldpart.o dropped 2026-09-01 (DAR-153/DAR-164): a real, newly-arrived
# member of libc_locale_for_stdio.a (real Libc/locale/FreeBSD/ldpart.c,
# __ldpart_free_extra + __part_load_locale) this dylib's own build was
# never updated for. __part_load_locale needs a real __open_path_locale
# this project's whole third_party/Libc checkout doesn't have anywhere
# (see build_openpam_dylib.sh's own header on this exact, already-
# documented wall) and nothing in this project's dylib graph calls it
# (confirmed via nm -u). __ldpart_free_extra itself IS real and needed
# (real callers in libopenpam.dylib), but its own real definition is
# __private_extern__ -- dropped here and given a real, genuinely
# exported second home instead (libsystem_c_ldpart_free_extra_compat.c,
# aliased to the real public name below) rather than trying to force-
# export ldpart.o's own hidden copy, which -Wl,-exported_symbol does not
# actually do for private-extern symbols (tried first, see that compat
# file's own header for the real confirmed failure).
rm -f "$EXTRACT/ldpart.o"

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

# DAR-346: real getlogin()/getlogin_r() and setlogin() -- Libc's own home
# for both. setlogin.c clears getlogin.c's cached __logname under
# __logname_mutex, so the two must live in the same image. Their raw
# traps (___getlogin/___setlogin) are in libsystem_kernel.dylib.
"$CLANG" $SIGLIST_CFLAGS -I "$LIBC/fbsdcompat" -I "$LIBC/darwin" \
    "$LIBC/gen/FreeBSD/getlogin.c" -o "$EXTRACT/getlogin.o"
"$CLANG" $SIGLIST_CFLAGS "$LIBC/gen/setlogin.c" -o "$EXTRACT/setlogin.o"
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
# _environ (the actual global storage, not _NSGetEnviron's indirection
# below): real gap, found 2026-09-14 fixing the libsystem_c.sh <->
# libdyld.sh ordering cycle. The comment this replaced claimed
# "_environ itself comes from dyld's real libdyldGlue.o" -- checked
# directly against third_party/dyld's real, pinned source and that's
# not true: libdyldGlue.cpp only ever USES `&environ` (real Apple's own
# ProgramVars-struct plumbing), never DEFINES the actual storage
# anywhere in this checkout, confirmed via a real grep across the whole
# dyld tree, not assumed. On real macOS, dyld's own entry-point code
# populates this global before any dylib's code runs; this project's
# dyld doesn't implement that yet (see third_party/Csu/start.s's own
# extensive comment on the parallel gap for statically-linked targets,
# `CRT_DYNAMIC_LINKING`) -- a real, honestly-flagged architecture gap,
# not fabricated completeness. Until real dyld-side ProgramVars support
# exists, this dylib owns the actual storage directly (it's the first,
# and so far only, real consumer needing it).
#
# REAL BUG FOUND+FIXED 2026-09-14 (found via a real QEMU boot +
# -user_fault_debug, launchd's own real main() -> launchd_runtime_init()
# returns, then `if (NULL == getenv("PATH")) setenv("PATH", ...)`
# crashed with EXC_BAD_ACCESS, far 0x0, backtrace confirming the fault
# was inside real Apple setenv.c called from launchd's real main()):
# the initial value here was a null POINTER (`environ = 0`), but real
# Apple's setenv.c/getenv.c (third_party/Libc/stdlib/FreeBSD/*.c,
# already linked into this dylib) never NULL-checks `environ` itself --
# every real implementation assumes it always points to AT LEAST a
# one-element, NULL-terminated array (a real, empty envp), the same
# invariant a real exec()'d process's envp always satisfies even with
# zero real variables. `environ = 0` is a null pointer to nothing,
# so the very first real environ[N] scan inside setenv()/getenv()
# dereferences NULL directly. Fixed by pointing environ at a real
# static one-element `{ NULL }` array instead -- a real, minimal,
# correctly-shaped empty environment, not a stub.
cat > "$OUT/environ_storage_compat.c" <<'EOF'
static char *empty_environ[1] = { 0 };
char **environ = empty_environ;
EOF
"$CLANG" $SIGLIST_CFLAGS "$OUT/environ_storage_compat.c" -o "$EXTRACT/environ_storage_compat.o"

# _NSGetEnviron: real Apple's setenv.c reaches `environ` through this
# indirection (crt_externs.h #defines environ to (*_NSGetEnviron())).
# simple_cmds_getenv_compat.c already has a real one, narrowed here to
# that single symbol via `ld -r -exported_symbol` because the rest of
# that file is this project's pre-dyld single-threaded getenv/setenv
# simplification, superseded by the real Apple setenv.c linked just
# above (same narrowing precedent as build_libdispatch_dylib.sh's
# _mach_make_memory_entry_64 extraction).
LD_FOR_NARROW="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
"$CLANG" $SIGLIST_CFLAGS simple_cmds_getenv_compat.c -o "$EXTRACT/nsgetenviron_raw.o"
"$LD_FOR_NARROW" -r -arch arm64 -exported_symbol __NSGetEnviron \
    "$EXTRACT/nsgetenviron_raw.o" -o "$EXTRACT/nsgetenviron.o"
rm -f "$EXTRACT/nsgetenviron_raw.o"

# DAR-153 (2026-09-02): real, working getenv() -- this dylib's ONLY
# other getenv() definition is libc_locale_for_stdio.a's
# libc_locale_env_stubs.o, whose `__attribute__((weak)) getenv()`
# unconditionally `return 0` (written for a pre-dyld, no-envp target;
# see that file's own header comment). Real Apple's setenv.c (compiled
# above into this exact dylib) DOES carry a real, correct getenv() body,
# but it's `__private_extern__` in real Apple source -- confirmed via
# `dyld_info -exports libsystem_c.dylib` showing `_getenv [weak-def]` at
# libc_locale_env_stubs.o's address, NOT setenv.c's -- and this project
# already has the "-Wl,-exported_symbol does not un-hide a private
# extern" lesson on record (___ldpart_free_extra, this same script).
# Real, concrete, QEMU-confirmed consequence: since the always-NULL weak
# stub is the one symbol table entry that wins the dylib's public export
# slot, EVERY process on this port calling the real, dylib-bound
# getenv() -- not just launchd/syslogd -- always got NULL regardless of
# what was actually in its real environ (DAR-153 investigation: launchd
# genuinely appended LAUNCHD_TRUSTED_FD_ENV to the real, now-correctly-
# unified `environ` -- see build_launchd.sh's own -DCRT_DYNAMIC_LINKING
# fix -- but syslogd's real getenv(LAUNCHD_TRUSTED_FD_ENV) call still
# read back NULL).
#
# Fix: same narrowing technique as _NSGetEnviron immediately above, same
# source file. simple_cmds_getenv_compat.c's own getenv() is a real,
# correct, already-in-tree POSIX getenv(3) reading the real `environ`
# global (not fabricated for this fix) -- extracting it here as a
# separate, genuinely STRONG (non-weak) `_getenv` object makes ld64
# prefer it over libc_locale_env_stubs.o's weak stub for the exported
# symbol, exactly the override mechanism that stub's own header comment
# already documented as the intended design (same precedent as
# pthread_tsd_shim.c's default-stub-overridden-by-strong-definition
# pattern) -- it just never had a real strong getenv() in this dylib's
# own link to do the overriding until now.
"$CLANG" $SIGLIST_CFLAGS simple_cmds_getenv_compat.c -o "$EXTRACT/getenv_real_raw.o"
"$LD_FOR_NARROW" -r -arch arm64 -exported_symbol _getenv \
    "$EXTRACT/getenv_real_raw.o" -o "$EXTRACT/getenv_real.o"
rm -f "$EXTRACT/getenv_real_raw.o"

"$CLANG" $SIGLIST_CFLAGS "$LIBC/gen/FreeBSD/siglist.c" -o "$EXTRACT/siglist.o"
"$CLANG" $SIGLIST_CFLAGS "$LIBC/gen/FreeBSD/vis.c" -o "$EXTRACT/vis.o"

# __ldpart_free_extra's real, genuinely-exported second home -- see
# ldpart.o's own removal comment above and this compat file's own header
# for the full real trail (DAR-153/DAR-164).
"$CLANG" $SIGLIST_CFLAGS libsystem_c_ldpart_free_extra_compat.c -o "$EXTRACT/ldpart_free_extra_compat.o"

# Real gap: mktemp.o (kept in this dylib -- real dyld's own
# LibSystemHelpers::mkstemp() needs it, see libsystem_c_syscall_compat.c's
# own comment) needs real arc4random_uniform(). Real Apple source
# (gen/FreeBSD/arc4random.c) compiles clean under this project's
# standard -DVARIANT_STATIC (already proven by build_libresolv.sh's own
# identical compile, same real files reused verbatim, not re-derived).
#
# DAR-247: needs three real extra -I paths (os/assumes.h itself, plus
# _simple.h and os/base_private.h it pulls in) and, scoped to ONLY this
# one file, arc4random_compat_include's TargetConditionals.h override --
# see that file's own header for the full real chain (an unreachable
# os_log/firehose parse-time dependency, not a runtime one).
"$CLANG" $SIGLIST_CFLAGS -I arc4random_compat_include -I "$LIBC" \
    -I ../../third_party/libplatform/private -I "$XNU/libkern" \
    -include stdlib.h "$LIBC/gen/FreeBSD/arc4random.c" -o "$EXTRACT/arc4random.o"
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

# DAR-16, 2026-09-04: acos/asin/fabs/fmod/modf/pow/scalbn/sqrt/trunc are
# NO LONGER extracted here. Real Apple layering (confirmed via the
# pinned apple-oss-distributions/Libsystem-1292.60.1 `requiredlibs`,
# commit 8db6779): libsystem_m.dylib is these 9 symbols' one real owner,
# and libsystem_m.dylib itself links libsystem_c.dylib as a sibling --
# so libsystem_c CANNOT own them too without being a real, live
# duplicate (confirmed single-source: both dylibs used to extract the
# exact same 5 libm.a objects -- acos_freeBSD.o/asin_freeBSD.o/
# libm_freebsd_fallback.o/sqrt.o/fmod.o -- byte-for-byte, not two
# independent implementations).
#
# CONSOLIDATION ONTO libsystem_m.dylib ATTEMPTED, REVERTED 2026-08-31,
# RETRIED AND CLOSED 2026-09-04 -- the 2026-08-31 attempt found a real,
# severe blocker: reexporting libsystem_m.dylib through libSystem.B.dylib
# makes it a REQUIRED load dependency of every process, and this
# project's dyld does only eager chained-fixups binding, so
# libsystem_m.dylib's own real, pre-existing `-Wl,-U` flat-namespace
# deferrals (`__muldc3`/`__mulsc3` + 13 unported float-precision
# entries) became boot-blocking -- PID 1 panicked, "symbol not found in
# flat namespace '__muldc3'". That prerequisite is what DAR-124 (also
# 2026-08-31/2026-09-01) closed for a different reason (a dup-symbol
# sweep on libsystem_m.dylib itself): `complex.o`/`scalblnf.o`, the
# ONLY two objects needing those 15 deferred symbols, were confirmed to
# have zero real callers anywhere in this project and dropped from
# build_libm_dylib.sh's own link. Verified fresh before retrying this
# fix: `xcrun dyld_info -fixups libc_build/system/libsystem_m.dylib |
# grep -c flat-namespace` -> 0. With that prerequisite genuinely gone,
# the reexport (`build_libsystem.sh`) no longer panics -- real QEMU
# boot test PASS, see that commit's own message for the log path.
#
# This dylib's own 2 internal callers of asin()/sqrt()
# (libsystem_c_gap_compat.o's atan(x) == asin(x / sqrt(1+x*x)), see that
# file's own header) cannot link libsystem_m.dylib directly -- that
# would be the real circular BUILD dependency this project's own layering
# forbids (libsystem_m links libsystem_c, not the reverse). Deferred via
# `-Wl,-U` below instead: safe specifically because libsystem_m.dylib is
# now unconditionally co-loaded with this dylib (both reexported through
# the same libSystem.B.dylib umbrella every process links), so the flat-
# namespace bind always finds a real provider at load time -- unlike the
# `__muldc3` case above, this is not a dormant, unprovided gap.
if [ ! -f "$OUT/libm.a" ]; then
    echo "error: $OUT/libm.a not found -- run build_libm.sh first" >&2
    exit 1
fi

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
# localtime.o/timezone_unix03.o/strcat_chk.o compiled directly here
# rather than extracted from liblaunchd_core.a, found 2026-09-13
# (DAR-301 follow-on): extracting from the full archive made this
# script depend on build_launchd_core.sh, which itself requires
# build_corefoundation.sh, which requires build_libxml2.sh, which
# requires THIS script's own libsystem_c.dylib -- a genuine, real
# 4-way cycle (build_libsystem_c.sh -> build_launchd_core.sh ->
# build_corefoundation.sh -> build_libxml2.sh -> build_libsystem_c.sh),
# confirmed real in every link (grep), not a parser artifact. It was
# invisible until this session's ordering-parser fixes correctly
# recognized enough real dependencies simultaneously to expose it.
# Only 3 real, self-contained, zero-cross-dependency Apple source
# files were actually needed (never the rest of liblaunchd_core.a) --
# same real LOCALTIME_FLAGS recipe build_launchd_core.sh's own comment
# already documents in detail, copied here rather than re-derived, to
# break the cycle instead of routing around it with a skip (unlike
# this session's other real cycles, skipping these three would ship an
# incomplete libsystem_c.dylib missing real localtime/timezone/
# strcat_chk symbols other real code calls unconditionally).
if [ ! -f "$OUT/libxpc_syscalls2.a" ]; then
    echo "error: $OUT/libxpc_syscalls2.a not found -- run build_libxpc_syscalls2.sh first" >&2
    exit 1
fi
mkdir -p "$OUT/libc_features_gen"
ARCHS=arm64 VARIANT_PLATFORM_NAME=iphoneos SRCROOT="$(cd libc_features_platform && pwd)" \
    DERIVED_FILES_DIR="$(cd "$OUT" && pwd)/libc_features_gen" \
    perl "$LIBC/xcodescripts/generate_features.pl" >/dev/null
LOCALTIME_FLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -fno-stack-check \
    -include localtime_private_decls.h \
    -I "$LIBC/include" -I "$OUT/libc_features_gen/arm64" -I "$PTHREAD/private" \
    -idirafter "$LIBC/fbsdcompat" -idirafter "$LIBC/darwin" -idirafter "$XNU/bsd" -idirafter "$XNU/osfmk")
"$CLANG" "${LOCALTIME_FLAGS[@]}" "$LIBC/stdtime/FreeBSD/localtime.c" -o "$EXTRACT/localtime.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -DPRIVATE=1 \
    -D_FORTIFY_SOURCE=0 "$LIBC/secure/strcat_chk.c" -o "$EXTRACT/strcat_chk.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -DPRIVATE=1 \
    "$LIBC/stdtime/timezone_unix03.c" -o "$EXTRACT/timezone_unix03.o"
(cd "$EXTRACT" && ar x "../../libxpc_syscalls2.a" chk_fail_compat.o strcpy_chk.o strncpy_chk.o)

# darwin_check_fd_set_overflow_compat.o added 2026-08-31 (static-linking
# cleanup, "needs a new home" batch): same libxpc_syscalls2.a archive as
# the _chk tier just above, same real reasoning -- real Apple's
# `__darwin_check_fd_set_overflow()` is part of the same `_FORTIFY_SOURCE`
# hardening family (the FD_SET/FD_CLR/FD_ISSET bounds check), and this
# project's own curated copy (darwin_check_fd_set_overflow_compat.c,
# already compiled into this archive by build_libxpc_syscalls2.sh) had no
# real single owner before this: libxpc.dylib and libncurses.dylib each
# independently carried their own copy, and libresolv.dylib's own link
# already deferred to a flat-namespace `-Wl,-U` HOPING some loaded image
# would provide it. Making libsystem_c.dylib -- the dylib every one of
# those three already links as a sibling and the same real _chk-family
# home as strcpy_chk.o/etc above -- the one real exporter closes all
# three at once (see build_libxpc_dylib.sh's and build_ncurses_dylib.sh's
# own updated comments for their halves of this fix).
(cd "$EXTRACT" && ar x "../../libxpc_syscalls2.a" darwin_check_fd_set_overflow_compat.o)

# sleep()/_sleep: new file, not extracted from an existing archive --
# see libsystem_c_sleep_compat.c's own header for the full real-owner
# investigation (2026-08-31, "needs a new home" batch). Needs plain
# `nanosleep()`, a real export of the libsystem_kernel.dylib sibling
# linked below as of this same batch.
"$CLANG" $SIGLIST_CFLAGS libsystem_c_sleep_compat.c -o "$EXTRACT/sleep_compat.o"

# strftime/strftime_l: real Apple source (stdtime/FreeBSD/strftime.c),
# newly compiled here 2026-08-31 (static-linking cleanup, locale/
# timezone-string consolidation batch) -- this dylib had NO copy of
# strftime() at all before this (a real, pre-existing gap; libc++.dylib
# and libopenpam.dylib each independently compiled their own, making
# THEM the accidental owners of a real Apple libsystem_c export). Round
# 8 (2026-08-31, continuation 6) investigated removing localtime.o/
# timezone_unix03.o from those two siblings and found it structurally
# blocked: strftime.o (kept resident in both) directly calls
# __st_get_timezone (timezone_unix03.o) and ___darwin_altzone
# (localtime.o), both real Apple `.private_extern` symbols that cannot
# cross a dylib boundary -- so as long as strftime.o stayed resident
# there, its two private-extern dependencies had to stay right beside
# it. This session's fix moves the WHOLE coupled trio here instead of
# leaving strftime.o behind: once strftime.o is compiled into THIS
# image, its private-extern refs resolve locally (this dylib already
# carries localtime.o/timezone_unix03.o for the reason above, plus
# xlocale.o/ldpart.o/timelocal.o for __global_locale/__locale_key/
# __get_current_time_locale, and fprintf.o/sprintf.o for
# _fprintf_l/_sprintf_l -- confirmed via a real trial compile, every
# other undefined symbol strftime.o needs is already resident here).
# Compiled with the same $SIGLIST_CFLAGS (__DARWIN_UNIX03=1) recipe
# build_libcxx_dylib.sh/build_openpam_dylib.sh already use for this
# exact file, so the exported symbol names match what those sibling
# dylibs' consumers already expect byte-for-byte
# (_strftime$UNIX2003/_strftime_l$UNIX2003/_strftime_l) -- no relink
# needed anywhere else just to keep the same names, only to repoint
# which dylib owns them.
"$CLANG" $SIGLIST_CFLAGS "$LIBC/stdtime/FreeBSD/strftime.c" -o "$EXTRACT/strftime.o"

# asctime/asctime_r: real Apple/FreeBSD source (stdtime/FreeBSD/
# asctime.c), replacing this dylib's own hand-rolled stand-in
# (libsystem_c_gap_compat.c's former asctime()/asctime_r(), removed --
# see that file's own updated comment) 2026-08-31 (static-linking
# cleanup, locale/timezone-string consolidation batch, closes DAR-134).
# DAR-134's own real blocker was exactly this: asctime_r() calls
# strftime_l() internally just to format the 4-digit year, and this
# dylib had no strftime_l() until the strftime.o compiled just above
# (for the unrelated locale/timezone-string consolidation this session
# is otherwise doing). Re-tested per DAR-134's own "may already just
# work like atol/strtoll/strtoul did" prediction -- confirmed via a real
# trial compile: asctime.c's only undefined refs (___error/_sprintf/
# _strcpy/_strftime_l$UNIX2003/_strlen) are all already resident here.
# Now that this copy is the same real FreeBSD source as libc++.dylib's/
# libopenpam.dylib's own (no longer an inferior stub), this session also
# removed THEIR asctime.o -- the old "real vs stub, don't consolidate"
# exception no longer applies -- so this is now the one real owner, same
# as the rest of the family above.
"$CLANG" $SIGLIST_CFLAGS "$LIBC/stdtime/FreeBSD/asctime.c" -o "$EXTRACT/asctime.o"

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
# uuid_clear: DAR-287 (2026-09-11) completes that same family. clear.c is
# the one member nothing in this project had ever compiled anywhere --
# libcopyfile.a does not build it, so there is no object to `ar x` above,
# and `nm -gU` across every built dylib found _uuid_copy/_uuid_pack/
# _uuid_parse/_uuid_unparse{,_lower,_upper} but no _uuid_clear at all.
# SystemConfiguration.fproj/SCProxies.c really calls uuid_clear(3);
# DAR-283 compiled clear.c privately into scutil.macho rather than
# rebuilding this dylib, which is exactly the ownership debt DAR-287
# closes -- the canonical home is right here, per this block's own
# argument above.
#
# Given its own compile step, with one define the rest of this script
# does not set: uuidP.h falls back to <uuid/uuid_types.h> (a header
# present in no SDK or pin here) unless HAVE_STDINT_H is defined.
# Confirmed still true against the real pinned header, not carried over
# on faith. Same define build_copyfile.sh already passes for the rest of
# this family; scoped to this one file so it cannot change how anything
# else in this build compiles.
"$CLANG" $SIGLIST_CFLAGS -DHAVE_STDINT_H=1 "$LIBC/uuid/uuidsrc/clear.c" -o "$EXTRACT/clear.o"

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
# _execve added 2026-09-17 (DAR-292): raw execve(2) trap, real public
# name straight out of the generator (`MI_ENTRY_POINT(_execve)`, no
# triple-underscore raw/public split the way ___wait4_nocancel below
# needs) -- exec.c's own real `_execve(...)` calls resolve to this
# directly, no alias needed.
for s in _access _ftruncate _mkdir _mkdirat _sigprocmask _read _execve; do
    "$CLANG" $SYSCFLAGS "$TMPGEN/${s}.s" -o "$EXTRACT/sys${s}.o"
done
# __wait4/__wait4_nocancel added 2026-09-17 (DAR-292): real waitpid.c
# (compiled below) is built with this dylib's own SIGLIST_CFLAGS, which
# already sets -DVARIANT_CANCELABLE -- so it takes waitpid.c's
# `#ifdef VARIANT_CANCELABLE` branch and calls `__wait4` (cancelable),
# NOT `__wait4_nocancel`. Real trial compile confirmed this the hard
# way: a first attempt compiled only the _nocancel raw trap and the link
# failed with "___wait4, referenced from: ___waitpid in waitpid.o".
# ___wait4_nocancel is compiled anyway (unused by waitpid.c here, but
# real and harmless) purely because the generator auto-appends a real
# `.globl _wait4 / .set _wait4, ___wait4_nocancel` alias to that one
# file specifically (confirmed by reading the generated .s, not
# assumed) -- so this one extra compile gives this dylib a real, public
# plain `wait4()` for free, matching real Apple's own export surface.
"$CLANG" $SYSCFLAGS "$TMPGEN/___wait4.s" -o "$EXTRACT/sys___wait4.o"
"$CLANG" $SYSCFLAGS "$TMPGEN/___wait4_nocancel.s" -o "$EXTRACT/sys___wait4_nocancel.o"
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

# fork()/waitpid()/execlp() real canonical home (DAR-292, 2026-09-17):
# split out of DAR-286, where scutil/libedit's own real `-Wl,-U,_fork`
# flat-namespace deferral only worked because libSystemConfiguration.dylib
# happened to incidentally export `_fork` from configd's own compat
# objects, not from being the designated owner (see that script's own
# header). 84 of 101 shipped .macho binaries carry a private static copy
# of `_fork` today (re-measured 2026-09-17 via `nm` across every built
# .macho -- the ticket's own 2026-09-11 count of 79 is stale, 5 more
# since then) -- this dylib is now the one real, canonical owner for
# new/converted consumers; the 84 are real, separate follow-on sweep
# work (DAR-292's own Linear comment has the concrete recipe, including
# the single real leverage point for most of them).
#
# waitpid.c/exec.c are real, unmodified Apple/FreeBSD source
# (Libc/gen/FreeBSD/waitpid.c, Libc/gen/FreeBSD/exec.c) -- the exact same
# files build_launchd_syscalls.sh already compiles for launchd's own
# private copy (see that script's own header for the two real fixes both
# files needed: `__weak_reference(sym,alias)` expanding to nothing in
# this project's fbsdcompat/sys/cdefs.h, neutralized the same way here).
# Unlike that launchd copy, this compile needs NO private-name forwarding
# shims for `_execve`/`_write`/`_sigaction` -- all three are already
# real, public, local definitions in THIS dylib (sys_execve.o, just
# compiled above; libc_stdio's own fwrite.o-adjacent _write; and DAR-164's
# real Libc/sys/sigaction.c, both already resident here -- confirmed via
# `nm`, not assumed) rather than launchd's forwarding stand-ins.
# exec.c calls the real private-namespace names (_execve/_write)
# directly (un-namespace.h convention) with no visible prototype in any
# header this project's SDK ships -- real Apple's own libc_private.h
# doesn't declare them either (they're syscall-generator output, not
# hand-written API). Reuses launchd_syscall_compat_headers/
# private_names_decls.h purely for its 4 extern prototypes (declarations
# only -- NOT the corresponding launchd_syscall_wrappers_compat.c
# forwarding bodies, which this dylib doesn't need: _execve/_write are
# already real, public, local definitions here, see this block's own
# header comment above).
"$CLANG" $SIGLIST_CFLAGS -D'__weak_reference(sym,alias)=' \
    -include launchd_syscall_compat_headers/private_names_decls.h \
    "$LIBC/gen/FreeBSD/exec.c" -o "$EXTRACT/exec.o"
"$CLANG" $SIGLIST_CFLAGS -D'__weak_reference(sym,alias)=' \
    "$LIBC/gen/FreeBSD/waitpid.c" -o "$EXTRACT/waitpid.o"

# real fork() -- see libsystem_c_fork_compat.c's own header for the full
# real, verified reasoning on why this canonical version omits
# dispatch_atfork_* (a real, confirmed ld64 build-order cycle:
# libdispatch.dylib reexports THIS dylib, so this dylib cannot also need
# a symbol from it) unlike launchd's own richer private copy.
"$CLANG" $SIGLIST_CFLAGS libsystem_c_fork_compat.c -o "$EXTRACT/fork_compat.o"

# DAR-130: real _libc_fork_child() (third_party/Libc/sys/_libc_fork_child.c),
# called from fork()'s child side above. Its three callees:
# _arc4_fork_child (arc4random.o, already here), __environ_lock_fork_child
# (environ_lock_compat.c) and _init_clock_port (libsystem_c_clock_port_compat.c).
"$CLANG" $SIGLIST_CFLAGS "$LIBC/sys/_libc_fork_child.c" -o "$EXTRACT/libc_fork_child.o"
"$CLANG" $SIGLIST_CFLAGS libsystem_c_clock_port_compat.c -o "$EXTRACT/clock_port_compat.o"

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
    `# DAR-16: asin/sqrt no longer local to this dylib (real owner is` \
    `# now libsystem_m.dylib, see the extraction-removal comment above);` \
    `# libsystem_c_gap_compat.o's atan() still calls both internally.` \
    `# Deferred flat-namespace, resolved at real load time by` \
    `# libsystem_m.dylib -- always co-loaded via the libSystem.B.dylib` \
    `# umbrella, not a dormant gap.` \
    -Wl,-U,_asin \
    -Wl,-U,_sqrt \
    `# __ldpart_free_extra: ldpart.o's own real definition is` \
    `# __private_extern__ (real Apple source), which -Wl,-exported_symbol` \
    `# does NOT reverse (tried first, real confirmed-via-dyld_info` \
    `# failure -- see libsystem_c_ldpart_free_extra_compat.c's own header` \
    `# for the full trail). Real fix: alias a second, genuinely non-` \
    `# hidden copy of the same real body (that compat file, extracted` \
    `# below) to the real public name instead.` \
    -Wl,-alias,_dar153_ldpart_free_extra_impl,___ldpart_free_extra \
    `# ___part_load_locale: real callers (lmessages.o/lmonetary.o/` \
    `# lnumeric.o/timelocal.o, all already part of this dylib's own` \
    `# $EXTRACT via libc_locale_for_stdio.a) reference it. A bare` \
    `# -Wl,-U deferral here was tried first and caused a REAL REGRESSION` \
    `# (broke launchd itself, not just syslogd -- this project's dyld` \
    `# does only eager chained-fixups binding, so an unresolved flat-` \
    `# namespace bind with no real provider anywhere fails at process` \
    `# LOAD time regardless of whether the referencing code path is ever` \
    `# exercised; see dar153_part_load_locale_impl's own header in` \
    `# libsystem_c_ldpart_free_extra_compat.c for the full real trail).` \
    `# Real fix: alias a real, honest, always-_LDP_ERROR implementation` \
    `# instead (same file), matching real Apple's own actual behavior` \
    `# for this target's real, empty locale-file inventory.` \
    -Wl,-alias,_dar153_part_load_locale_impl,___part_load_locale \
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
    -Wl,-alias,_mktime,'_mktime$UNIX2003' \
    -Wl,-alias,'_strftime_l$UNIX2003',_strftime_l \
    -Wl,-alias,_strerror,'_strerror$UNIX2003' \
    -Wl,-alias,_fwrite,'_fwrite$UNIX2003' \
    -Wl,-alias,_fstatfs,'_fstatfs$INODE64' \
    -Wl,-alias,_fopen,'_fopen$UNIX2003' \
    `# DAR-379: freopen gets the same $UNIX2003 spelling as fopen/fdopen.` \
    `# third_party/Libc/include/stdio.h:179-180 declares it` \
    `# __DARWIN_ALIAS(freopen), so every command compiled with this` \
    `# project's -D__DARWIN_UNIX03=1 references _freopen$UNIX2003. With` \
    `# no such export here, build_simple_cmds.sh linked its own freopen.c` \
    `# and, for __sfprelease (__private_extern__, Libc/stdio/FreeBSD/` \
    `# findfp.c), its own findfp.c -- a second, private FILE table that` \
    `# exit() never flushed. Base _freopen is local to this link` \
    `# ($EXTRACT/freopen.o from libc_stdio.a).` \
    -Wl,-alias,_freopen,'_freopen$UNIX2003' \
    `# strtod_l gets its $UNIX2003 spelling too: Libc/include/xlocale/` \
    `# _stdlib.h declares it __DARWIN_ALIAS(strtod_l), so Libc's own` \
    `# atof.c (linked by awk) references _strtod_l$UNIX2003. Base` \
    `# _strtod_l is already this dylib's own export.` \
    -Wl,-alias,_strtod_l,'_strtod_l$UNIX2003' \
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
    `# waitpid.c's own real __waitpid() never gets its public "waitpid"/` \
    `# "_waitpid" aliases in this project -- the real` \
    `# __weak_reference(sym,alias) mechanism it relies on is a no-op here` \
    `# (fbsdcompat/sys/cdefs.h, same real gap build_launchd_syscalls.sh's` \
    `# own header already documents for its private copy). Base` \
    `# (___waitpid) is genuinely local to this link (exec/waitpid.o,` \
    `# compiled above), same nonlocal-base requirement as every other` \
    `# alias here.` \
    -Wl,-alias,___waitpid,_waitpid \
    -Wl,-alias,___waitpid,__waitpid \
    `# exec.c's own un-namespace.h convention calls the PRIVATE,` \
    `# POSIX-reserved name "_execve" directly (real trial link confirmed` \
    `# this, not assumed: "undefined symbols: __execve, referenced from` \
    `# _execl in exec.o"). Real Apple's libsyscall generator/syscall.map` \
    `# never actually alias it (checked, no entry in Platforms/` \
    `# syscall.map or the iPhoneOS arm64 variant) -- launchd's own` \
    `# private copy works around the same real gap with a forwarding` \
    `# compat .c (launchd_syscall_compat_headers/private_names_compat.c).` \
    `# This dylib already has a real, local, base "_execve" (sys_execve.o` \
    `# just compiled above), so a plain alias is simpler than a` \
    `# forwarding call. exec.c's other private name, "_write", needed NO` \
    `# alias at all -- libc_locale_syscalls.o (already resident here)` \
    `# already defines it for real, confirmed by a real "duplicate` \
    `# symbol" link error on a first attempt that added one anyway.` \
    `# Base is genuinely local to this link, same nonlocal-base` \
    `# requirement as every other alias here.` \
    -Wl,-alias,_execve,__execve \
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
    "$OUT/system/libsystem_malloc.dylib" "$OUT/system/libsystem_pthread.dylib" "$OUT/system/libsystem_blocks.dylib"
    # ^ libdyld.dylib is deliberately NOT reexported here (moved
    # 2026-09-14 to the real libSystem.B.dylib umbrella,
    # build_libsystem.sh, instead -- this dylib reexporting libdyld.dylib
    # made libsystem_c.sh <-> libdyld.sh a real cycle, since libdyld.sh
    # genuinely needs libsystem_c.dylib to exist first for its own real
    # symbol needs. libSystem.B.dylib already runs after both, so it's
    # the correct place for this reexport, matching real Apple
    # architecture -- see that script's own comment.)
    # ^ libremovefile.dylib is deliberately NOT linked here (removed
    # 2026-09-14): this dylib's own qsort.o (extracted from libremovefile.a
    # above) calls flsl(), but flsl.o is compiled directly into this
    # dylib's own $EXTRACT (see libiconv_flsl_compat.c above) since
    # 2026-08-30 -- the only reason a direct libremovefile.dylib link was
    # ever added (2026-08-28, static-linking cleanup Stage B, for that
    # same flsl() symbol) no longer applies. Keeping it created a real
    # bootstrap cycle: build_removefile_dylib.sh's own dependency check
    # requires libsystem_c.dylib to already exist (it links against it),
    # so the two scripts couldn't both go first. Removing this dylib link
    # breaks the cycle at its (stale) root instead of reordering around
    # it.
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
