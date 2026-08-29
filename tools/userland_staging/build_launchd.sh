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
if [ ! -f "$OUT/cerror_stub.o" ]; then
    echo "error: $OUT/cerror_stub.o not found -- run build_launchd_core.sh first" >&2
    exit 1
fi
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

# Real gap found+fixed 2026-08-28 booting launchd as PID 1: libmalloc.a
# bundles its own private, pre-dylib-era copy of real Apple mach_init.c
# (magazine_malloc.o/vm.o/mach_vm.o reference _mach_task_self_ as
# undefined, so the linker pulls the whole member in to satisfy it) --
# same real "two private copies of one process-global" bug class already
# documented in build_libsystem_kernel.sh's own header (mach_task_self_'s
# Phase D bugs), a THIRD occurrence of it: this one splits launchd's own
# statically-linked code (liblaunchd_core.a's runtime.c calls
# mach_task_self() and gets libmalloc.a's copy, permanently 0/never
# initialized) from the real, correctly-populated copy in
# libsystem_kernel.dylib (which this project's own real mach_init(),
# called once via libsystem_dyld_initializer_compat.c's constructor,
# actually writes to). Confirmed via a real multi-layer instrumentation
# pass: mach_task_self()==0x0 read back inside launchd_runtime_init(),
# causing a real mach_port_allocate() failure -> os_assert_zero() ->
# real SIGABRT (namespace 2/OS_REASON_SIGNAL subcode 6). Same established
# `ar d`-on-a-local-copy fix already used once before for libmalloc.a
# (see libcxx_tsd_shim_bootstrap_compat.c's own header) -- strips
# mach_init.o (confirmed via nm: every symbol it defines --
# _mach_task_self_, _mach_init, _bootstrap_port, _host_page_size,
# _NDR_record, __mach_fork_child, _vm_kernel_page_*/_vm_page_* -- is also
# a real export of libsystem_kernel.dylib, already a real input to this
# link) from a LOCAL copy only, never the shared committed libmalloc.a
# (other static-only consumers still need their own private copy).
#
# A FOURTH occurrence of the exact same bug class, found+fixed 2026-08-29
# resuming bug #6: libmalloc.a (via build_libmalloc.sh, which compiles
# mach_vm.c/mach_init.c/mig_reply_port.c/init_cpu_capabilities.c as one
# real-Apple-source group) ALSO bundles its own private copy of
# mig_reply_port.o -- confirmed via `nm launchd.macho` showing
# `T _mig_get_reply_port` as a LOCAL strong definition, not `U`. Because
# ld64 resolves an executable's own local strong definitions before ever
# consulting a linked dylib's export (even when the dylib, here
# libsystem_kernel.dylib, exports the exact same symbol), every call to
# mig_get_reply_port()/mig_dealloc_reply_port()/mig_put_reply_port() from
# ANYWHERE in launchd.macho's own statically-linked code -- including
# generated MIG client stubs -- silently resolved to this stale local
# copy instead of libsystem_kernel.dylib's real, shared, canonical one.
# Found while adding finer-grained diagnostics inside mig_reply_port.c
# for bug #6 (SIGSEGV in the kqueue_demand_loop second thread) and
# discovering none of the new prints ever fired -- the code being
# instrumented in libsystem_kernel.dylib was never the code actually
# executing. Confirmed via nm that every symbol mig_reply_port.o defines
# (_mig_get_reply_port/_mig_dealloc_reply_port/_mig_put_reply_port) is
# already a real export of libsystem_kernel.dylib, same as mach_init.o
# above -- safe to strip the local copy the same way.
rm -f "$OUT/libmalloc_no_mach_init.a"
cp "$OUT/libmalloc.a" "$OUT/libmalloc_no_mach_init.a"
ar d "$OUT/libmalloc_no_mach_init.a" mach_init.o
ar d "$OUT/libmalloc_no_mach_init.a" mig_reply_port.o

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

rm -f "$OUT/launchd.macho"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
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
    "$OUT/libCoreFoundation.a" "$OUT/cf_libc_extras.a" "$OUT/launchd_libc_extras.a" \
    "$OUT/liblaunchd_mig.a" "$OUT/liblaunchd_syscalls.a" \
    "$OUT/libmach_ipc_no_dead_stubs.a" "$OUT/mach_port_gap_closures2.o" "$OUT/mach_port_gap_closures2_traps.o" \
    "$OUT/mach_ipc_dead_stubs_voucher_only.o" \
    "$OUT/libclosure.a" "$OUT/libmalloc_no_mach_init.a" "$OUT/cerror_stub.o" "$OUT/libplatform.a" \
    "$OUT/libpthread_no_dup_pthread_create.a" "$OUT/libc_gen.a" "$OUT/libc_string.a" "$OUT/libc_stdio.a" "$OUT/libc_printf.a" \
    "$OUT/libc_locale_for_stdio.a" \
    "$OUT/system/libxpc.dylib" "$OUT/system/libbsm.dylib" \
    "$OUT/system/libdispatch.dylib" "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib" \
    "$OUT/system/libsystem_pthread.dylib" \
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
