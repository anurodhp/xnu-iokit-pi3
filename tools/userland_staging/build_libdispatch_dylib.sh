#!/bin/bash
# Builds libc_build/system/libdispatch.dylib: a real dylib, matching
# this project's standing 2026-08-25 directive (docs/daemon-roadmap.md's
# "Static-vs-dylib architecture debt" section) that new work should
# produce a real per-library `.dylib`, not a new static archive --
# real Apple ships libdispatch at exactly this real installed path,
# `/usr/lib/system/libdispatch.dylib`, alongside `libsystem_kernel.dylib`
# etc (confirmed: it is its OWN separate image on real Darwin, not
# folded into `libSystem.B.dylib` the way this project's own umbrella
# `libSystem.B.dylib` currently folds everything together -- see
# CLAUDE.md's own note that that flattening is migration debt, not a
# design goal).
#
# Reuses the already-built, already-verified object files from
# build_libdispatch_real.sh's own libc_build/dispatch_obj/ (`ar x`-free
# here since those were never archived into anything this script needs
# to unpack -- build_libdispatch_real.sh writes loose .o's directly)
# rather than recompiling.
#
# ============================================================
# HONEST SCOPE STATEMENT -- read this first
# ============================================================
# REAL, KNOWN ARCHITECTURE DEBT this script does NOT fix, filed instead
# as a followup (see docs/backlog.md's new "library debt" section): this
# dylib is built `-nostdlib` and STATICALLY bundles its current
# dependency stack (libmach_ipc.a/libclosure.a/libmalloc.a/libplatform.a/
# libpthread.a/libc_*.a), exactly like every static archive consumer of
# those same archives already does. That means libdispatch.dylib gets
# its OWN private copy of every process-global piece of state those
# archives carry (mach_task_self_, g_zone, errno, etc) -- the SAME real
# bug class build_libsystem_malloc.sh's own header comment already
# documents and fixed for libdyld.dylib/libSystem.B.dylib (two
# different dylibs, each with a silently-separate heap zone, real heap
# corruption risk). Until this dylib is relinked against the REAL
# sibling dylibs this project already built (libsystem_kernel.dylib/
# libsystem_platform.dylib/libsystem_malloc.dylib/libsystem_pthread.dylib/
# libsystem_c.dylib) instead of re-bundling their pre-dylib static
# ancestors, any process loading both libdispatch.dylib and one of those
# sibling dylibs has two independent copies of the same "one true
# global" state -- do not treat this dylib as safe to load alongside
# them in a real multi-dylib process until that follow-up lands.
#
# The real, categorized undefined symbols build_libdispatch_real.sh's
# own HONEST SCOPE STATEMENT lists (mostly libpthread's own missing
# standard POSIX/QoS API) are tolerated here via a real, explicit
# per-symbol `-Wl,-U,<name>` exception list -- the SAME established
# mechanism build_libsystem_c.sh's own link already uses for
# `___progname` (a real symbol resolved by whatever loads the dylib,
# not baked in). A blanket `-Wl,-undefined,dynamic_lookup` was tried
# first and does NOT work in this environment: real ld64 attempts to
# resolve each dynamic_lookup candidate against the HOST macOS's own
# real dyld shared cache, and hard-fails (rather than deferring) for any
# name that cache actually recognizes -- which is most of these, since
# they're real Apple symbol names by construction. `-U` avoids that
# lookup entirely per-symbol. This is NOT the same as real Apple's
# actual two-level-namespace dylibs (which record an explicit, specific
# dependent-dylib-and-symbol pair for every import) -- it's an honest,
# deliberate shortcut, not a claim this dylib's import table matches
# real Apple's. Doing this properly (explicit `-l`-style dependencies on
# this project's own already-real libsystem_kernel.dylib/
# libsystem_pthread.dylib/etc, once those actually implement the missing
# symbols for real) is the same follow-up as the duplicate-global-state
# fix above, not a separate task.
#
# Real fix 2026-08-27 (found bringing up syslogd): `__os_semaphore_create`/
# `__os_semaphore_dispose` used to be deferred here as if they were
# external Mach-loader dependencies, but they're not -- real, small,
# self-contained implementations already exist at
# third_party/libplatform/src/os/semaphore.c (same class of omission as
# shims/yield.c, see build_libdispatch_real.sh's own updated header),
# just never compiled into libdispatch_real.a before. Real, still-
# external Mach traps that file's own body needs instead:
# `_semaphore_create`/`_semaphore_destroy` (real raw Mach traps, not
# curated anywhere in this dylib's own dependency stack, unlike
# `_semaphore_signal`/`_semaphore_wait`, which this script's own
# `semaphore_wrap.o` extraction below already covers) -- deferred below,
# same honest `-U` mechanism as every other real Mach trap in this list.
#
# Real fix 2026-08-27 (pthread QoS/attr subsystem): ~20 of the entries
# this list used to carry (pthread_qos_class_encode and its QoS-class/
# workqueue-override siblings, pthread_attr_init/destroy/setdetachstate/
# setschedparam/setschedpolicy/getschedparam/getschedpolicy/
# setstacksize, pthread_exit, pthread_sigmask) are now real, closed by
# build_libpthread_qos.sh's new libc_build/libpthread_qos.a -- see that
# script's own HONEST SCOPE STATEMENT for the full real finding
# (build_libpthread.sh's "qos.c unreachable per the kernel constraint"
# comment predates Phase 7.5's real bsdthread_create/register port and
# is now only true for the narrower __bsdthread_ctl-backed functions,
# which get a real, honest kernel ENOTSUP at runtime -- same accepted
# pattern as pthread_create() itself). Kept in this project's master
# libpthread.a is deliberately NOT touched (real regression risk to
# bash/ncurses/getty/syslogd, already real+QEMU-verified consumers) --
# libpthread_qos.a is its own separate, isolated archive, used only
# here. pthread_get_stackaddr_np/pthread_setschedparam stay honestly
# deferred below -- real thread-list-tracking ABI risk, not yet verified
# against this project's own from-scratch pthread_create() (see
# libpthread_attr_compat.c's own header for the full real trace).
# pthread_workloop_create/destroy, clock_gettime_nsec_np, and the real
# mach_voucher_*/host_get_special_port MIG routines are now real too
# (same libpthread_qos.a / libpthread_attr_compat.c /
# libpthread_qos_compat.c, plus a real mach_voucher.defs MIG regen and a
# reused host_privUser.o already built elsewhere in this project) --
# see those files' own headers for the full per-symbol trace.
#
# host_create_mach_voucher() (plain name) was initially thought to be a
# genuine version-skew gap (confirmed absent from this project's own
# pinned xnu-7195 osfmk/mach/host_priv.defs entirely) -- but this
# project's own libmach_ipc.a already has the real, trap-based
# __kernelrpc_host_create_mach_voucher from earlier work, just never
# had the plain-named public forward -- see libpthread_qos_compat.c's
# own header for the correction. Real, now closed, not deferred.
#
# Real Step 2 progress, 2026-08-27: of the ~54 symbols this list used to
# carry, 20 are real, already-defined exports of sibling dylibs already
# built in this project (`libc_build/system/libsystem_c.dylib`/
# `libsystem_malloc.dylib`/`libsystem_pthread.dylib`, confirmed via a
# real `nm -gU` sweep of each, not assumed) -- exactly the
# "real DEPENDENCY on libSystem's libc portion at runtime" fix this
# script's own header comment already called for, rather than more
# static-archive extraction. Those 20 are removed from this deferred
# list below and the dylib is now linked against those 3 siblings for
# real (see the final link command) -- same established pattern
# `build_libunwind_dylib.sh` already uses for its own sibling deps.
# `libsystem_kernel.dylib`/`libsystem_platform.dylib`/
# `libsystem_blocks.dylib` were checked too (real `nm -gU` sweep) and
# genuinely do NOT export any of the remaining symbols below yet --
# linking them in would add real LC_LOAD_DYLIB weight for zero symbol
# resolution, so they're deliberately not added until they actually
# grow the needed exports.
#
# _gettimeofday and _geteuid closed the same day, real code not sibling-
# dylib import: _gettimeofday now has a real, narrow raw-syscall #116
# body added directly to libpthread_qos_compat.c (verbatim copy of the
# already-QEMU-verified pattern in ncurses_extra_syscalls.c, kept
# separate rather than sharing that file's object to avoid pulling in
# its unrelated strstr()/lib_termcap.c compat bodies). _geteuid is a
# real, narrow single-object extraction (geteuid.o) from this project's
# own syslogd_libc_extras.a -- its only dependency, _cerror_nocancel, is
# already resolved for real via the libsystem_c.dylib sibling link above
# (confirmed via nm before extracting, not assumed), so this adds zero
# new gaps.
#
# _atoi closed the same way, one level deeper: atoi.o (same archive as
# geteuid.o) needs ___global_locale/___locale_key. Pulling in the whole
# xlocale.o that defines them was tried and rejected (real-cascades into
# __collate_load_tables/__monetary_load_locale/__numeric_load_locale/
# __time_load_locale/__messages_load_locale/__setrunelocale/
# __detect_path_locale/__get_locale_env -- confirmed via a real nm sweep
# of libc_build/libc_locale.a's own xlocale.o member, same bloat class
# already rejected for strcasecmp/getprogname above). Instead:
# libc_locale_globals_compat.c (new sibling file, see its own header for
# the full real trace) defines just the two real globals verbatim from
# xlocale.c, plus real table.o/none.o (locale/FreeBSD/table.c's own
# _DefaultRuneXLocale + its self-contained __none_* dependency) extracted
# from libc_build/libc_locale.a -- confirmed via nm that their only real
# deps (___error/_memchr) are already resolved elsewhere in this link.
#
# _environ, checked but NOT changed: confirmed via a real `nm -m` on
# syslogd.macho itself that it has a real, defined, external `_environ`
# (`third_party/Csu/crt.c`'s own real global, populated by `start.s`
# before `main()`, per that project's own Phase 4.6 writeup in
# simple_cmds_getenv_compat.c's header) -- so `_environ` deferred via
# `-Wl,-U` below is NOT a real gap needing new code, it's the CORRECT
# real architecture (same as real Apple's own libSystem: `environ` is
# provided by the loading executable's own crt, not by libSystem itself).
# Left deferred/dynamically-looked-up on purpose, not fixed further.
# 2026-08-27: closed kevent_qos, kpersona_get, pread/pwrite, sigsuspend,
# sleep, semaphore_timedwait, malloc_engaged_nano/memory_event_handler,
# mach_continuous_time/mach_get_times, mach_make_memory_entry_64, and all
# 7 mach_port_* + thread_destruct_special_reply_port real gaps -- the
# full trail of how each was found/verified used to be cited via the
# now-retired tools/init_binary/init.c's spawn_syslogd() comment (see
# docs/static-linking-cleanup-plan.md for the 2026-08-31 retirement;
# that specific comment's content was not preserved elsewhere). Real,
# still-open gaps kept below: the 7-symbol mach_eventlink_* family
# (genuinely needs new MIG regen against mach_eventlink.defs, out of
# this pass's scope) and pthread_get_stackaddr_np (thread-list-tracking
# risk, not yet verified safe -- see this file's own earlier note).
# __mach_errors/_environ stay listed but are NOT real gaps -- see this
# script's own header comment above (both are the correct real
# architecture, resolved by whatever loads this dylib, same as real
# Apple). _pthread_setschedparam/_semaphore_create/_semaphore_destroy/
# _mach_port_get_context/_mach_port_set_context were never real gaps in
# the first place (already resolved elsewhere, confirmed via gap_audit.sh
# -- -U on an already-defined symbol is a harmless no-op, not removed to
# avoid churn).
# _thread_get_special_reply_port REMOVED from this array 2026-08-31
# (DAR-124/DAR-8/DAR-22 sweep continuation): this WAS a real, confirmed-
# broken flat-namespace deferral -- `xcrun dyld_info -fixups` showed a
# real `<flat-namespace>` bind here, resolved today only because
# launchd.macho happens to compile a local copy (build_launchd_core.sh),
# and a real, already-shipped second consumer (syslogd.macho: loads this
# dylib + libsystem_notify.dylib + libxpc.dylib directly, confirmed via
# otool -L, and does NOT define this symbol anywhere in its own object
# code, confirmed via `nm -g`) would have hit "symbol not found in flat
# namespace" the instant it's actually spawned (DAR-153). Real Apple's
# actual home for this symbol is libsystem_kernel.dylib (a plain 2-
# instruction Mach trap, kernel_trap(thread_get_special_reply_port,-50,0)
# -- same tier as every other hand-written trap .s that dylib already
# compiles) -- added there this round (see build_libsystem_kernel.sh's
# own updated comment), already a linked sibling here, so this dylib's
# own undefined reference now resolves via a real two-level bind instead
# of flat-namespace lookup. Confirmed via a fresh `xcrun dyld_info
# -fixups` after rebuild: zero remaining `<flat-namespace>` entries in
# this dylib.
UNDEF_SYMS=(
    _pthread_setschedparam
    __mach_errors _environ
    _mach_port_get_context _mach_port_set_context
    _semaphore_create _semaphore_destroy
)
# Real, defined-elsewhere-in-the-process symbols this dylib's own
# gap-closure .o's (mach_port_gap_closures2.o/cf_mach_port_syscalls.o)
# reference but can't resolve at THIS dylib's own standalone `-nostdlib`
# link time, because the archive that really defines them
# (libxpc_syscalls2.a's mach_portUser.o for the 11 _kernelrpc_mach_port_*
# MIG fallbacks) is deliberately NOT linked into this narrow dylib --
# same real flat-namespace mechanism as every other -U symbol here: they
# resolve for real once syslogd.macho (which DOES link that archive)
# loads this dylib, confirmed via nm -g syslogd.macho showing the MIG
# fallbacks already real+defined there. NOT the same class as this
# dylib's own honestly-still-open gaps above -- kept in a separate array
# to avoid muddying that list's meaning.
#
# _nanosleep REMOVED from this array 2026-08-31 (static-linking cleanup,
# "needs a new home" batch): it was here only for dispatch_gap_
# closures4.o's own sleep(), which is now gone from this dylib entirely
# (moved to libsystem_c.dylib, see that file's own updated header) --
# confirmed via nm -m that no other object in this dylib's own extract
# dir references `_nanosleep`.
UNDEF_SYMS_ELSEWHERE_IN_PROCESS=(
    __kernelrpc_mach_port_allocate __kernelrpc_mach_port_construct
    __kernelrpc_mach_port_destruct __kernelrpc_mach_port_extract_member
    __kernelrpc_mach_port_extract_right __kernelrpc_mach_port_get_attributes
    __kernelrpc_mach_port_insert_member __kernelrpc_mach_port_insert_right
    __kernelrpc_mach_port_kernel_object __kernelrpc_mach_port_request_notification
    __kernelrpc_mach_port_type __kernelrpc_mach_port_unguard
)
UNDEF_SYMS+=("${UNDEF_SYMS_ELSEWHERE_IN_PROCESS[@]}")
UNDEF_FLAGS=()
for s in "${UNDEF_SYMS[@]}"; do
    UNDEF_FLAGS+=(-Wl,-U,"$s")
