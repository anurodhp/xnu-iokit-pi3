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
# 7 mach_port_* + thread_destruct_special_reply_port real gaps -- see
# tools/init_binary/init.c's spawn_syslogd() comment for the full trail
# and gap_audit.sh's own output for how each was found/verified. Real,
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
UNDEF_SYMS=(
    _pthread_setschedparam
    __mach_errors _environ
    _mach_port_get_context _mach_port_set_context
    _semaphore_create _semaphore_destroy
    _thread_get_special_reply_port
)
# Real, defined-elsewhere-in-the-process symbols this dylib's own
# gap-closure .o's (mach_port_gap_closures2.o/cf_mach_port_syscalls.o/
# dispatch_gap_closures4.o) reference but can't resolve at THIS dylib's
# own standalone `-nostdlib` link time, because the archives that really
# define them (libxpc_syscalls2.a's mach_portUser.o for the 11
# _kernelrpc_mach_port_* MIG fallbacks; syslogd_libc_extras.a for
# _nanosleep) are deliberately NOT linked into this narrow dylib -- same
# real flat-namespace mechanism as every other -U symbol here: they
# resolve for real once syslogd.macho (which DOES link both archives)
# loads this dylib, confirmed via nm -g syslogd.macho showing the MIG
# fallbacks and nanosleep already real+defined there. NOT the same class
# as this dylib's own honestly-still-open gaps above -- kept in a
# separate array to avoid muddying that list's meaning.
UNDEF_SYMS_ELSEWHERE_IN_PROCESS=(
    __kernelrpc_mach_port_allocate __kernelrpc_mach_port_construct
    __kernelrpc_mach_port_destruct __kernelrpc_mach_port_extract_member
    __kernelrpc_mach_port_extract_right __kernelrpc_mach_port_get_attributes
    __kernelrpc_mach_port_insert_member __kernelrpc_mach_port_insert_right
    __kernelrpc_mach_port_kernel_object __kernelrpc_mach_port_request_notification
    __kernelrpc_mach_port_type __kernelrpc_mach_port_unguard
    _nanosleep
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
if [ ! -f "$OUT/system/libsystem_c.dylib" ] || [ ! -f "$OUT/system/libsystem_malloc.dylib" ] || \
   [ ! -f "$OUT/system/libsystem_pthread.dylib" ]; then
    echo "error: libsystem_c/malloc/pthread.dylib not found -- run those build scripts first" >&2
    exit 1
fi
if [ ! -f "$OUT/syslogd_libc_extras.a" ]; then
    echo "error: $OUT/syslogd_libc_extras.a not found -- run build_syslogd.sh first" >&2
    exit 1
fi

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
LD="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
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
(cd "$OUT/dispatch_dylib_extract" && ar x ../libmach_ipc.a && rm -f mach_ipc_dead_stubs.o)

# real, narrow single-object extraction: geteuid.o's only dependency
# (_cerror_nocancel) is already resolved for real via the
# libsystem_c.dylib sibling link below (confirmed via nm before
# extracting).
(cd "$OUT/dispatch_dylib_extract" && ar x ../syslogd_libc_extras.a geteuid.o)

# atoi: real body is atoi.o, same archive as geteuid.o above. Its only
# real deps (___global_locale/___locale_key) are NOT provided by pulling
# in the whole xlocale.o (real-cascades into
# __collate_load_tables/__monetary_load_locale/etc, same bloat class
# already rejected for strcasecmp/getprogname elsewhere in this file) --
# instead: a real, narrow libc_locale_globals_compat.c (this script's own
# sibling file, see its header for the full trace) defines just those
# two real globals, verbatim from xlocale.c, plus real table.o/none.o
# (locale/FreeBSD/table.c's own _DefaultRuneXLocale + its own
# self-contained __none_* charset-conversion dependency) extracted from
# libc_build/libc_locale.a -- confirmed via nm that table.o/none.o's only
# real deps (___error/_memchr) are already resolved elsewhere in this
# exact link, so this whole chain adds zero new gaps.
if [ ! -f "$OUT/libc_locale.a" ]; then
    echo "error: $OUT/libc_locale.a not found -- run build_libc_locale.sh first" >&2
    exit 1
fi
(cd "$OUT/dispatch_dylib_extract" && ar x ../syslogd_libc_extras.a atoi.o && \
 ar x ../libc_locale.a table.o none.o)
LIBC="../../third_party/Libc"
PTHREAD_FOR_LOCALE="../../third_party/libpthread"
XNU_FOR_LOCALE="../../third_party/xnu-7195"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I "$OUT/compat_include" \
    -I "$LIBC/include" -I "$LIBC/darwin" -I "$LIBC/fbsdcompat" -I "$LIBC/locale" -I "$LIBC/locale/FreeBSD" \
    -I "$LIBC/stdtime/FreeBSD" \
    -I "$PTHREAD_FOR_LOCALE/include" -I "$PTHREAD_FOR_LOCALE/private" \
    -I "$XNU_FOR_LOCALE/osfmk" -I "$XNU_FOR_LOCALE/libsyscall" \
    -I ../../generated \
    libc_locale_globals_compat.c -o "$OUT/dispatch_dylib_extract/libc_locale_globals_compat.o"

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
cp "$OUT/xpc_syscall_obj/semaphore_wrap.o" "$OUT/dispatch_dylib_extract/semaphore_wrap.o"

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
cp "$OUT/xpc_syscall_obj/mach_error_string.o" "$OUT/dispatch_dylib_extract/mach_error_string.o"
cp "$OUT/xpc_syscall_obj/mach_semaphore_traps.o" "$OUT/dispatch_dylib_extract/mach_semaphore_traps.o"
cp "$OUT/xpc_obj/os_assumes_compat.o" "$OUT/dispatch_dylib_extract/os_assumes_compat.o"
cp "$OUT/xpc_syscall_obj/getenv_compat.o" "$OUT/dispatch_dylib_extract/getenv_compat.o"

# real, minimal stack-protector runtime support -- needed only because
# dyld_mach_portUser.o/dyld_vm_mapUser.o (below, reused from the dyld
# port) were originally compiled WITH stack-protector enabled; this
# dylib's own dispatch_obj/*.o are all `-fno-stack-protector` (matching
# every other tier in this project) and never call these. Real,
# standard canary-check runtime (matches every libc's actual
# __stack_chk_fail semantics: abort on mismatch), not a functional
# stand-in.
cat > "$OUT/dispatch_dylib_extract/stack_chk_compat.c" <<'EOF'
#include <stdint.h>
#include <stdlib.h>
long __stack_chk_guard = 0x0BADC0DEDEADBEEFL;
void __stack_chk_fail(void) { abort(); }
/* real arm64 stack-probe helper (touches each page of a large stack
 * allocation so the kernel's guard page reliably faults on overflow
 * instead of possibly skipping past it) -- reused objects here only
 * make small, fixed-size MIG reply-buffer allocations, so a no-op body
 * is safe for this target's real call pattern, not a functional gap. */
void __chkstk_darwin(void) {}
EOF
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    "$OUT/dispatch_dylib_extract/stack_chk_compat.c" -o "$OUT/dispatch_dylib_extract/stack_chk_compat.o"

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
# this dylib's own real remaining-gap list (see
# tools/init_binary/init.c's spawn_syslogd() comment for the full current
# list and why it's still disabled: kevent_qos was the FIRST symbol dyld's
# eager chained-fixups binding aborted on when syslogd was actually
# exec'd, found via a real QEMU boot test, not the only one left).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    -I "$XNU_FOR_TRAP/BUILD.hdrs/dst/usr/include" -I "$OUT/pthread_syscall_gen" \
    "$OUT/pthread_syscall_gen/_kevent_qos.s" -o "$OUT/dispatch_dylib_extract/kevent_qos_trap.o"

# same real _chk/-undefined-dynamic_lookup quirk as kdebug_trace64 above
# -- real source already established by build_libxpc_syscalls2.sh
# (third_party/Libc/secure/*.c), just needs its own copy here since this
# script doesn't depend on that one having run.
LIBC="../../third_party/Libc"
CFLAGS_SECURE=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -I "$LIBC/secure")
for f in snprintf_chk strlcpy_chk vsnprintf_chk; do
    "$CLANG" "${CFLAGS_SECURE[@]}" "$LIBC/secure/$f.c" -o "$OUT/dispatch_dylib_extract/$f.o"
