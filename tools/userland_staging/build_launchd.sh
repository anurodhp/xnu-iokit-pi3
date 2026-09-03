#!/bin/bash
# Builds libc_build/launchd.macho: the real, complete, dynamically-linked
# launchd executable -- promotes build_launchd_core.sh's own informational
# trial link (which reached 0 undefined/0 duplicate symbols for real,
# 2026-08-28, see docs/daemon-roadmap.md's "Milestone 2 CLOSED" writeup)
# into a real, first-class, checked-in build product, per this project's
# own "every built artifact needs a reproducing script" convention.
#
# `-force_load` on liblaunchd_core.a is NOT just informational here --
# it's the real, required build flag. Real launchd.c's own main() reaches
# core.o/ipc.o/log.o only indirectly (dispatch source handlers, XPC
# connection callbacks stored as function pointers), which a plain
# (non-force_load) static archive link's pull-in logic can't see as a
# "reference" -- confirmed by build_launchd_core.sh's own header comment
# (its first trial-link attempt, without -force_load, silently produced a
# "0 undefined symbols" link that was missing core.o/ipc.o/log.o
# entirely). Real DYNAMIC link (not `-static`): CoreFoundation depends on
# real libdispatch.dylib, which a static link can't resolve at all (real
# ld64 limitation). Every `-Wl,-alias` pair and prerequisite archive here
# is real, load-bearing, and copied exactly from build_launchd_core.sh's
# own real trial-link command (see that script + docs/daemon-roadmap.md
# for the full per-symbol writeup of why each one is needed).
#
# THIS SCRIPT IS A LINK STEP ONLY -- IT COMPILES NONE OF LAUNCHD'S OWN
# SOURCE (DAR-200, 2026-09-02). Every launchd .c file (launchd.c, core.c,
# ipc.c, runtime.c, log.c, ...) is compiled by build_launchd_core.sh into
# liblaunchd_core.a; this script only relinks that already-built archive.
# So editing anything under
# third_party/PureDarwin/src/Libraries/XPC/launchd/ and then running only
# this script silently produces a launchd.macho WITHOUT the edit -- it
# links cleanly, it is a valid binary, and it is simply stale. That cost a
# real, wasted QEMU boot cycle during DAR-200: a temporary ipc.c
# diagnostic printed nothing, which read exactly like "this code never
# runs" rather than "this code was never compiled in". Always run
# build_launchd_core.sh FIRST after any launchd source change:
#
#     tools/userland_staging/build_launchd_core.sh   # compiles the sources
#     tools/userland_staging/build_launchd.sh        # links launchd.macho
#
# and when adding a diagnostic, confirm it actually landed
# (`strings libc_build/launchd.macho | grep <marker>`) before trusting a
# null result from a boot log -- same "validate the diagnostic channel in
# the same boot" lesson as docs/launchd-pid1-postmortem.md's process
# lesson 3, one layer earlier in the toolchain.
set -e
cd "$(dirname "$0")"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
LD="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi

# Same real ld64-609 workaround as every other real dynamic-link script
# in this project (docs/backlog.md item d9): Xcode 12's own bundled ld
# crashes under -Wl,-fixup_chains, so the final link step's linker binary
# is the daily-driver Xcode's newer ld instead (compiling still uses
# Xcode 12's clang/SDK, untouched).
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi

OUT="libc_build"
ICU_LIB="icu_build/target/lib"

for a in liblaunchd_core.a libCoreFoundation.a cf_libc_extras.a launchd_libc_extras.a \
         liblaunchd_mig.a liblaunchd_syscalls.a \
         libmach_ipc.a libclosure.a libmalloc.a libplatform.a libpthread.a \
         libc_gen.a libc_string.a libc_stdio.a libc_printf.a libc_locale_for_stdio.a; do
    if [ ! -f "$OUT/$a" ]; then
        echo "error: $OUT/$a not found -- run build_launchd_core.sh first" >&2
        exit 1
    fi
done

# Staleness guard (DAR-200, 2026-09-02) -- see this script's own header
# comment for the wasted boot cycle that motivated it. This script never
# compiles launchd's own source, so a liblaunchd_core.a older than any
# file under the launchd source tree means the launchd.macho about to be
# produced would silently NOT contain the newest edit. Fail loudly here
# rather than at 4 minutes per QEMU boot.
LAUNCHD_SRC="../../third_party/PureDarwin/src/Libraries/XPC/launchd"
if [ -d "$LAUNCHD_SRC" ]; then
    NEWER=$(find "$LAUNCHD_SRC" \( -name '*.c' -o -name '*.h' -o -name '*.defs' \) \
        -newer "$OUT/liblaunchd_core.a" -print 2>/dev/null | head -5)
    if [ -n "$NEWER" ]; then
        echo "error: launchd source is newer than $OUT/liblaunchd_core.a -- this script only LINKS," >&2
        echo "       it never compiles launchd's own .c files, so the resulting launchd.macho would" >&2
        echo "       silently be missing these changes:" >&2
        echo "$NEWER" | sed 's/^/         /' >&2
        echo "       Run tools/userland_staging/build_launchd_core.sh first, then re-run this." >&2
        echo "       (Set LAUNCHD_ALLOW_STALE_CORE=1 to link anyway, e.g. for a deliberate bisect.)" >&2
        [ "${LAUNCHD_ALLOW_STALE_CORE:-0}" = "1" ] || exit 1
        echo "= LAUNCHD_ALLOW_STALE_CORE=1 set -- linking a knowingly stale liblaunchd_core.a anyway" >&2
    fi
fi
# libsystem_malloc.dylib -- real, single-owner heap (DAR-131/DAR-162 fix,
# see this script's own "libmalloc_no_mach_init.a REMOVED" comment below
# for the full story). Needed as an EXPLICIT link input, not just reachable
# via libSystem.B.dylib's LC_REEXPORT_DYLIB of it -- same real ld64
# limitation as this script's own pre-existing ___chkstk_darwin comment a
# few lines down: a reexport-only symbol needs its real owning dylib
# physically listed on this link line too.
if [ ! -f "$OUT/system/libsystem_malloc.dylib" ]; then
    echo "error: $OUT/system/libsystem_malloc.dylib not found -- run build_libsystem_malloc.sh first" >&2
    exit 1