done
set -e
cd "$(dirname "$0")"

OUT="libc_build"

if [ ! -d "$OUT/dispatch_obj" ]; then
    echo "error: $OUT/dispatch_obj not found -- run build_libdispatch_real.sh first" >&2
    exit 1
fi
# Real gap, found 2026-09-13 (surfaced immediately by the very next
# build_everything.sh attempt after removing this script's dead
# syslogd_libc_extras.a check, which had been the only thing anchoring
# it late enough in the order for these 4 real prerequisites to
# accidentally already be satisfied): this combined error message names
# no literal build_X.sh, so gen_build_order.py's parser recognized none
# of these real dependencies. Named individually here.
if [ ! -f "$OUT/system/libsystem_c.dylib" ]; then
    echo "error: $OUT/system/libsystem_c.dylib not found -- run build_libsystem_c.sh first" >&2
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
if [ ! -f "$OUT/system/libsystem_kernel.dylib" ]; then
    echo "error: $OUT/system/libsystem_kernel.dylib not found -- run build_libsystem_kernel.sh first" >&2
    exit 1
fi
# syslogd_libc_extras.a check REMOVED 2026-09-13 (found via this
# session's own forward-reference audit): its only real purpose was
# geteuid.o/atoi.o, both already removed from this script's own
# extraction on 2026-08-29 in favor of the sibling-dylib exports
# documented in the "Objects removed" comment above -- nothing in this
# script has extracted from syslogd_libc_extras.a since, so this check
# was pure dead weight left behind by that cleanup. Real impact beyond
# the dead check itself: it created a genuine, large, accidental
# dependency edge (build_libdispatch_dylib.sh, needed very early by
# almost everything, transitively requiring build_syslogd.sh, needed
# very late) that closed a real 5-way cycle once build_syslogd.sh
# separately gained its own real build_launchd_core.sh dependency this
# same session (syslogd -> launchd_core -> corefoundation -> libxml2 ->
# this script -> syslogd) -- confirmed via gen_build_order.py: removing
# this dead check dropped the cycle-warning count from 12 back to the
# expected baseline and restored build_launchd_core.sh's real, verified
# position after build_corefoundation.sh (DAR-cycle-fix earlier this
# session -- that ordering is load-bearing, not incidental).

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
LD="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
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