done
"$CLANG" "${CFLAGS_SECURE[@]}" chk_fail_compat.c -o "$OUT/dispatch_dylib_extract/chk_fail_compat.o"

# 2026-08-27 gap-closure pass: real remaining-gap symbols closed at once
# (see tools/init_binary/init.c's spawn_syslogd() comment for the full
# real trail of what each needed and why -- not re-derived here).
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

# pread/pwrite: plain Apple-generated syscall stubs (#153/#154, NOT
# NO_SYSCALL_STUB), already produced by build_libpthread.sh's own
# create-syscalls.pl run -- reused verbatim, not regenerated. Only real
# dep is _cerror (already linked into this dylib below via cerror_stub.o).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -I "$XNU_FOR_GAPS/BUILD.hdrs/dst/usr/include" -I "$OUT/pthread_syscall_gen" \
    "$OUT/pthread_syscall_gen/_pread.s" -o "$OUT/dispatch_dylib_extract/pread_trap.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -I "$XNU_FOR_GAPS/BUILD.hdrs/dst/usr/include" -I "$OUT/pthread_syscall_gen" \
    "$OUT/pthread_syscall_gen/_pwrite.s" -o "$OUT/dispatch_dylib_extract/pwrite_trap.o"

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
cp "$OUT/cf_obj/cf_mach_port_syscalls.o" "$OUT/dispatch_dylib_extract/cf_mach_port_syscalls.o"
cp "$OUT/cf_obj/mach_port_traps.o" "$OUT/dispatch_dylib_extract/mach_port_traps.o"

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
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I "$XNU_FOR_GAPS/libsyscall/mach" -I "$XNU_FOR_GAPS/libsyscall/os" -idirafter "$XNU_FOR_GAPS/osfmk" \
    -I "$LIBC/include" -I "$LIBC/darwin" \
    mach_port_gap_closures2.c -o "$OUT/dispatch_dylib_extract/mach_port_gap_closures2.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    mach_port_gap_closures2.s -o "$OUT/dispatch_dylib_extract/mach_port_gap_closures2_traps.o"

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