fi
# libremovefile.dylib -- same reexport-depth issue as libsystem_malloc.dylib
# above, found while relinking after the malloc removal: liblaunchd_
# syscalls.a's real glob.o (___gl_globextend) needs _reallocarray$
# DARWIN_EXTSN, which used to be silently satisfied by libmalloc_no_mach_
# init.a's own local (non-suffixed) copy before this session's removal.
# The real, working $DARWIN_EXTSN alias lives in build_removefile_dylib.sh
# (confirmed via `xcrun dyld_info -exports`: it's real and local there),
# reachable at RUNTIME via libdyld.dylib's own -Wl,-reexport_library of
# it -- but per this file's own pre-existing ___chkstk_darwin comment,
# ld64's STATIC link can't resolve a reexport that's two reexport-hops
# deep (libSystem.B.dylib -> libdyld.dylib -> libremovefile.dylib) by
# install-name alone, so it must be listed directly here too. Only
# surfaced once the malloc archive's own local (non-suffixed) _reallocarray
# stopped being present to incidentally short-circuit ld64's namespace
# walk for this one -- the real, correct owner was always this deep
# reexport chain, never the malloc archive.
if [ ! -f "$OUT/system/libremovefile.dylib" ]; then
    echo "error: $OUT/system/libremovefile.dylib not found -- run build_removefile_dylib.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/chk_fail.o" ]; then
    echo "error: $OUT/chk_fail.o not found -- run build_libmalloc.sh first" >&2
    exit 1
fi
# libxpc.a/libxpc_syscalls.a/libxpc_syscalls2.a -> libxpc.dylib and
# libbsm.a -> libbsm.dylib (static-linking cleanup pass, 2026-08-29):
# real per-library dylibs now exist for both (build_libxpc_dylib.sh,
# build_bsm_dylib.sh) -- confirmed via `nm -gU` before switching that
# libxpc.dylib genuinely, locally exports every MIG symbol this file's
# own mach_port_gap_closures2.c comment above relies on
# (_kernelrpc_mach_port_insert_right/_extract_right/_unguard, all real
# `T`, not deferred), not just guessed compatible.
for d in libxpc.dylib libbsm.dylib; do
    if [ ! -f "$OUT/system/$d" ]; then
        echo "error: $OUT/system/$d not found -- run the matching build_*_dylib.sh first" >&2
        exit 1
    fi
done
for d in libdispatch.dylib libdyld.dylib libSystem.B.dylib libsystem_pthread.dylib libc++.dylib; do
    if [ ! -f "$OUT/system/$d" ]; then
        echo "error: $OUT/system/$d not found -- run the matching build_*.sh (see docs/daemon-roadmap.md's dylib migration list) first" >&2
        exit 1
    fi
done
for d in libicuuc.dylib libicui18n.dylib libicudata.dylib; do
    if [ ! -f "$ICU_LIB/$d" ]; then
        echo "error: $ICU_LIB/$d not found -- run build_icu.sh first" >&2
        exit 1
    fi
done

# libmalloc.a REMOVED FROM THIS LINK ENTIRELY 2026-08-31 (DAR-23/DAR-131/
# DAR-162): launchd.macho used to carry a narrowed private copy of it
# here (libmalloc_no_mach_init.a, `ar d`-stripped of just mach_init.o/
# mig_reply_port.o -- see git history for the two real, QEMU-diagnosed
# bugs that fix addressed, 2026-08-28/29) alongside the real
# libsystem_malloc.dylib it ALSO links (transitively, via
# libSystem.B.dylib's own real LC_REEXPORT_DYLIB of it). That is the
# exact same "two heaps" bug class as this file's own already-fixed
# TSD-base-clobber (Bug A) and dual-pthread_create (Bug B) duplicates
# documented in docs/launchd-pid1-postmortem.md -- `nm libmalloc_
# no_mach_init.a` shows `T _malloc`/`_free`/`_calloc`/`_realloc`, the
# whole malloc-zone-introspection family (`_mach_zone_info`/
# `_task_zone_info`/`_scalable_zone_info`/`_scalable_zone_statistics`/
# `_szone_check_*`/`_malloc_error_break`/`_malloc_printf`/
# `_malloc_tracing_enabled`/`_malloc_zone_check_fail`), the fork hooks
# (`__malloc_fork_child`/`_parent`/`_prepare`), AND a private copy of
# `pthread_tsd_shim.o`'s POSIX-named `_pthread_getspecific`/
# `_pthread_setspecific`/`_pthread_key_create`/`_pthread_once`/
# `_pthread_key_init_np` -- every one of those is also a real export of
# libsystem_pthread.dylib (the pthread ones) or libsystem_malloc.dylib
# (the malloc ones), both already-linked siblings. DAR-131's own finding
# was concrete: `_malloc_fork_child()` in `fork()` bound to launchd's OWN
# local zone, so a real `fork()` only ever reinitialized launchd's private
# heap, never the real one CoreFoundation/libdispatch/libc++ actually
# allocate from -- a real, live correctness gap on every fork this PID-1
# does, not just a symbol-table count.
#
# Fix: drop the archive from the link line entirely (same "remove, don't
# reconcile" shape as the libpthread_no_dup_pthread_create.a removal a
# few lines down) and link libsystem_malloc.dylib explicitly as a sibling
# (see the dependency check above) -- real ld64 can't resolve a
# reexport-only symbol by install-name alone here (this file's own
# pre-existing ___chkstk_darwin comment below explains why: the reexport
# target doesn't exist as a real file at its /usr/lib/system install path
# in this project's sysroot, only at this project's own build output
# path), so any consumer of a reexported-only symbol must list the real
# owning dylib directly, same as libsystem_c.dylib/libsystem_pthread.dylib
# already are here.
#
# What broke on the first trial relink, and the real (not narrowing) fix
# for each: `___chk_fail_overflow`/`___chk_overlap`/
# `___chk_assert_no_overlap`/`___chk_fail_overlap` (chk_fail_compat.o's
# real, `.private_extern`-by-construction helpers -- per this project's
# own static-linking-cleanup-plan.md, these legitimately belong in every
# image that carries `_chk` members, so they're relinked here from the
# already-built standalone `$OUT/chk_fail.o` object instead of the whole
# malloc archive).
#
# libmalloc.a's real mach_init.o/mig_reply_port.o duplicate-state history
# (the two bugs referenced above) is preserved in git log/
# docs/launchd-pid1-postmortem.md; this comment intentionally does not
# restate it since the whole archive is gone from this link now, not just
# those two members.
#
# Real gap found relinking without libmalloc.a: liblaunchd_syscalls.a's
# own sys____fork.o (the raw __fork() trap, generated fresh in
# build_launchd_syscalls.sh from the same real create-syscalls.pl this
# project uses everywhere) writes the post-fork child pid into
# __current_pid via a direct (arm64_adrp_lo12) same-image relocation --
# real Apple's own third_party/xnu-7195/libsyscall/custom/__getpid.s
# confirms this is by design: __getpid() and __fork() are meant to be
# compiled into the SAME image so this addressing mode can reach the pid
# cache directly, without a dylib-crossing GOT load. This project's real
# canonical home for that pairing would be libsystem_kernel.dylib (which
# owns __fork's own sibling raw traps) -- but that dylib does not
# currently implement _getpid/__current_pid at all (confirmed via nm; a
# real, structural, pre-existing gap, and build_libsystem_kernel.sh is
# out of scope for this session, being actively worked by a concurrent
# effort). Practical fallback: reuse the already-built, real Apple
# sys_getpid.o (build_libmalloc.sh's own output, same source this
# project's __current_pid always came from) narrowed via the same
# `ld -r -unexported_symbol` idiom already established in this file for
# pthread_main_thread_bootstrap.o -- keep __current_pid (the real,
# genuinely-needed data symbol) fully external, demote _getpid/___getpid
# to private-extern. Verified via a real relink + `nm -m`: every plain
# _getpid() call elsewhere in this binary (core.o/ipc.o/launchd.o/log.o/
# runtime.o, CFMessagePort.o/CFUtilities.o/cf_pthread_setname_syscalls.o,
# liblaunchd_syscalls.a's raise.o) still binds to THIS local copy, not to
# libsystem_malloc.dylib's own (separate, real) sys_getpid.o-derived
# export -- i.e. _getpid/___getpid/__current_pid stay in the DAR-23
# shadow count exactly as before (they were already shadowed via
# libmalloc_no_mach_init.a pre-this-fix), not newly introduced and not
# newly fixed. Left as a documented, structural exception rather than
# force-fixed: the real fix is giving libsystem_kernel.dylib its own real
# getpid/fork tier, tracked as a follow-up, not attempted here to avoid
# touching a file another concurrent session owns this pass.
if [ ! -f "$OUT/sys_getpid.o" ]; then
    echo "error: $OUT/sys_getpid.o not found -- run build_libmalloc.sh first" >&2
    exit 1