# libmach_ipc.a's own mach_ipc_dead_stubs.o is real, confirmed-dead
# weight now: all 4 of its symbols (_mach_port_extract_right/
# _insert_right/_unguard, _voucher_mach_msg_set) are already provided
# for real elsewhere in this exact link (voucher.o's own real body for
# the last one; real mach_portUser.o-family MIG output for the first 3
# -- confirmed via `nm` before excluding, not assumed) -- extracted here
# rather than archived-in-whole to avoid a real duplicate-symbol link
# error, same "ar x, exclude the now-redundant member" precedent
# build_libsystem_malloc.sh already established for a different archive.
mkdir -p "$OUT/dispatch_dylib_extract"
rm -f "$OUT"/dispatch_dylib_extract/*.o
# mach_msg_real.o excluded too (real gap found+fixed 2026-08-28 booting
# launchd as PID 1 alongside this dylib): mach_msg()/mach_msg_trap() are
# now real, canonical exports of libsystem_kernel.dylib (see
# build_libsystem_kernel.sh's own header) -- this dylib's own private
# static copy of the same functions was winning symbol resolution for
# launchd.macho's own real _kernelrpc_mach_port_move_member() call
# (confirmed via a real instrumentation pass), the exact same duplicate-
# global-state risk this file's own HONEST SCOPE STATEMENT above already
# flagged, now closed for this specific symbol pair the same way
# mach_task_self_ already was.
# libmach_ipc.a is NO LONGER extracted here (static-linking cleanup,
# Phase 1a, 2026-08-29). Its whole real content -- the generated MIG
# client stubs for clock.defs/mach_host.defs/task.defs, mig_strncpy,
# mach_host_self and the g_mach_ipc_clock_port/_sem cache -- now lives in
# libsystem_kernel.dylib, already a sibling on this link line (see
# build_libsystem_kernel.sh's own comment for the full rationale,
# including the two real duplicate-GLOBALS this closes). The
# mach_ipc_dead_stubs.o / mach_msg_real.o exclusions that used to be
# needed here are handled at that end now, once, instead of separately in
# every image that bundled the archive.

# --- Objects removed 2026-08-29 (static-linking cleanup, Phase 1a) ---
# Each of these was a private copy of something a sibling dylib already
# on this link line now owns outright, so each was a real duplicate
# definition in any process that loaded both:
#   geteuid.o                     -> libsystem_kernel.dylib (_geteuid)
#   pread_trap.o / pwrite_trap.o  -> libsystem_kernel.dylib
#   stack_chk_compat.o            -> libsystem_c.dylib (___stack_chk_fail,
#                                    ___stack_chk_guard, ___chkstk_darwin)
#   atoi.o                        -> libsystem_c.dylib (_atoi, _atoi_l)
#   table.o / none.o /            -> libsystem_c.dylib. These carried
#   libc_locale_globals_compat.o     __CurrentRuneLocale,
#                                    __DefaultRuneLocale, ___mb_cur_max,
#                                    ___mb_sb_limit, ___global_locale and
#                                    ___locale_key -- real, mutable
#                                    process-global locale state, so this
#                                    one was duplicate STATE, not just
#                                    duplicate code.
# The long comments that used to explain why each was extracted here are
# removed with them; build_libsystem_c.sh / build_libsystem_kernel.sh are
# the place to look now.




# libc_build/dyld_core.a's own mach_portUser.o/vm_mapUser.o were tried
# here (they have real mach_port_construct/destruct/get_context/etc and
# mach_make_memory_entry_64) but real-conflict on 9 OTHER symbols
# (_vm_allocate/_deallocate/_map/_protect/_remap/_purgable_control/_read,
# _mach_port_mod_refs/_deallocate) against libmalloc.a's own real
# mach_vm.o/libmalloc_dispatch.o bodies for the exact same functions --
# both real, just built at different points in this project's history.
# Deferred via `-Wl,-U` below instead of fighting that conflict (real
# followup: same de-duplication task as pthread_key_init_np above).

# real semaphore_signal()/semaphore_wait() curated bodies already built
# for the libxpc syscalls tier (build_libxpc_syscalls.sh's own
# mach_semaphore_wrap_compat.o) -- reused, not regenerated.
# semaphore_wrap.o no longer copied here (static-linking cleanup, Phase
# 1a, 2026-08-29): _semaphore_signal/_semaphore_wait are now real exports
# of libsystem_kernel.dylib, a sibling on this link line.

# real third_party/Libc/gen/FreeBSD locale/string primitives
# (strcasecmp, sys_errlist/sys_nerr) this dylib's own code needs --
# extracted from libc_locale_for_stdio.a EXCLUDING pthread_tsd_shim.o,
# which real-but-redundantly redefines `pthread_key_init_np` (this
# project's real libpthread.a already has its own, canonical copy -- a
# genuine duplicate-symbol conflict between two archives built at
# different points in this project's history, not a fabricated
# exclusion). Two real attempts at pulling libc_locale_for_stdio.a's
# strcasecmp/getprogname/toupper/raw-fstat-open-etc members (first the
# whole archive, then a hand-picked subset) both cascaded into that
# archive's own real, much larger locale-loading subsystem
# (_modifylocale -> collate/messages/monetary/numeric/setrunelocale
# table loaders; ___time_load_locale -> its own extra table loader;
# real stack-protector globals from a DIFFERENT pre-built object
# entirely) -- none of which libdispatch itself needs, just transitive
# bloat from sharing object files with unrelated real functionality.
# Deferred via `-Wl,-U` below instead (`_strcasecmp`/`_getprogname`/
# `_sys_errlist`/`_sys_nerr`/`___toupper`/`___toupper_l`/`__fstat`/
# `__open`/`__close`/`__read`/`__write`/`_close`) -- real Apple's own
# libdispatch.dylib gets these from a real DEPENDENCY on libSystem's
# libc portion at runtime, not by bundling libc's internals into
# itself; this project's own libsystem_c.dylib already exists for
# exactly that real dependency (see docs/backlog.md's new library-debt
# item -- relink against it instead of static bundling, once this
# dylib's mach_task_self_/g_zone/etc duplicate-global-state issue is
# fixed the same way).

# real mach_error_string()/semaphore_{signal,wait}_trap()/os_assumes
# bodies -- already built for the libxpc tier, reused not regenerated.
# mach_error_string.o / mach_semaphore_traps.o no longer copied here
# (static-linking cleanup, Phase 1a, 2026-08-29) -- _mach_error_string/
# _mach_error_type and the raw semaphore traps are now real exports of
# libsystem_kernel.dylib. os_assumes_compat.o stays for now: nothing
# below this dylib owns the __os_assumes_log/__os_crash family yet
# (real Apple puts it in libsystem_c) -- still duplicated with
# libxpc.dylib, tracked as remaining work.
# os_assumes_compat.o no longer copied here (static-linking cleanup,
# Phase 1a, 2026-08-29): the __os_assumes_log/__os_assert_log/__os_crash
# family is now owned by libsystem_c.dylib, a sibling on this link line,
# matching real Apple. libxpc.dylib had a second copy of the same object.
# getenv_compat.o no longer copied here (static-linking cleanup, Phase
# 1a, 2026-08-29): the whole environment-variable subsystem (getenv,
# setenv/unsetenv/putenv and their $UNIX2003 aliases, _NSGetEnviron,
# ___environ_lock) is now owned by libsystem_c.dylib, a sibling on this
# link line. It used to exist only in this dylib and libxpc.dylib, each
# with its own lock over the one real `environ` array.



# real raw trap for ___kdebug_trace64 (BSD syscall #179, NO_SYSCALL_STUB
# -- confirmed real trap number in bsd/kern/syscalls.master), closing a
# real gap this dylib's own dispatch_prefix_compat.h kdebug_trace()
# wrapper needs. `-Wl,-undefined,dynamic_lookup` (below) cannot defer
# this one specifically -- real ld64 quirk: it tries to resolve raw
# double-underscore trap names against the HOST macOS's own real dyld
# shared cache metadata even for this iOS-target build, and fails
# outright rather than deferring, unlike every other undefined symbol
# here. A real local definition sidesteps that path entirely.
mkdir -p "$OUT/sys_asm_compat/mach/arm64"
cat > "$OUT/sys_asm_compat/mach/arm64/asm.h" <<'EOF'
#include <arm64/asm.h>
EOF
XNU_FOR_TRAP="../../third_party/xnu-7195"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -arch arm64 \
    -I"$OUT/sys_asm_compat" -I"$XNU_FOR_TRAP/libsyscall/custom" -I"$XNU_FOR_TRAP/libsyscall/mach" -I"$XNU_FOR_TRAP/osfmk" \
    "$OUT/dyld_syscall_gen/___kdebug_trace64.s" -o "$OUT/dispatch_dylib_extract/kdebug_trace64_trap.o"

# _kevent_qos: real syscall #374 (bsd/kern/syscalls.master), NOT
# NO_SYSCALL_STUB -- a plain Apple-generated trap stub
# ($OUT/pthread_syscall_gen/_kevent_qos.s, already produced as a byproduct
# of build_libpthread.sh's own create-syscalls.pl run against the same
# syscalls.master; regenerating here would just reproduce it byte-for-
# byte). Its kernel-side implementation (bsd/kern/kern_event.c) is real
# and complete, not an ENOTSUP stub -- confirmed by reading it, not
# assumed -- so this is a real, working syscall, not a stub-on-stub.
# Only real dependency is _cerror_nocancel (confirmed via nm before
# adding), already resolved via the libsystem_c.dylib sibling link below,
# same as geteuid.o above -- adds zero new gaps. Closes one entry off
# this dylib's own real remaining-gap list (the full current
# list and why it's still disabled used to be cited via the now-retired
# tools/init_binary/init.c's spawn_syslogd() comment, content not
# preserved elsewhere -- see docs/static-linking-cleanup-plan.md):
# kevent_qos was the FIRST symbol dyld's
# eager chained-fixups binding aborted on when syslogd was actually
# exec'd, found via a real QEMU boot test, not the only one left).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    -I "$XNU_FOR_TRAP/BUILD.hdrs/dst/usr/include" -I "$OUT/pthread_syscall_gen" \
    "$OUT/pthread_syscall_gen/_kevent_qos.s" -o "$OUT/dispatch_dylib_extract/kevent_qos_trap.o"

# ___gettimeofday_with_mach: needed here because dispatch_gap_closures4.o's
# real mach_get_times() calls it (see that file's own header comment).
# Compiled locally rather than resolved from libsystem_pthread.dylib --
# which DOES link the same generated stub -- because real Apple's own
# generator emits it `.private_extern` (confirmed via
# `nm -m`: "private external"), so it is deliberately image-local and can
# never be exported across a dylib boundary. On real Darwin every image
# that needs it has its own copy for exactly this reason.
#
# This is NOT the duplicate-symbol bug class the rest of this pass
# removes: the stub is a stateless raw syscall trap with no globals, and
# being private-extern it is invisible outside this image, so there is no
# symbol for another image to bind to the wrong copy of. Added 2026-08-29
# when dropping this dylib's static libpthread.a bundle (Phase 1a)
# removed the previous, accidental source of it.
# The generated stub defines BOTH ___gettimeofday (public, and a real
# export of libsystem_c.dylib already) and ___gettimeofday_with_mach.
# Only the second is wanted here, so the first is hidden with
# `ld -r -unexported_symbol` rather than shipping a second public
# definition -- `-exported_symbol` cannot be used for the inverse
# selection because the symbol being kept is private-extern to begin
# with and so is not exportable at all. `-keep_private_externs` is
# required alongside `-r`: without it ld resolves private externs down to
# plain local symbols, which makes ___gettimeofday_with_mach invisible to
# the final link (verified -- the link fails on exactly that symbol).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    -I "$XNU_FOR_TRAP/BUILD.hdrs/dst/usr/include" -I "$OUT/pthread_syscall_gen" \
    "$OUT/pthread_syscall_gen/___gettimeofday.s" -o "$OUT/dispatch_dylib_extract/gettimeofday_trap_raw.o"
"$LD" -r -arch arm64 -keep_private_externs -unexported_symbol ___gettimeofday \
    "$OUT/dispatch_dylib_extract/gettimeofday_trap_raw.o" \
    -o "$OUT/dispatch_dylib_extract/gettimeofday_trap.o"
rm -f "$OUT/dispatch_dylib_extract/gettimeofday_trap_raw.o"

# same real _chk/-undefined-dynamic_lookup quirk as kdebug_trace64 above
# -- real source already established by build_libxpc_syscalls2.sh
# (third_party/Libc/secure/*.c), just needs its own copy here since this
# script doesn't depend on that one having run.
#
# -D_FORTIFY_SOURCE=0 REQUIRED: see build_libxpc_syscalls2.sh's own
# CFLAGS_SECURE comment for the full real trail -- this SDK defaults
# _FORTIFY_SOURCE to 2 for this iOS target, so without this flag each of
# these _chk implementation files' own internal calls (e.g.
# vsnprintf_chk.c's `vsnprintf(...)`) get macro-rewritten right back into
# themselves, causing real unbounded self-recursion -> stack-guard-page
# SIGSEGV. Found 2026-08-27 the first time this dylib's real dependents
# (syslogd) actually exercised plain snprintf()/vsnprintf() at runtime.
LIBC="../../third_party/Libc"
CFLAGS_SECURE=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -D_FORTIFY_SOURCE=0 \
    -Wno-nullability-completeness -I "$LIBC/secure")
# The _chk tier is no longer compiled here (static-linking cleanup,
# Phase 1a, 2026-08-29) -- ___snprintf_chk/___vsnprintf_chk/
# ___strlcpy_chk/___snprintf_object_size_chk and the ___chk_fail_*
# handlers are now real exports of libsystem_c.dylib, a sibling on this
# link line, which is where real Apple puts them. The -D_FORTIFY_SOURCE=0
# requirement described above did not go away, it moved with them: see
# build_libsystem_c.sh, which compiles the same real sources with the
# same flag for the same reason (launchd bug #6).

# 2026-08-27 gap-closure pass: real remaining-gap symbols closed at once
# (the full real trail of what each needed and why used to be cited via
# the now-retired tools/init_binary/init.c's spawn_syslogd() comment,
# content not preserved elsewhere -- see
# docs/static-linking-cleanup-plan.md -- not re-derived here).
XNU_FOR_GAPS="../../third_party/xnu-7195"

# kpersona_get: real, unmodified libsyscall/wrappers/persona.c (all 9
# kpersona_* functions -- only kpersona_get is a real gap here, the rest
# are free real functionality from the same self-contained file, not
# extra risk) + its own real raw NO_SYSCALL_STUB trap (___persona.s,
# already generated by build_libpthread.sh's own create-syscalls.pl run
# against the same syscalls.master -- reused, not regenerated). Only
# real deps: ___persona (this trap) and ___strlcpy_chk (already compiled
# above in this exact script).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -DPRIVATE=1 \
    -I "$XNU_FOR_GAPS/libsyscall/wrappers/string" -I "$XNU_FOR_GAPS/bsd" -I "$XNU_FOR_GAPS/BUILD.hdrs/dst/usr/include" \
    "$XNU_FOR_GAPS/libsyscall/wrappers/persona.c" -o "$OUT/dispatch_dylib_extract/persona.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -I "$XNU_FOR_GAPS/BUILD.hdrs/dst/usr/include" -I "$OUT/pthread_syscall_gen" \
    "$OUT/pthread_syscall_gen/___persona.s" -o "$OUT/dispatch_dylib_extract/persona_trap.o"



# mach_port_construct: real, already-built CoreFoundation objects
# (cf_obj/cf_mach_port_syscalls.o + cf_obj/mach_port_traps.o, built by
# build_corefoundation.sh -- see that pair's own header comments for the
# full real extraction rationale) reused as-is, not recompiled. Its own
# real MIG fallback (_kernelrpc_mach_port_construct, on
# MACH_SEND_INVALID_DEST) is already a real, defined export of
# libc_build/libxpc_syscalls2.a's own mach_portUser.o -- confirmed via
# nm before reusing, not assumed. Only extern-declared here, not linked
# in directly (this dylib doesn't otherwise depend on libxpc_syscalls2.a;
# it resolves at the whole-process level once syslogd.macho, which does
# link that archive, loads this dylib -- same real flat-namespace
# mechanism this dylib's own -Wl,-U symbols already rely on).
# cf_mach_port_syscalls.o / mach_port_traps.o no longer copied here
# (static-linking cleanup, Phase 1a, 2026-08-29): the mach_port_allocate
# family and its raw traps are now real exports of libsystem_kernel.dylib.

# mach_port_extract_right/insert_right/unguard/request_notification/
# kernel_object + thread_destruct_special_reply_port: real trap-first
# (mach_port_gap_closures2.s, 3 more real kernelrpc traps not already
# covered by cf_obj/mach_port_traps.o) + real MIG fallback (same
# already-real libxpc_syscalls2.a mach_portUser.o as mach_port_construct
# above). See mach_port_gap_closures2.c's own header for the real bug
# this also fixes: build_libdispatch_dylib.sh's own dispatch_dylib_extract
# step (below) excludes libmach_ipc.a's mach_ipc_dead_stubs.o, whose 3
# narrowly-scoped KERN_FAILURE stand-ins for these exact symbol names
# would otherwise have been the ONLY definition available process-wide.
# mach_port_gap_closures2.o/.s no longer compiled here (static-linking
# cleanup, Phase 1a, 2026-08-29): _mach_port_extract_right/_insert_right/
# _unguard/_kernel_object and _thread_destruct_special_reply_port are now
# real exports of libsystem_kernel.dylib, which is where they belonged --
# libxpc.dylib was compiling its own second copy of this same file.

# mach_make_memory_entry_64: real, already-built MIG output
# (libc_build/dyld_core.a's own vm_mapUser.o, generated from real
# osfmk/mach/vm_map.defs) narrowed via `ld -r -exported_symbol` to just
# this one routine -- that .o also defines ~25 OTHER vm_*() names
# (vm_allocate/vm_deallocate/vm_map/etc) that real-conflict with
# libmalloc.a's own already-linked mach_vm.c definitions of the same
# names (confirmed via nm before doing this, not guessed; same
# "ar x, exclude/isolate the now-redundant member" precedent this
# script's own mach_ipc_dead_stubs.o exclusion already established, just
# via `ld -r -exported_symbol` instead of `ar x` + `rm` since this
# routine needs to come OUT of a multi-routine .o rather than being one
# whole member). Remaining real deps (_NDR_record, _mach_msg,
# _mach_msg_destroy, _memcpy, _mig_dealloc_reply_port/_get_reply_port/
# _put_reply_port, _voucher_mach_msg_set, ___stack_chk_guard) are all
# already real and linked elsewhere in this exact build -- confirmed via
# nm before adding, not assumed.
ar p "$OUT/dyld_core.a" vm_mapUser.o > "$OUT/dispatch_dylib_extract/vm_mapUser_raw.o"
"$LD" -r -arch arm64 -exported_symbol _mach_make_memory_entry_64 \
    "$OUT/dispatch_dylib_extract/vm_mapUser_raw.o" -o "$OUT/dispatch_dylib_extract/mach_make_memory_entry_64.o"
rm -f "$OUT/dispatch_dylib_extract/vm_mapUser_raw.o"

# sigsuspend, semaphore_timedwait(+trap), malloc_engaged_nano,
# malloc_memory_event_handler: see dispatch_gap_closures3.c/.s's own
# header comments for the full real rationale on each (raw NO_SYSCALL_STUB
# trap reusing this project's established carry-flag pattern; real Mach
# trap -38, same class as this project's own existing
# mach_semaphore_traps.s; and 2 narrow-but-real Apple-source-matching
# fallback bodies for functionality this project's curated libmalloc
# never built, not fabrications).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    dispatch_gap_closures3.c -o "$OUT/dispatch_dylib_extract/dispatch_gap_closures3.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    dispatch_gap_closures3.s -o "$OUT/dispatch_dylib_extract/dispatch_gap_closures3_trap.o"

# sleep, mach_continuous_time, mach_get_times: see dispatch_gap_closures4.c's
# own header comment for the full real rationale (real algorithm/bodies,
# narrowed against symbols that actually exist in this project rather
# than the exact real internal split Apple's own source assumes).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -DPRIVATE=1 -I "$XNU_FOR_GAPS/osfmk" \
    dispatch_gap_closures4.c -o "$OUT/dispatch_dylib_extract/dispatch_gap_closures4.o"

# mach_eventlink_* family (7 symbols), 2026-08-27: real, unmodified Apple
# source throughout, same "check real source first" priority as
# everything else in this pass.
#
#   - mach_eventlink_signal/wait_until/signal_wait_until: real, verbatim
#     libsyscall/mach/mach_eventlink.c (uint64_t-packed-return BSD
#     syscalls, per that file's own header comment on why these 3 are
#     syscalls rather than Mach traps -- a trap can't return a full 64-bit
#     value). Each is backed by its own real NO_SYSCALL_STUB raw-syscall
#     stub (#496/#497/#498, already generated by build_libpthread.sh's
#     own create-syscalls.pl run against the same syscalls.master --
#     reused verbatim, not regenerated, same as _kevent_qos/pread/pwrite
#     above). decode_eventlink_error_from_retval/
#     decode_eventlink_count_from_retval (used by mach_eventlink.c itself)
#     are real, self-contained macros in mach_eventlink_types.h, no
#     extra dependency.
#   - mach_eventlink_create/destroy/associate/disassociate: real MIG
#     routines, no trap variant -- fresh `mig` regen against
#     osfmk/mach/mach_eventlink.defs (client side only, same
#     -server /dev/null -sheader /dev/null precedent build_libxpc_syscalls2.sh's
#     own mach_port.defs regen already established), NOT hand-written.
#     Needs mach_eventlink_types.h force-included (`-include`) for the
#     real mach_eventlink_*_option_t types the mig-generated header
#     itself references but doesn't include -- confirmed by trying
#     without it first and reading the real compile error, not guessed.
#     Real remaining deps (_NDR_record, _mach_msg, _mach_msg_destroy,
#     _mig_dealloc_reply_port/_get_reply_port/_put_reply_port,
#     _voucher_mach_msg_set) are all already real and linked elsewhere in
#     this exact build -- confirmed via nm before adding, not assumed
#     (same set mach_make_memory_entry_64's own isolated extraction above
#     already needed).
MIG="$(which mig)"
if [ -z "$MIG" ] || [ ! -x "$MIG" ]; then
    echo "error: mig tool not found" >&2
    exit 1
fi
mkdir -p "$OUT/mach_eventlink_mig"
"$MIG" -arch arm64 -I"$XNU_FOR_GAPS/osfmk" \
    -header "$OUT/mach_eventlink_mig/mach_eventlink.h" -user "$OUT/mach_eventlink_mig/mach_eventlinkUser.c" \
    -server /dev/null -sheader /dev/null \
    "$XNU_FOR_GAPS/osfmk/mach/mach_eventlink.defs"
echo "mig: mach_eventlink.defs regenerated cleanly (client side)"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I "$XNU_FOR_GAPS/libsyscall/mach" -idirafter "$XNU_FOR_GAPS/osfmk" -I "$OUT/mach_eventlink_mig" \
    -include "$XNU_FOR_GAPS/osfmk/mach/mach_eventlink_types.h" \
    "$OUT/mach_eventlink_mig/mach_eventlinkUser.c" -o "$OUT/dispatch_dylib_extract/mach_eventlinkUser.o"

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -DPRIVATE=1 -I "$XNU_FOR_GAPS/osfmk" -I "$XNU_FOR_GAPS/bsd" \
    "$XNU_FOR_GAPS/libsyscall/mach/mach_eventlink.c" -o "$OUT/dispatch_dylib_extract/mach_eventlink.o"

for f in ___mach_eventlink_signal ___mach_eventlink_wait_until ___mach_eventlink_signal_wait_until; do
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -I "$XNU_FOR_GAPS/BUILD.hdrs/dst/usr/include" -I "$OUT/pthread_syscall_gen" \
        "$OUT/pthread_syscall_gen/$f.s" -o "$OUT/dispatch_dylib_extract/${f}_trap.o"
done

# pthread_get_stackaddr_np used to be compiled into this dylib here
# (pthread_get_stackaddr_np_compat.c). DAR-110 moved it to its real home,
# libsystem_pthread.dylib, which this dylib links; see
# build_libsystem_pthread.sh.

# Real gap found+fixed 2026-08-28 booting launchd as PID 1 with
# libdispatch.dylib loaded alongside it: libmalloc.a bundles its own
# private, pre-dylib-era copy of real Apple mach_init.c
# (magazine_malloc.o/vm.o/mach_vm.o reference _mach_task_self_ as
# undefined, so the linker pulls the whole member in to satisfy it) --
# this is exactly the real "two private copies of one process-global"
# risk this file's own HONEST SCOPE STATEMENT above already flagged and
# deferred, now confirmed live: launchd.macho's own two-level-namespace
# binding for _mach_task_self_ picked THIS dylib's local copy (first
# dylib on launchd's own link line to export a real definition) over the
# real, correctly-populated one in libsystem_kernel.dylib -- so anything
# reading mach_task_self() anywhere in the process got a permanently-0,
# never-initialized task port, causing a real mach_port_allocate()
# failure inside launchd_runtime_init() (SIGABRT via os_assert_zero()).
# Same established `ar d`-on-a-local-copy fix build_launchd.sh's own
# header comment already applied for this exact same reason -- strips
# mach_init.o (confirmed via nm: every symbol it defines --
# _mach_task_self_, _mach_init, _bootstrap_port, _host_page_size,
# _NDR_record, __mach_fork_child, _vm_kernel_page_*/_vm_page_* -- is
# also a real export of libsystem_kernel.dylib, already a real
# transitive dependency via libsystem_c.dylib/libsystem_malloc.dylib/
# libsystem_pthread.dylib below) from a LOCAL copy only, never the
# shared committed libmalloc.a (other static-only consumers still need
# their own private copy). Does NOT fully close this file's own broader
# HONEST SCOPE STATEMENT (g_zone/errno etc are real, separate, still-open
# instances of the same class) -- narrowly fixes the one instance that
# actually crashed launchd, not a claim the whole debt item is resolved.
#
# A SECOND real instance found+fixed 2026-08-29 resuming bug #6: this
# same local libmalloc_no_mach_init.a copy also bundles its own private
# mig_reply_port.o (only mach_init.o was ever stripped from it) -- and
# because libdispatch.dylib appears BEFORE libSystem.B.dylib on
# build_launchd.sh's own link line, launchd.macho's two-level-namespace
# binding for _mig_get_reply_port/_mig_dealloc_reply_port/
# _mig_put_reply_port picked THIS dylib's copy over libsystem_kernel.dylib's
# real, canonical one (confirmed: after fixing build_launchd.sh's OWN
# separate mig_reply_port.o duplicate inside launchd.macho's local static
# link, diagnostic prints added to libsystem_kernel.dylib's copy still
# never fired -- only went silent once this dylib's copy was also
# checked and found to still carry a plain, uninstrumented one). Same
# established fix, same verification (every symbol mig_reply_port.o
# defines is already a real export of libsystem_kernel.dylib, already a
# real transitive dependency here).
rm -f "$OUT/libmalloc_no_mach_init.a"
cp "$OUT/libmalloc.a" "$OUT/libmalloc_no_mach_init.a"
ar d "$OUT/libmalloc_no_mach_init.a" mach_init.o
ar d "$OUT/libmalloc_no_mach_init.a" mig_reply_port.o