# pthread_get_stackaddr_np: real, narrow extraction of pthread.c's own
# function (see pthread_get_stackaddr_np_compat.c's own header for the
# full real rationale on why the cross-thread liveness check is narrowed
# away safely, not skipped carelessly). Self-contained -- zero new deps.
PTHREAD_FOR_GAPS="../../third_party/libpthread"
PLATFORM_FOR_GAPS="../../third_party/libplatform"
MALLOC_FOR_GAPS="../../third_party/libmalloc"
CFLAGS_PTHREAD_GAP=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -Wno-availability -Wno-deprecated-declarations \
    -D__LIBC__ -D__POSIX_LIB__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_NON_CANCELABLE=1 \
    -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 -D__PTHREAD_BUILDING_PTHREAD__=1 -D__PTHREAD_EXPOSE_INTERNALS__ \
    -DOS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY=1 -DPRIVATE=1 -DVARIANT_STATIC=1 \
    -D_os_xbs_chrooted=0 \
    -D__API_UNAVAILABLE_PLATFORM_bridgeos=ios,unavailable \
    -D__API_AVAILABLE_PLATFORM_bridgeos\(x\)=ios,introduced=x \
    -DSPI_AVAILABLE\(...\)= -D__SPI_AVAILABLE\(...\)= \
    -I . \
    -I "$PTHREAD_FOR_GAPS" -I "$PTHREAD_FOR_GAPS/src" -I "$PTHREAD_FOR_GAPS/src/resolver" -I "$PTHREAD_FOR_GAPS/private" -I "$PTHREAD_FOR_GAPS/include" -I "$PTHREAD_FOR_GAPS/kern" \
    -I "$PLATFORM_FOR_GAPS/internal" -I "$PLATFORM_FOR_GAPS/private" -I "$PLATFORM_FOR_GAPS/include" \
    -I "$OUT/compat_include" \
    -I "$MALLOC_FOR_GAPS/private" \
    -I "$XNU_FOR_GAPS/libsyscall" -I "$XNU_FOR_GAPS/libsyscall/mach" -I "$XNU_FOR_GAPS/bsd" -I "$XNU_FOR_GAPS/libsyscall/wrappers/spawn" \
    -idirafter "$XNU_FOR_GAPS/osfmk" -idirafter "$XNU_FOR_GAPS/libkern")
"$CLANG" "${CFLAGS_PTHREAD_GAP[@]}" pthread_get_stackaddr_np_compat.c -o "$OUT/dispatch_dylib_extract/pthread_get_stackaddr_np_compat.o"

mkdir -p "$OUT/system"
rm -f "$OUT/system/libdispatch.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libdispatch.dylib \
    -Wl,-fixup_chains \
    "${UNDEF_FLAGS[@]}" \
    -Wl,-alias,___kill,'_kill$UNIX2003' \
    -Wl,-alias,_mprotect,'_mprotect$UNIX2003' \
    -Wl,-alias,__mal_semwait_sleep,'_sleep$UNIX2003' \
    -Wl,-alias,_write,'_write$UNIX2003' \
    -o "$OUT/system/libdispatch.dylib" \
    "$OUT"/dispatch_obj/*.o "$OUT"/dispatch_obj/event/*.o \
    "$OUT"/dispatch_dylib_extract/*.o \
    -Wl,-force_load,"$OUT/libpthread_qos.a" \
    "$OUT/libclosure.a" "$OUT/libmalloc.a" "$OUT/cerror_stub.o" "$OUT/libplatform.a" \
    "$OUT/libpthread.a" "$OUT/libc_gen.a" "$OUT/libc_string.a" "$OUT/libc_stdio.a" \
    "$OUT/simple_obj/sys__kqueue.o" \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_malloc.dylib" "$OUT/system/libsystem_pthread.dylib" \
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