fi
rm -f "$OUT/sys_getpid_current_pid_only.o"
"$LD" -r -arch arm64 -keep_private_externs \
    -unexported_symbol ___getpid -unexported_symbol _getpid \
    "$OUT/sys_getpid.o" -o "$OUT/sys_getpid_current_pid_only.o"

# Real gap found continuing the DAR-23/DAR-131/DAR-162 sweep after the
# malloc removal above: libc_locale_for_stdio.a's own pthread_tsd_shim.o
# (the SAME pthread_tsd_shim.c that Bug A of docs/launchd-pid1-postmortem.md
# already fixed at the SOURCE level -- the adopt-not-clobber logic in
# pthread_tsd_shim_bootstrap() -- so this copy is not a repeat of that
# specific TSD-base-clobber danger) still gave launchd.macho its OWN
# independent implementation of `_pthread_getspecific`/`_setspecific`/
# `_key_create`/`_once`/`_key_init_np`/`_tsd_shim_bootstrap`, all of which
# are ALSO real exports of libsystem_pthread.dylib (the POSIX names) or
# libsystem_malloc.dylib (`_pthread_tsd_shim_bootstrap` itself) -- both
# already-linked siblings. Real danger, same "state, not count" priority
# as the malloc fix above: this project's own xlocale.o (in the SAME
# archive) calls these 3 expecting REAL pthread TSD key semantics, so a
# `pthread_key_create()` done through this shim's OWN, separate key
# table would be invisible to any REAL pthread_getspecific() call made by
# libdispatch/CF/libc++ against a key THEY created (and vice versa) --
# two independent TSD-key namespaces in one process.
#
# Fix: same "-r -unexported_symbol on a private derived copy" shape as
# libmalloc.a's own pthread_tsd_shim.o handling above, but here the
# archive (libc_locale_for_stdio.a) is genuinely SHARED with other
# consumers (syslogd.macho, pd_console_login.macho, build_simple_cmds.sh,
# build_corefoundation.sh's own smoketest) that still need their OWN
# private copy (either fully static or not yet swept) -- so the shared
# archive itself is untouched; only a launchd-local derived copy drops
# the two tsd_shim members entirely (not narrowed: nothing else in this
# link needs pthread_tsd_shim_libc_bootstrap.o's own
# _pthread_tsd_shim_bootstrap_for_libc, confirmed via a real `nm` sweep of
# every launchd-relevant source file -- only *_smoketest.c/
# uname_main_wrapper.c call it, none of which are part of this link).
# Verified via a real relink (0 undefined symbols) + `nm -m`: xlocale.o's
# calls now show "(from libsystem_pthread)" instead of a local T
# definition.
if [ ! -f "$OUT/libc_locale_for_stdio.a" ]; then
    echo "error: $OUT/libc_locale_for_stdio.a not found -- run build_libc_stdio.sh first" >&2
    exit 1
fi
rm -f "$OUT/libc_locale_for_stdio_no_tsd_shim.a"
cp "$OUT/libc_locale_for_stdio.a" "$OUT/libc_locale_for_stdio_no_tsd_shim.a"
ar d "$OUT/libc_locale_for_stdio_no_tsd_shim.a" pthread_tsd_shim.o
ar d "$OUT/libc_locale_for_stdio_no_tsd_shim.a" pthread_tsd_shim_libc_bootstrap.o
# cerror_stub.o ALSO REMOVED from this same derived copy (found immediately
# after the standalone $OUT/cerror_stub.o removal below still left
# ___error/_cerror/_cerror_nocancel/__pthread_exit_if_canceled locally
# defined -- libc_locale_for_stdio.a carries its OWN separate copy of this
# same member, per build_libc_locale.sh's own `ar rcs` file list). Many
# members of this archive (ascii.o/collate.o/setlocale.o/strtol.o/xlocale.o
# and ~20 more) have real, undefined `___error` references that need to
# keep resolving -- confirmed they now bind "(from libsystem_kernel)"
# after this removal, the real canonical owner, already linked below.
ar d "$OUT/libc_locale_for_stdio_no_tsd_shim.a" cerror_stub.o