mkdir -p "$OUT/system"
rm -f "$OUT/system/libdispatch.dylib"
    # -not_for_dyld_shared_cache: real, newly-surfaced requirement of
    # the daily-driver Xcode's newer ld (NEWLD_BINDIR above) -- it
    # refuses "-undefined dynamic_lookup"/-U on a dylib it classifies
    # as shared-cache eligible (real /usr/lib install path) unless this
    # is set, a real check Xcode 12's ld64-609 never enforced. This
    # project builds no real dyld shared cache at all, so opting every
    # real dylib out is correct, not a workaround (docs/backlog.md item
    # d9's NEWLD_BINDIR sweep, 2026-08-28).
    #
    # __dispatch_queue_attr_concurrent alias: real init.c only compiles a
    # distinct definition of this global under `#if DISPATCH_VARIANT_STATIC`
    # -- the real dylib build instead aliases it to __dispatch_queue_attrs[0]
    # via the real, pinned xcodeconfig/libdispatch.aliases + -alias_list on
    # Darwin (init.c's own comment says so explicitly). Same real pairing
    # as that file's one non-UNIX2003 entry, just spelled as a single
    # `-alias` pair instead of wiring up `-alias_list` for one line.
    #
    # libsystem_blocks.dylib reexport (not a plain load) added 2026-08-29
    # (docs/daemon-roadmap.md Phase 6.8 continued, item 1 real
    # root-cause): a real launchd-as-PID-1 boot panicked with dyld's own
    # real exit reason "Symbol not found: __Block_copy, Referenced from:
    # /usr/lib/system/libsystem_notify.dylib, Expected in:
    # /usr/lib/system/libdispatch.dylib" -- real Apple's libdispatch.dylib
    # has historically absorbed the Blocks runtime's public identity (it
    # reexports it), so any consumer written against real Apple headers
    # expects Block_copy/Block_release to resolve through libdispatch, not
    # just be usable *by* libdispatch internally. A plain load only
    # satisfies the latter.
    #
    # libsystem_c.dylib reexport added the same pass: after the above fix,
    # the SAME boot hit an identical-shaped abort for ___snprintf_chk,
    # also "Expected in: libdispatch.dylib" -- despite libsystem_c.dylib
    # (which really defines it, confirmed via `nm -m`/`dyld_info -exports`)
    # being linked earlier on libsystem_notify.dylib's own link line. A
    # real `dyld_info -fixups` dump of libsystem_notify.dylib showed dozens
    # of genuinely libdispatch-owned symbols (real dispatch_* API) grouped
    # in the same ordinal as these two non-dispatch symbols -- this
    # project's pinned Xcode-12-era ld64 (NEWLD_BINDIR override, see
    # docs/backlog.md item d9) appears to fold a plain (non-reexport) load
    # dependency into the SAME two-level ordinal as its own real exports
    # under `-Wl,-fixup_chains`, rather than keeping them distinct the way
    # real Apple's newer toolchain does -- a real, narrow toolchain quirk,
    # not a duplicate-symbol or architecture problem (confirmed:
    # tools/userland_staging/dup_symbol_audit.sh does not flag either
    # symbol). Reexporting libsystem_c.dylib here closes the whole class
    # at once instead of chasing it symbol-by-symbol.