# Real gap found the same pass: cerror_stub.o (a small, freestanding
# `___error`/`_cerror`/`_cerror_nocancel`/`_errno`/`_pthread_exit_if_
# canceled` stand-in predating this project's real error-handling
# tier) was still linked directly into launchd.macho as a loose object,
# even though the 2026-08-30 static-linking-cleanup session already
# identified this EXACT member as a stray duplicate and removed it from
# libsystem_c.dylib's own build for precisely this reason (see
# docs/static-linking-cleanup-plan.md's "libsystem_c.dylib gained real
# single ownership" section) -- libsystem_kernel.dylib is the real,
# already-transitively-linked canonical owner of all five symbols
# (confirmed via `nm -gUj`). Dropped entirely from this link; verified
# via a real relink that all five now resolve "(from libsystem_kernel)".
# libsystem_kernel.dylib is not otherwise explicitly listed on this link
# line (only reachable via libSystem.B.dylib's reexport before this
# point) -- same reexport-depth reasoning as libsystem_malloc.dylib/
# libremovefile.dylib above, so it is added explicitly too.
if [ ! -f "$OUT/system/libsystem_kernel.dylib" ]; then
    echo "error: $OUT/system/libsystem_kernel.dylib not found -- run build_libsystem_kernel.sh first" >&2
    exit 1
fi

# Real gap found+fixed 2026-08-28, next crash after the mach_task_self_
# fix above: runtime.c's launchd_runtime_init() calls
# launchd_mport_make_send() -> mach_port_insert_right(), which SIGABRTed
# via os_assert_zero() because it always returned KERN_FAILURE. Root
# cause: libmach_ipc.a's mach_ipc_dead_stubs.o bundles a narrowly-scoped
# KERN_FAILURE stand-in for mach_port_insert_right/extract_right/unguard
# (only ever meant to satisfy 3 provably-unreachable mach_msg_destroy_port()
# reply-validation-failure paths, see that file's own header), and the
# linker pulled it into launchd.macho's own static portion to satisfy
# runtime.c's real, live, direct call -- the EXACT same bug this project
# already found and fixed once for syslogd/libdispatch.dylib (see
# mach_port_gap_closures2.c's own header, "REAL BUG FIXED, not just a new
# gap closure") -- just never applied to launchd's own separate,
# statically-linked build. Same fix: strip mach_ipc_dead_stubs.o from a
# local copy of libmach_ipc.a, compile mach_port_gap_closures2.c/.s fresh
# (real trap-first + real MIG-fallback implementations, verbatim from
# libsyscall/mach/mach_port.c) so the real versions win instead. The MIG
# fallback symbols (_kernelrpc_mach_port_insert_right/_unguard) these
# reference are already real, defined exports of libxpc_syscalls2.a's
# mach_portUser.o, already a real input to this link.
XNU="../../third_party/xnu-7195"
LIBC="../../third_party/Libc"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I "$XNU/libsyscall/mach" -I "$XNU/libsyscall/os" -idirafter "$XNU/osfmk" \
    -I "$LIBC/include" -I "$LIBC/darwin" \
    mach_port_gap_closures2.c -o "$OUT/mach_port_gap_closures2_raw.o"
# Narrowed via `ld -r -exported_symbol` (same precedent
# build_libdispatch_dylib.sh's own vm_mapUser.o narrowing already
# established): mach_port_request_notification is excluded on purpose --
# liblaunchd_core.a's own launchd_mach_port_compat.o already provides a
# real (MIG-only, no trap-first fast path, but functionally correct)
# definition of that one symbol; keeping this file's other 5 real
# implementations (which launchd_mach_port_compat.o does NOT provide)
# without hard-duplicate-symbol-conflicting on the one it does.
"$LD" -r -arch arm64 \
    -exported_symbol _mach_port_insert_right \
    -exported_symbol _mach_port_extract_right \
    -exported_symbol _mach_port_unguard \
    -exported_symbol _mach_port_kernel_object \
    -exported_symbol _thread_destruct_special_reply_port \
    "$OUT/mach_port_gap_closures2_raw.o" -o "$OUT/mach_port_gap_closures2.o"
rm -f "$OUT/mach_port_gap_closures2_raw.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    mach_port_gap_closures2.s -o "$OUT/mach_port_gap_closures2_traps.o"
rm -f "$OUT/libmach_ipc_no_dead_stubs.a"
cp "$OUT/libmach_ipc.a" "$OUT/libmach_ipc_no_dead_stubs.a"
ar d "$OUT/libmach_ipc_no_dead_stubs.a" mach_ipc_dead_stubs.o
# Real gap found+fixed 2026-08-28, real crash introduced by the above
# `ar d` itself: mach_ipc_dead_stubs.o bundles 4 functions, not just the
# 3 mach_port_* ones replaced above -- voucher_mach_msg_set() (an honest
# `return FALSE` stand-in; real, weak_import-declared per every real MIG
# client stub's own generated code, see that function's own header on
# why "no voucher subsystem" is the correct, deliberate answer here, same
# scope decision as INET/INET6 networking) got removed as a side effect
# too. Without it, _kernelrpc_mach_port_move_member's own real generated
# `if (voucher_mach_msg_set != NULL) voucher_mach_msg_set(&InP->Head);`
# check resolved the symbol externally instead -- to libdispatch.dylib's
# OWN real, considerably more complex voucher implementation, which this
# project's boot-time process state isn't ready for (confirmed via a real
# instrumentation pass: this is exactly where the next silent exit(1)
# appeared after the mach_msg duplicate fix above). Real fix: narrow
# mach_ipc_dead_stubs.o via `ld -r -exported_symbol` (same precedent
# mach_port_gap_closures2.o's own narrowing already established) down to
# just voucher_mach_msg_set, so the honest stub wins again without
# reintroducing the mach_port_insert_right/extract_right/unguard
# duplicate-symbol conflict the real versions above already fixed.
if [ ! -f "$OUT/mach_ipc_dead_stubs.o" ]; then
    echo "error: $OUT/mach_ipc_dead_stubs.o not found -- run build_libmach_ipc.sh first" >&2
    exit 1
fi
"$LD" -r -arch arm64 \
    -exported_symbol _voucher_mach_msg_set \
    "$OUT/mach_ipc_dead_stubs.o" -o "$OUT/mach_ipc_dead_stubs_voucher_only.o"
# Real gap found+fixed 2026-08-28, next crash after the above: real
# mach_msg()/mach_msg_trap() (mach_msg_real.o) were ALSO bundled as a
# private static copy here, competing with libsystem_kernel.dylib's own
# now-real copy (see build_libsystem_kernel.sh's own header on this same
# fix) -- launchd's own _kernelrpc_mach_port_move_member() call resolved
# mach_msg() to libdispatch.dylib's OWN separate private copy instead of
# either of these (confirmed via a real instrumentation pass: temporary
# prints in this project's own mach_msg_real.o never fired). Stripped
# here so this static portion's real call resolves against the one
# shared, canonical copy in libsystem_kernel.dylib (already transitively
# linked via libSystem.B.dylib's own reexport chain, confirmed via nm
# before relying on it, not assumed).
ar d "$OUT/libmach_ipc_no_dead_stubs.a" mach_msg_real.o

# DAR-23 dedup pass (2026-09-03): the rest of libmach_ipc.a's MIG
# client-stub members (task.defs/mach_host.defs/clock.defs-generated
# code, plus a couple of small trap/strncpy helpers) are pure
# duplicates of libsystem_kernel.dylib's own real, identical exports --
# unlike mach_ipc_dead_stubs.o/mach_msg_real.o above, these were never
# behaviorally wrong, just redundant. Verified member-by-member: every
# symbol each of these 7 members defines is present in launchd.macho's
# OWN real loaded-dylib transitive closure (dup_symbol_audit.sh's
# per-binary computation, which follows actual LC_LOAD_DYLIB/
# LC_REEXPORT_DYLIB edges -- NOT just "some dylib somewhere under
# libc_build/system", which would wrongly credit dylibs launchd never
# loads at all, e.g. libCoreFoundation.dylib/libncurses.dylib). Leaving
# them in costs nothing at runtime (ld64 just picks one definition) but
# keeps this binary carrying private copies of MIG glue for no reason,
# same shadowing class as DAR-131/DAR-162.
ar d "$OUT/libmach_ipc_no_dead_stubs.a" \
    clockUser.o mach_host_self_real.o mach_hostUser.o \
    mach_ipc_clock_init.o mach_ipc_traps.o mig_strncpy.o taskUser.o

# Real gap found+fixed 2026-08-29, launchd bug #6's actual root cause:
# pthread_main_thread_bootstrap.o (pthread_create/_real_pthread_start_abi/
# the `_pthread_create.registered` bsdthread_register-once gate) was ALSO
# bundled as a private static copy here -- libpthread.a is a plain
# archive linked BEFORE system/libsystem_pthread.dylib below, so ld64
# pulls this .o out of the archive to satisfy liblaunchd_core.a's
# undefined `_pthread_create` reference before the dylib is ever
# considered, permanently baking a SEPARATE copy of the entire pthread
# bootstrap/registration implementation into launchd.macho (confirmed via
# `nm`: both launchd.macho and libsystem_pthread.dylib defined a strong
# `T _pthread_create` before this fix). Same bug class as the
# mach_msg_real.o fix directly above: two independently-registered
# `__bsdthread_register()` call sites (own `_real_pthread_start_abi`
# code address each, own `registered` static gate each) racing to be the
# one the kernel actually trusts for this process is a real, sufficient
# explanation for launchd bug #6's observed symptom (main thread resuming
# execution pointed at the SECOND thread's private stack) -- confirmed
# every symbol this .o provides (___pthread_head, __main_thread_ptr,
# _pthread_create, _pthread_equal, _pthread_main_np,
# _pthread_main_thread_bootstrap, _pthread_main_thread_np, _pthread_once)
# is already a real, exported symbol of libsystem_pthread.dylib before
# stripping it.
# Can't just `ar d` the whole .o out (first attempt, real link failure):
# pthread_tsd.o/pthread_clear_qos_tsd_compat.o (other, still-needed
# members of this same archive) hold real undefined references to this
# .o's own shared DATA symbols (___pthread_head, __main_thread_ptr,
# __main_thread, __pthread_list_lock, __pthread_ptr_munge_token) --
# those aren't duplicated anywhere else (the dylib's own copies are
# local/non-exported, invisible for this static archive's own internal
# cross-.o linking). Real fix: narrow via `ld -r -exported_symbol` (same
# precedent as this script's own mach_port_gap_closures2.o above) down
# to just those 5 real data symbols, dropping every function this .o
# also defined (_pthread_create, _pthread_equal, _pthread_main_np,
# _pthread_main_thread_bootstrap, _pthread_main_thread_np,
# _pthread_once) so those resolve to
# system/libsystem_pthread.dylib's own real, shared copies below instead.
# -unexported_symbol (not -exported_symbol): ___pthread_head/
# __main_thread_ptr/__main_thread/__pthread_list_lock/
# __pthread_ptr_munge_token are already `private extern` (hidden but
# link-unit-visible) in the real source -- ld64 refuses to even list an
# already-hidden symbol under -exported_symbol ("cannot export hidden
# symbol"). -unexported_symbol instead demotes exactly the named
# (currently fully-external) function symbols to hidden and leaves
# every other symbol, hidden or not, untouched -- but `ld -r` only
# preserves already-private-extern symbols AS private-extern (still
# externally linkable within this static archive) when
# -keep_private_externs is also passed; without it, a real (found by
# trying it) second failure mode appeared, `ld -r` silently demoting
# ___pthread_head/__main_thread_ptr/__pthread_list_lock all the way to
# fully local/non-external, breaking pthread_tsd.o's own real undefined
# references to them.
rm -f "$OUT/pthread_main_thread_bootstrap_raw.o"
ar p "$OUT/libpthread.a" pthread_main_thread_bootstrap.o > "$OUT/pthread_main_thread_bootstrap_raw.o"
"$LD" -r -arch arm64 -keep_private_externs \
    -unexported_symbol _pthread_create \
    -unexported_symbol _pthread_equal \
    -unexported_symbol _pthread_main_np \
    -unexported_symbol _pthread_main_thread_bootstrap \
    -unexported_symbol _pthread_main_thread_np \
    -unexported_symbol _pthread_once \
    "$OUT/pthread_main_thread_bootstrap_raw.o" -o "$OUT/pthread_main_thread_bootstrap_data_only.o"