# libsystem_kernel.dylib reexport added 2026-09-01 (DAR-164, real gap
# found root-causing syslogd's post-exec abort): same exact bug shape as
# the libsystem_c.dylib reexport documented just above, same real
# Xcode-12-era ld64 ordinal-folding quirk (a plain load dependency's
# exports get folded into the SAME two-level ordinal as this dylib's own
# real exports under `-Wl,-fixup_chains`) -- this time for `_nanosleep`
# (real, confirmed via `nm -m`/`dyld_info -exports` to be genuinely
# defined in libsystem_kernel.dylib, build_libsystem_kernel.sh's own
# nanosleep_compat.o, not libdispatch's own code), found via a real
# kernel-side abort_with_payload() probe on a real QEMU boot:
# "Symbol not found: _nanosleep / Referenced from: /usr/sbin/syslogd /
# Expected in: /usr/lib/system/libdispatch.dylib". No new eager-binding
# risk (unlike the earlier, reverted libsystem_m.dylib reexport attempt
# documented in build_libsystem_c.sh): libsystem_kernel.dylib was already
# a plain load dependency right here, already unconditionally present in
# every process via libSystem.B.dylib's own reexport chain since before
# this fix -- reexporting it here doesn't make anything newly mandatory,
# it only republishes an already-always-loaded, already-vetted sibling's
# real exports under this dylib's own two-level namespace ordinal too.
# $OUT/simple_obj/sys__kqueue.o REMOVED from this link 2026-08-31
# (DAR-124 sweep): it was a bare, undocumented cross-script dependency
# on build_simple_cmds.sh already having run (that script generates this
# .o as part of its own SYS_STUBS_NEEDED batch) AND a real, confirmed
# dylib-vs-dylib duplicate with libsystem_kernel.dylib's own new,
# canonical _kqueue export (see that script's own "needs a new home"
# comment) -- libsystem_kernel.dylib is already a linked sibling just
# below, so this dylib's own real reference to kqueue() now resolves
# there instead of carrying a second, independently-compiled copy.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libdispatch.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    "${UNDEF_FLAGS[@]}" \
    -Wl,-alias,__dispatch_queue_attrs,__dispatch_queue_attr_concurrent \
    -o "$OUT/system/libdispatch.dylib" \
    "$OUT"/dispatch_obj/*.o "$OUT"/dispatch_obj/event/*.o \
    "$OUT"/dispatch_dylib_extract/*.o \
    "$OUT/system/libsystem_malloc.dylib" "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_platform.dylib" \
    -Wl,-reexport_library,"$OUT/system/libsystem_blocks.dylib" \
    -Wl,-reexport_library,"$OUT/system/libsystem_c.dylib" \
    -Wl,-reexport_library,"$OUT/system/libsystem_kernel.dylib" \
    2>"$OUT/system/libdispatch_dylib_link.err" \
    || { echo "note: dylib link reported diagnostics (expected -- undefined symbols are normal in a dylib), see $OUT/system/libdispatch_dylib_link.err" >&2; }

if [ ! -f "$OUT/system/libdispatch.dylib" ]; then
    echo "error: libdispatch.dylib was not produced -- check $OUT/system/libdispatch_dylib_link.err for a real link error (not just undefined-symbol warnings)" >&2
    tail -40 "$OUT/system/libdispatch_dylib_link.err" >&2
    exit 1
fi

echo "wrote $OUT/system/libdispatch.dylib"
otool -hv "$OUT/system/libdispatch.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
otool -l "$OUT/system/libdispatch.dylib" | grep -q LC_ID_DYLIB || {
    echo "error: no LC_ID_DYLIB -- not a real dylib" >&2
    exit 1
}
echo "confirmed: $OUT/system/libdispatch.dylib is a real MH_DYLIB with LC_ID_DYLIB"

undef_count=$(nm -u "$OUT/system/libdispatch.dylib" 2>/dev/null | wc -l | tr -d ' ')
# plain nm -u conflates two different things: symbols already bound to a
# sibling dylib on this link line (nm -m: "(from libsystem_X)", safe --
# resolves exactly like any other real Apple dylib's cross-image call)
# vs symbols with no resolving dylib at all (nm -m: "(dynamically looked
# up)", the real UNDEF_SYMS/-Wl,-U gap list this script documents above).
# Only the second group is a real gap; report both so the raw undef_count
# doesn't overstate it (see build_libsystem_pthread.sh's own nm -m fix,
# same distinction).
real_gap_count=$(nm -m "$OUT/system/libdispatch.dylib" 2>/dev/null | grep undefined | grep -vc "(from " | tr -d ' ')
sibling_bound_count=$((undef_count - real_gap_count))
echo "undefined-symbol table count in the dylib: $undef_count total -- $sibling_bound_count resolved via sibling dylib (real, safe), $real_gap_count real gap requiring flat-namespace runtime lookup (expected -- see this script's own HONEST SCOPE STATEMENT)"