rm -f "$OUT/pthread_main_thread_bootstrap_raw.o"
rm -f "$OUT/libpthread_no_dup_pthread_create.a"
cp "$OUT/libpthread.a" "$OUT/libpthread_no_dup_pthread_create.a"
ar d "$OUT/libpthread_no_dup_pthread_create.a" pthread_main_thread_bootstrap.o
ar r "$OUT/libpthread_no_dup_pthread_create.a" "$OUT/pthread_main_thread_bootstrap_data_only.o"

# libSystem.B.dylib real-reexports libsystem_c.dylib (confirmed via
# otool -L: "reexport"), which is where the real, defined
# ___chkstk_darwin (Apple's large-stack-frame probe helper -- confirmed
# via `nm -m libsystem_c.dylib` showing it "external"/defined, not just
# referenced) actually lives. ld64 can't resolve a reexport by
# install-name alone here since /usr/lib/system/libsystem_c.dylib
# doesn't exist in this project's on-disk SDK/sysroot -- only this
# project's own build at $OUT/system/libsystem_c.dylib does -- so any
# consumer that needs a reexported-only symbol must list the real dylib
# explicitly on the link line too, same as libSystem.B.dylib itself is.
# CF's CFAttributedString.c/CFBinaryPList.c and the host_priv.defs MIG
# stubs below both have large enough stack frames to reference it;
# -fno-stack-check (this project's usual workaround, see
# build_launchd_core.sh's localtime.c comment) would also work but a
# real chkstk_darwin probe already exists as real Apple code, so linking
# to it directly is the more correct fix.
# -Wl,-u,__current_pid added 2026-08-31 (DAR-124 sweep, found while
# verifying this session's OWN, unrelated dylib fixes with a real
# rebuild -- pre-existing bug, not caused by this session's actual
# DAR-124 work): now that libsystem_malloc.dylib is a real, explicit
# link input here (this file's own just-preceding DAR-131/DAR-162 fix),
# it silently short-circuits ld64's archive-member-pull for `_getpid`
# (it exports a real `_getpid` of its own) before `liblaunchd_syscalls.a`
# ever gets a chance to satisfy that reference from its own
# sys____getpid.o -- which is the ONLY object in this link that defines
# `__current_pid`, the process-wide cached-pid global real Apple's own
# sys____fork.o/sys____vfork.o (real, unmodified libsyscall/custom/
# __fork.s) directly ADRP-addresses (same-image, non-GOT-relative
# codegen -- see docs/static-linking-cleanup-plan.md's "continuation 9"
# xlocale writeup for the identical `arm64_adrp_lo12` mechanism). Once
# libsystem_malloc.dylib's own `_getpid` wins the resolution race,
# sys____fork.o's own private reference to `__current_pid` becomes truly
# dangling: `ld: fixup error (kind=arm64_adrp_lo12) ... target
# '__current_pid' does not have address`. This flag forces ld64 to treat
# `__current_pid` as needed from the very start of the link, which pulls
# sys____getpid.o (and therefore a second, local, real Apple `_getpid`)
# in alongside it -- ld64 resolves the LOCAL strong definition ahead of
# any dylib's for launchd.macho's own references, so this also makes
# `_getpid`/`fork()` share the one real cache again, matching real
# Apple's own architecture (both belong in the same image) rather than
# leaving fork()'s pid cache and getpid()'s answer split across two
# independently-compiled copies -- arguably a real correctness fix, not
# just a link-error workaround.

# DAR-23 dedup pass (2026-09-03), continued: same member-by-member
# verification as the libmach_ipc_no_dead_stubs.a trim above, applied to
# the other four plain archives on this link line. Each member below
# defines ONLY symbols already present in launchd.macho's own real
# loaded-dylib transitive closure -- confirmed via dup_symbol_audit.sh's
# per-binary computation, not a blanket per-directory nm scan (that
# over-broad version wrongly "found" e.g. _getsectbynamefromheader_64
# covered via libCoreFoundation.dylib, a dylib this binary never loads --
# false positive, left alone). Members NOT listed here (most of each
# archive, e.g. liblaunchd_syscalls.a's fork/exec/chdir/chown family)
# have no dylib provider at all yet and must stay; removing a whole
# archive wholesale was tried and fails with ~150 undefined symbols, the
# same "no dylib provider at all" shape DAR-234 already catalogued --
# this pass only removes the provably-redundant slice, per member, not
# per archive.
rm -f "$OUT/liblaunchd_mig_no_dup.a"
cp "$OUT/liblaunchd_mig.a" "$OUT/liblaunchd_mig_no_dup.a"
# domain_user.o/internal_user.o/job_forward_user.o/job_replyUser.o/
# job_user.o kept (NOT stripped) 2026-09-03, real QEMU regression found:
# these are launchd's OWN vproc/job.defs-generated MIG CLIENT stubs --
# real IPC protocol code, not inert data/wrapper functions like the other
# members stripped below. They matched "covered by loaded-dylib closure"
# only because libxpc.dylib bundles its own same-named legacy vproc_mig
# client stubs (real Apple compat shim) reachable via reexport -- but
# libxpc.dylib's copy talks the GENERIC macOS vproc/job MIG wire format,
# not necessarily byte-identical to whatever job.defs/vproc.defs revision
# this project's OWN liblaunchd_core.a MIG SERVER side (job_mig_server.c
# et al) actually implements. Stripping these 5 members and relying on
# libxpc.dylib's copy stalled launchd's own boot dead right after
# "remounted / read-write" -- consistent with a client/server MIG
# protocol mismatch (message sent, server never replies, no crash, no
# error, just a hang). DAR-234 itself already flagged this exact
# _vproc_mig_*/_job_mig_* symbol family as needing a real per-symbol
# check, not a blanket strip -- this is that check's answer. Do NOT
# re-strip these.
rm -f "$OUT/liblaunchd_syscalls_no_dup.a"
cp "$OUT/liblaunchd_syscalls.a" "$OUT/liblaunchd_syscalls_no_dup.a"
ar d "$OUT/liblaunchd_syscalls_no_dup.a" \
    isthreaded_compat.o siglist.o sys____munmap.o sys__dup.o \
    sys__kevent.o sys__mkdirat.o uuid_pack.o

rm -f "$OUT/launchd_libc_extras_no_dup.a"
cp "$OUT/launchd_libc_extras.a" "$OUT/launchd_libc_extras_no_dup.a"
# getifaddrs.o kept (NOT stripped) 2026-09-03, real QEMU regression found:
# see cf_libc_extras.a's copyfile_syscalls.o comment below -- same "same
# name, wrong dylib" trap, this time via libSystem.B.dylib's reexport of
# libresolv.dylib, which also happens to define real, local (non-reexport)
# _getifaddrs/_freeifaddrs/_gethostname.
ar d "$OUT/launchd_libc_extras_no_dup.a" \
    copy.o fstatat64.o heapsort.o Libc_sys_sigaction.o \
    libplatform_sigaction.o libresolv_getentropy_syscall.o \
    ls_fenv_compat.o qsort.o removefile_syscalls.o

rm -f "$OUT/cf_libc_extras_no_dup.a"
cp "$OUT/cf_libc_extras.a" "$OUT/cf_libc_extras_no_dup.a"
# copyfile_syscalls.o/gethostname.o kept (NOT stripped) 2026-09-03: a
# first attempt stripped both, believing them pure duplicates of
# libcopyfile.dylib/libresolv.dylib (both real, reachable only via
# libSystem.B.dylib's own LC_REEXPORT_DYLIB, per this file's established
# "list the real owner explicitly" pattern) -- confirmed via
# `dyld_info -exports` that libcopyfile.dylib genuinely, locally (not
# `[re-export]`) defines _getpwnam/_getgrnam/_getpwuid/_syslog too, same
# as libresolv.dylib genuinely defines _gethostname/_getifaddrs/
# _freeifaddrs. The link succeeded, but a real QEMU boot-test then
# stalled launchd's own boot dead right after "remounted / read-write",
# never reaching job spawn -- copyfile_syscalls.o is this project's OWN
# real getpwnam/getgrnam/getpwuid/syslog implementation (a thin wrapper
# over libinfo_custom.o's own `_li_getpwnam` family, this project's
# real flat-file /etc/master.passwd reader per docs/
# multiuser-userland-plan.md), NOT a duplicate of whatever
# libcopyfile.dylib/libresolv.dylib's own build accidentally bundled
# under the same symbol names (same "same name, different and untested
# code" trap as the cf_socket_syscalls.o finding logged elsewhere this
# session -- libCoreFoundation.a's own private socket syscalls, never
# swept). "Same symbol name reachable via reexport" is NOT sufficient
# evidence of a safe duplicate; only dylibs in this project's
# already-vetted CORE stack (libsystem_kernel/libsystem_c/
# libsystem_pthread/libsystem_platform/libsystem_malloc/libdispatch/
# libdyld) get this treatment without a real boot-test confirming it.
# Do NOT re-strip these, and do NOT re-add libresolv.dylib/
# libcopyfile.dylib to this link purely to satisfy them.
ar d "$OUT/cf_libc_extras_no_dup.a" \
    geteuid.o getuid.o

rm -f "$OUT/launchd.macho"
# -DCRT_DYNAMIC_LINKING (DAR-153, 2026-09-02): launchd.macho is a real,
# genuinely dynamically-linked executable (libdispatch/libdyld/libSystem.B/
# libsystem_pthread/libsystem_c/libxpc/... dylibs below), but this flag was
# missing here even though the identical DAR-161 fix was already applied to
# build_launchctl.sh. Without it, Csu/crt.c's own NXArgc/NXArgv/environ/
# __progname become LOCAL storage in launchd.macho instead of extern
# references to the real, canonical copies libdyld.dylib's libdyldGlue.o
# exports -- confirmed via `nm -m launchd.macho` showing `(__DATA,__common)
# external _environ` (a local definition) rather than an import/bind. Real,
# concrete consequence found via QEMU boot-test: job_start()'s child calls
# the real, dylib-bound setenv(LAUNCHD_TRUSTED_FD_ENV, ...) (correctly
# resolves "(from libsystem_c)", writes into libsystem_c.dylib's OWN
# internal environ storage), but job_start_child()'s final
# posix_spawn(..., environ) reads launchd.macho's OWN separate local
# `environ` global (this file's plain identifier reference) -- which
# setenv() never touched. The result: every checkin-requesting child
# (e.g. syslogd, DAR-153) is exec'd with an environment that never
# contains LAUNCHD_TRUSTED_FD_ENV, so its own launch_msg(LAUNCH_KEY_CHECKIN)
# always returns NULL and it exit(1)s immediately, respawns, repeats.
# Unifying `environ` onto the one real dyld-owned copy (this flag) makes
# setenv() and posix_spawn(...,environ) agree, same fix shape as
# build_launchctl.sh's own DAR-161 remediation.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-u,__current_pid \
    -Wl,-alias,__mh_execute_header,__mh_dylib_header \
    -Wl,-alias,_kCFDateFormatterCalendarKey,_kCFDateFormatterCalendar \
    -Wl,-alias,_kCFDateFormatterTimeZoneKey,_kCFDateFormatterTimeZone \
    -Wl,-alias,_kCFCalendarIdentifierGregorian,_kCFGregorianCalendar \
    -Wl,-alias,_kCFLocaleCalendarKey,_kCFLocaleCalendar \
    -Wl,-alias,_kCFLocaleCountryCodeKey,_kCFLocaleCountryCode \
    -Wl,-alias,_kCFLocaleLanguageCodeKey,_kCFLocaleLanguageCode \
    -Wl,-alias,_kCFLocaleScriptCodeKey,_kCFLocaleScriptCode \
    -o "$OUT/launchd.macho" \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    -Wl,-force_load,"$OUT/liblaunchd_core.a" \
    "$OUT/libCoreFoundation.a" "$OUT/cf_libc_extras_no_dup.a" "$OUT/launchd_libc_extras_no_dup.a" \
    "$OUT/liblaunchd_mig_no_dup.a" "$OUT/liblaunchd_syscalls_no_dup.a" \
    "$OUT/libmach_ipc_no_dead_stubs.a" "$OUT/mach_port_gap_closures2.o" "$OUT/mach_port_gap_closures2_traps.o" \
    "$OUT/mach_ipc_dead_stubs_voucher_only.o" \
    `# libclosure.a/libplatform.a REMOVED 2026-09-03 (dynamicfix sweep):` \
    `# same "one owner per symbol" rule as the libmalloc.a/libpthread.a` \
    `# removals documented below -- the Blocks runtime and the platform` \
    `# (os_unfair_lock/os_once) primitives are real exports of` \
    `# libsystem_blocks.dylib/libsystem_platform.dylib, reached here via` \
    `# libSystem.B.dylib's own reexport chain.` \
    "$OUT/chk_fail.o" "$OUT/sys_getpid_current_pid_only.o" \
    `# libpthread_no_dup_pthread_create.a REMOVED 2026-08-30 (docs/` \
    `# daemon-roadmap.md Phase 6.8 continued item 1): launchd.macho must` \
    `# NOT carry its own static copy of the pthread implementation` \
    `# alongside the real libsystem_pthread.dylib it already links. It` \
    `# did, and that was the root cause of a real, QEMU-diagnosed boot` \
    `# failure -- "one owner per symbol", the same invariant commit` \
    `# 9ee721b established, applied to pthread state specifically.` \
    `#` \
    `# The duplication was not benign: _main_thread is an EXTERNAL COMMON` \
    `# symbol, so the two copies coalesced at runtime into launchd's one` \
    `# struct, while _pthread_ptr_munge_token is NON-external in both and` \
    `# did NOT coalesce. Both copies of pthread_main_thread_bootstrap()` \
    `# therefore signed the SAME shared _main_thread with DIFFERENT` \
    `# private tokens, and whichever ran second won -- after which every` \
    `# pthread_self() served by libsystem_pthread.dylib (the copy real` \
    `# CoreFoundation, libdispatch and libc++ actually bind to) failed` \
    `# _pthread_validate_signature() and called abort_with_reason(), a` \
    `# silent exit(1) with zero console output. Proven by reading the` \
    `# munge tokens out of both images from the kernel at exit(2) time:` \
    `# (sig ^ launchd_token) == pthread_t exactly, (sig ^` \
    `# libsystem_pthread_token) did not.` \
    `#` \
    `# Dropping the archive relinks cleanly with ZERO undefined symbols` \
    `# (verified via nm -m: _pthread_self/_pthread_mutex_lock now resolve` \
    `# "(from libsystem_pthread)") because libsystem_pthread.dylib really` \
    `# does export the whole surface this binary needs. Do NOT re-add it.` \
    `# libc_gen.a/libc_string.a/libc_stdio.a/libc_printf.a/` \
    `# libc_locale_for_stdio_no_tsd_shim.a REMOVED 2026-09-03 (dynamicfix` \
    `# sweep): all five are merged wholesale into libsystem_c.dylib by` \
    `# build_libsystem_c.sh, which this link already loads. Carrying them` \
    `# statically here gave launchd private duplicate stdio FILE tables and` \
    `# locale state alongside libsystem_c.dylib's own.` \
    `#` \
    `# The glob.o/___locale_key fixup error this comment used to describe` \
    `# (arm64_adrp_lo12 relocation failure, ___locale_key not GOT-indirect)` \
    `# was fixed for real 2026-09-03 (DAR-234, commit 9748500) by` \
    `# recompiling liblaunchd_syscalls.a's glob.o to treat ___locale_key as` \
    `# an external import. This link succeeds today with 0 undefined` \
    `# symbols from that cause -- do not re-add libc_locale_for_stdio*.a.` \
    "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libxpc.dylib" "$OUT/system/libbsm.dylib" \
    "$OUT/system/libdispatch.dylib" "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib" \
    "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_malloc.dylib" "$OUT/system/libremovefile.dylib" \
    "$OUT/system/libsystem_c.dylib" \
    `# libresolv.dylib/libcopyfile.dylib deliberately NOT added here` \
    `# (tried 2026-09-03, reverted): they do genuinely, locally export` \
    `# _gethostname/_getifaddrs/_freeifaddrs and` \
    `# _getgrnam/_getpwnam/_getpwuid/_syslog respectively, but a real QEMU` \
    `# boot-test showed those are NOT the same code as this project's own` \
    `# real implementations (cf_libc_extras.a's copyfile_syscalls.o,` \
    `# launchd_libc_extras.a's getifaddrs.o/gethostname.o) -- linking` \
    `# these dylibs in stalled launchd's boot dead right after "remounted` \
    `# / read-write". See those archives' own "kept (NOT stripped)"` \
    `# comments above for the real story. Same symbol name reachable via` \
    `# a dylib's reexport chain is NOT sufficient evidence of a safe` \
    `# duplicate for anything outside this project's already-vetted core` \
    `# dylib stack.` \
    `# libsystem_platform.dylib listed EXPLICITLY, same reexport-depth` \
    `# reasoning as libsystem_malloc.dylib/libremovefile.dylib above.` \
    `# Added 2026-09-03 with the libplatform.a removal: it is the real` \
    `# owner of _mach_absolute_time/_mach_timebase_info (confirmed via` \
    `# nm -gU), which liblaunchd_core.a's runtime.o and CoreFoundation's` \
    `# CFDate.o/CFRunLoop.o both need. The archive had been their only` \
    `# supplier here purely because this dylib was never named.` \
    "$OUT/system/libsystem_platform.dylib" \
    "$ICU_LIB/libicuuc.dylib" "$ICU_LIB/libicui18n.dylib" "$ICU_LIB/libicudata.dylib" "$OUT/system/libc++.dylib" \
    2>"$OUT/launchd_link.err" \
    || { echo "error: real link failed -- see $OUT/launchd_link.err" >&2; tail -60 "$OUT/launchd_link.err" >&2; exit 1; }

echo "wrote $OUT/launchd.macho"

# Real smoketest: not just "did the link succeed" (a plain, non-force_load
# link can trivially succeed with 0 undefined symbols while silently
# missing core.o/ipc.o/log.o entirely, per this script's own header
# comment) -- confirm real daemon content actually linked in, via a
# distinctive, real, externally-visible core.c-only symbol
# (job_dispatch, the job-state-machine's own real entry point).
otool -hv "$OUT/launchd.macho" | grep -q " EXECUTE " || {
    echo "WARNING: not a real MH_EXECUTE" >&2
}
otool -hv "$OUT/launchd.macho" | grep -q NOUNDEFS || {
    echo "WARNING: NOUNDEFS not set -- real undefined symbols may remain" >&2
}
if nm "$OUT/launchd.macho" 2>/dev/null | grep -q "T _job_dispatch$"; then
    echo "launchd.macho: real core.c content confirmed linked (_job_dispatch present)"
else
    echo "WARNING: _job_dispatch not found -- core.o may not have actually linked in (see this script's own -force_load comment)" >&2
fi
echo "linked against:"
otool -L "$OUT/launchd.macho" 2>/dev/null | tail -n +2
