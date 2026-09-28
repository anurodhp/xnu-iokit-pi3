#!/bin/bash
# Builds libc_build/libdispatch_real.a: the REAL Apple libdispatch
# (third_party/libdispatch, tag libdispatch-1271.40.12, ~28K real LOC),
# replacing this project's earlier dispatch_min.a (a 393-line
# hand-written single-thread shim, see build_libdispatch_min.sh) --
# real-source-available shims are architecture debt in this project, not
# a design goal (see docs/daemon-roadmap.md's "libdispatch real port"
# section). Compiles the 14 real, confirmed-clean core dispatch files
# (see below) to real .o files, archives them, and does a real trial
# link against this project's existing libc/libpthread/libplatform/
# libmach_ipc stack with a real, functionally-exercising main() (see
# "NOT DONE YET" below for what's still open after that link).
#
# ============================================================
# HONEST SCOPE STATEMENT -- read this first
# ============================================================
# CLEAN (0 real compile errors, confirmed via real `clang -fsyntax-only`,
# not assumed): once.c, semaphore.c, apply.c, time.c, allocator.c,
# init.c, queue.c, object.c, source.c, voucher.c, io.c, mach.c,
# eventlink.c, transform.c, data.c, event/event.c, event/event_kevent.c,
# event/workqueue.c -- 18 real dispatch source files. data.c (the real
# dispatch_data_t engine) and the real event/ kqueue backend were
# initially left out of the first pass (data.c was thought incompatible
# with dispatch_min.a's own different dispatch_data_t representation --
# no longer true now that the REAL object model from object.c/queue.c is
# what's actually being built) -- both compile clean against the real
# object model with zero extra fixes beyond what's already below.
#
# DELIBERATELY EXCLUDED: workgroup.c (real "QoS workgroup" feature --
# confirmed via grep zero real os_workgroup_* call sites anywhere in
# third_party/PureDarwin's actual launchd/libxpc source, same finding
# that justified os/workgroup.h's own empty stub earlier in Phase 6.8);
# block.cpp (Objective-C++ Blocks-runtime integration, no ObjC here);
# the entire src/firehose/ and src/swift/ directories (real os_log
# tracing engine and Swift overlay, neither applicable -- no logd, no
# Swift runtime on this target).
#
# Real, non-obvious fixes this pass needed (see each compat file's own
# header comment for full detail):
# - config/config_ac.h (new, dispatch_compat_headers/): a real,
#   hand-authored replacement for internal.h's own stale, autotools-era
#   third_party/libdispatch/config/config.h (version "1.3", assumes
#   HAVE_OBJC=1/HAVE_PTHREAD_WORKQUEUES=1, neither true here). Key real
#   finding: DISPATCH_USE_THREAD_LOCAL_STORAGE must be left completely
#   UNDEFINED, not defined-as-0 -- shims/tsd.h's real TSD accessors are
#   gated `#if DISPATCH_USE_THREAD_LOCAL_STORAGE` vs `#ifndef
#   DISPATCH_USE_THREAD_LOCAL_STORAGE` (definedness, not truthiness),
#   so `=0` satisfies neither and silently undeclares
#   _dispatch_thread_port/_dispatch_thread_getspecific/etc; forcing
#   HAVE_PTHREAD_WORKQUEUES=0 (this project's curated libpthread has zero
#   real kernel-workqueue symbols, confirmed via `nm -g`) selects
#   DISPATCH_USE_INTERNAL_WORKQUEUE=1, libdispatch's own portable
#   userspace-thread-pool path; HAVE_PTHREAD_WORKQUEUE_QOS=1 is still
#   real and required separately (voucher.c hard-`#error`s without it --
#   QoS class support is a real, always-present pthread-header feature
#   independent of which thread-pool implementation is chosen).
# - dispatch_prefix_compat.h (new, force-`-include`d on every TU): closes
#   a real, recurring class of "needed regardless of this project's SPI
#   flags" gap -- firehose_activity_id_t/VOUCHER_CURRENT/
#   OS_FIREHOSE_SPI_VERSION are all real symbols libdispatch's own
#   voucher_internal.h/queue.c/init.c reference UNCONDITIONALLY even
#   though their real declaring headers (os/voucher_activity_private.h,
#   firehose/private.h) are gated behind OS_VOUCHER_ACTIVITY_SPI/
#   OS_FIREHOSE_SPI (both forced 0 here, no logd exists). Also real,
#   narrow SDK-vintage-shadow fixes for mach_get_times()/
#   thread_get_special_reply_port() (both genuinely declared,
#   unconditionally, in xnu-7195's real osfmk headers, but this
#   project's pinned iPhoneOS14.4 SDK ships its OWN same-named older
#   header found first via the normal sysroot search -- same
#   `-idirafter` can't win against a same-named file class of gap this
#   project has hit before, e.g. mach/mach_vm.h, worked around here with
#   forward declarations instead of fighting search order) and a real,
#   forced `#include <os/tsd.h>` (xnu-7195's real one, with a real arm64
#   `_os_cpu_number()` reading TPIDRRO_EL0 -- the same register this
#   project's own Phase 7.5 pthread TSD bootstrap already uses) since
#   shims/tsd.h's own `__has_include(<os/tsd.h>)` check is independent of
#   (and always true ahead of) the config-gated `#include` next to it.
# - firehose/private.h (new, dispatch_compat_headers/): real-enough type
#   stand-ins (firehose_activity_id_t etc, all confirmed via grep to be
#   either plain uint64_t handles or opaque pointers nothing in the
#   compiled file set dereferences) for the real firehose/private.h this
#   project's pinned libdispatch checkout never vendored (only
#   src/firehose/*'s own ring-buffer IMPLEMENTATION exists, not the
#   public/private API surface external callers include) --
#   voucher_internal.h's own struct voucher_s/_voucher_mach_udata_s embed
#   these UNCONDITIONALLY regardless of the tracing SPI flags (real
#   Apple keeps the wire/struct layout stable either way). One of these,
#   voucher_activity_hooks_s, needed a REAL (not opaque) 2-field body --
#   see that file's own comment for why.
# - mach/mach_vm.h, os/base_private.h (new, dispatch_compat_headers/):
#   narrow, separate copies of two already-solved real gaps
#   (build_libmalloc.sh's own libc_build/compat_include/{mach/mach_vm.h,
#   nothing for base_private.h}) -- NOT reused directly via
#   `-I compat_include`, since that directory's own os/assumes.h is a
#   DIFFERENT real gap's deliberately-empty stand-in (for vfprintf.c, an
#   unrelated consumer) that would shadow libdispatch's real, actual need
#   for third_party/Libc/os/assumes.h's real os_assert (see
#   os_assumes_compat.c's own header for that already-fixed history).
# - libdispatch.patch (third_party-patches/, 2 commits, applied via
#   `git am` by setup_third_party.sh): two real, minimal upstream fixes
#   applied directly to the vendored source (this project's established
#   patch mechanism, same as bash.patch/dtc-AppleDeviceTree.patch) rather
#   than worked around via compat headers, because neither is
#   header-shadow-able: (1) queue_internal.h's own
#   dispatch_pthread_root_queue_observer_hooks_s typedef was used by
#   value ~190 lines before its own definition -- an incomplete type used
#   by value, illegal in C regardless of platform; moved the typedef
#   earlier, removed the now-duplicate later one. (2) voucher.c's own
#   real ~410-line "voucher_activity_t" pragma-mark section (firehose
#   ring-buffer creation/management) has zero real callers anywhere in
#   this project's actual XPC/launchd source or elsewhere in libdispatch
#   itself -- disabled via `#if 0` rather than porting the ~1600-line
#   firehose engine it depends on (same os_log/firehose scoping decision
#   as everywhere else in Phase 6.8).
#
# DONE this pass (real, verified via actual compile+archive+trial link,
# not just -fsyntax-only): all 19 files above compile to real .o's,
# archived into libc_build/libdispatch_real.a (23 objects with the real
# mig protocolUser.o/protocolServer.o). A real, functionally-exercising
# trial link (dispatch_trial_main.c -- calls dispatch_once/
# dispatch_queue_create/dispatch_async/dispatch_semaphore_*/
# dispatch_source_create, not an empty main()) against this project's
# existing libmach_ipc/libclosure/libmalloc/libplatform/libpthread/libc
# stack narrows the real undefined-symbol surface to 53 real symbols
# (down from an initial 122) -- see this script's own trial-link output
# for the exact, current list. Two more real upstream fixes found and
# patched via libdispatch.patch during this pass: shims/tsd.h's
# `_dispatch_thread_port()` needed an explicit `(pthread_t)` cast (C
# tolerates the implicit uintptr_t->pthread_t conversion the macro used
# to do; C++ -- needed once block.cpp entered the build -- does not);
# init.c's own real global-key list was missing `dispatch_priority_key`/
# `dispatch_r2k_key` even though queue.c calls
# `_dispatch_thread_key_create()` on both unconditionally (a real,
# narrow upstream omission in this exact vendored file, not a portability
# issue). event_kevent.c also needed a real upstream typo fix
# (`(void)guard_ptr` referencing a parameter actually named `fd_ptr`) and
# two firehose bank-flag no-ops (dead code behind an always-false
# `if (_firehose_task_buffer)` check, since nothing ever populates that
# global -- see firehose/private.h's own comment).
#
# The remaining 53-symbol gap is overwhelmingly libpthread's scope, not
# libdispatch's own: ~18 are standard POSIX pthread API entirely absent
# from this project's curated libpthread (`pthread_attr_init`/
# `_destroy`/`_setdetachstate`/`_setstacksize`/`_setschedparam`/
# `_setschedpolicy`/`_get_qos_class_np`/`_set_qos_class_np`,
# `pthread_exit`, `pthread_sigmask`, `pthread_setschedparam`,
# `pthread_get_stackaddr_np`, `qos_class_main`,
# `pthread_qos_max_parallelism`, `pthread_set_qos_class_self_np`); ~9 are
# real QoS-override/workqueue functions
# (`_pthread_qos_override_start/end_direct`,
# `_pthread_workqueue_override_*`, `_pthread_workloop_create/destroy`,
# `_pthread_set_properties_self`, `_pthread_qos_class_encode`) genuinely
# unimplemented (confirmed via `nm -g`, not assumed); a handful are raw
# traps needing stubs this project hasn't generated yet
# (`kevent_qos`/`clock_gettime_nsec_np`/`kpersona_get`/
# `thread_destruct_special_reply_port`/`___kdebug_trace64`/`mmap`/
# `munmap` -- `mmap`/`munmap`'s RAW trap objects already exist
# (`pthread___mmap.o`/`pthread___munmap.o`) but only export the
# triple-underscore raw name, not the convenience wrapper libdispatch
# actually calls); a few are real Mach voucher MIG routines
# (`mach_voucher_attr_command`/`_deallocate`/`_debug_info`/
# `_extract_attr_recipe`, `host_create_mach_voucher`,
# `host_get_special_port`) needing a `mach_voucher.defs`/
# `host_priv.defs` regen this project hasn't done; `__dispatch_
# wait_for_enqueuer`/`__os_semaphore_create`/`_dispose`/
# `__os_workgroup_tsd_cleanup` are real libdispatch-internal functions
# whose real source file hasn't been located/attempted yet;
# `_malloc_engaged_nano`/`_malloc_memory_event_handler` are real
# libmalloc internals this project's curated libmalloc doesn't
# implement. None of this blocks resuming the actual launchd/libxpc
# work (see docs/daemon-roadmap.md) -- it's a real, bounded, separately
# schedulable follow-up.
#
# Concrete next steps: rewire build_libxpc.sh/build_libnotify.sh/etc
# away from libdispatch_min.a onto this real archive; close the 53-symbol
# gap above (mostly a libpthread task); only after both, decide whether
# dispatch_min.a can be deleted outright or should stay as a documented
# fallback during the migration.
set -e
cd "$(dirname "$0")"

DISPATCH="../../third_party/libdispatch"
XNU="../../third_party/xnu-7195"
PTHREAD="../../third_party/libpthread"
PLATFORM="../../third_party/libplatform"
LIBC="../../third_party/Libc"
COMPAT="dispatch_compat_headers"
OUT="libc_build"

if [ ! -d "$DISPATCH/src" ]; then
    echo "error: $DISPATCH/src not found -- run setup_third_party.sh first" >&2
    exit 1
fi

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi
CLANGXX="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++"
MIG="$(which mig)"
if [ -z "$MIG" ] || [ ! -x "$MIG" ]; then
    echo "error: mig tool not found" >&2
    exit 1
fi
if [ ! -d "$OUT/dyld_compat_sys_override" ]; then
    echo "error: $OUT/dyld_compat_sys_override not found -- run build_dyld_core.sh first (reused here)" >&2
    exit 1
fi

# --- real mig regen of src/protocol.defs (libdispatch's own internal
# MIG protocol, self-contained) ---------------------------------------
mkdir -p "$OUT/dispatch_mig"
"$MIG" -arch arm64 -I"$XNU/osfmk" \
    -header "$OUT/dispatch_mig/protocol.h" -user "$OUT/dispatch_mig/protocolUser.c" \
    -server "$OUT/dispatch_mig/protocolServer.c" -sheader "$OUT/dispatch_mig/protocolServer.h" \
    "$DISPATCH/src/protocol.defs"
echo "mig: protocol.defs regenerated cleanly"

# DAR-416: DISPATCH_USE_DIRECT_TSD (see config_ac.h) pulls in libpthread's
# private/pthread/tsd_private.h, which includes the installed-SDK path
# <System/machine/cpu_capabilities.h>. Forward it to xnu's real
# osfmk/arm/cpu_capabilities.h, as build_libplatform.sh does for its own
# compile (it writes the same one-line forwarder).
mkdir -p "$OUT/dispatch_compat_system/System/machine"
printf '#include <arm/cpu_capabilities.h>\n' > "$OUT/dispatch_compat_system/System/machine/cpu_capabilities.h"

CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    `# DAR-237: no -DOS_OBJECT_USE_OBJC=0 any more. HAVE_OBJC=1` \
    `# (config_ac.h) and internal.h:80-96 set it per file, 1 for .m,` \
    `# 0 for C, as Apple builds it.` \
    -DOS_VOUCHER_ACTIVITY_SPI=0 -DOS_FIREHOSE_SPI=0 \
    `# libdispatch_init() real-caller investigation, 2026-09-01 (DAR-153,` \
    `# syslogd runtime SIGTRAP investigation) -- real root cause found` \
    `# and fixed, NOT via this CFLAGS block (kept as a comment so the` \
    `# trail isn't lost). _dispatch_alloc_try_create_heap() (src/` \
    `# allocator.c) crashed with a real, deterministic` \
    `# DISPATCH_CLIENT_CRASH(KERN_INVALID_ARGUMENT) on every syslogd` \
    `# boot -- root-caused via a real kprintf + raw write(2) probe (both` \
    `# reverted after use) to MAGAZINES_PER_HEAP (NUM_CPU) reading back` \
    `# 0, because _dispatch_hw_config (the BSS global` \
    `# dispatch_hw_config() reads) is only ever written by` \
    `# _dispatch_hw_config_init(), itself only ever called from` \
    `# queue.c's libdispatch_init() -- which was never being called at` \
    `# all on this target. Real Apple relies on Libsystem's own real,` \
    `# modular libSystem_initializer() to call libdispatch_init()` \
    `# directly as part of coordinated libSystem bootstrap; this` \
    `# project's libSystem.B.dylib is the pre-dyld flattened static` \
    `# libc/pthread/malloc stack (CLAUDE.md's "Static-vs-dylib` \
    `# architecture debt"), not a real libSystem_initializer.` \
    `# FIRST ATTEMPT (real, but proven wrong by testing): src/init.c's` \
    `# own USE_LIBDISPATCH_INIT_CONSTRUCTOR knob makes _libdispatch_init` \
    `# a plain __attribute__((constructor)) -- real Apple's own` \
    `# documented escape hatch for platforms with no libSystem_initializer` \
    `# (e.g. swift-corelibs-libdispatch on Linux). Tried it here; a` \
    `# temporary write(2) probe inside _libdispatch_init (reverted after` \
    `# use) proved it NEVER fires on this target -- this project's real` \
    `# dyld (DyldAPIs.cpp's runAllInitializersForMain()) deliberately` \
    `# marks every loaded image whose install_name starts with` \
    `# "/usr/lib/system/lib" (matches /usr/lib/system/libdispatch.dylib)` \
    `# as already-inited WITHOUT running its constructors, real Apple` \
    `# behavior that assumes libSystem_initializer already handled them` \
    `# -- exactly the mechanism this target lacks, so the constructor` \
    `# never runs either way. REAL FIX: src/allocator.c's own` \
    `# _dispatch_continuation_alloc_init() (guarded by the` \
    `# already-existing _dispatch_continuation_alloc_init_pred` \
    `# dispatch_once -- real Apple's own lazy "first continuation` \
    `# allocation" hook) now calls libdispatch_init() once before doing` \
    `# anything else -- see that function's own updated comment.` \
    -D__API_UNAVAILABLE_PLATFORM_bridgeos=ios,unavailable \
    -D__API_AVAILABLE_PLATFORM_bridgeos\(x\)=ios,introduced=x \
    -DSPI_AVAILABLE\(...\)= -D__SPI_AVAILABLE\(...\)= \
    -include "$COMPAT/dispatch_prefix_compat.h" \
    -I "$COMPAT" -I "$OUT/dispatch_mig" \
    -I "$DISPATCH" -I "$DISPATCH/private" -I "$DISPATCH/src" \
    -I "$OUT/dyld_compat_sys_override" \
    -idirafter "$XNU/bsd" -idirafter "$XNU/osfmk" -idirafter "$XNU/libsyscall" -idirafter "$XNU/libsyscall/mach" \
    -idirafter "$LIBC" \
    -idirafter "$PTHREAD/include" -idirafter "$PTHREAD/private" \
    -idirafter "$PLATFORM/private" -idirafter "$PLATFORM/include" -idirafter "$PLATFORM/internal" \
    -idirafter "$OUT/dispatch_compat_system")

mkdir -p "$OUT/dispatch_obj" "$OUT/dispatch_obj/event"
for f in once semaphore apply time allocator init queue object source voucher io mach eventlink transform data; do
    "$CLANG" "${CFLAGS[@]}" "$DISPATCH/src/$f.c" -o "$OUT/dispatch_obj/$f.o"
done
for f in event/event event/event_kevent event/workqueue; do
    "$CLANG" "${CFLAGS[@]}" "$DISPATCH/src/$f.c" -o "$OUT/dispatch_obj/$f.o"
done
"$CLANG" "${CFLAGS[@]}" "$DISPATCH/src/shims/lock.c" -o "$OUT/dispatch_obj/lock.o"
# Real gap found 2026-08-27 while bringing up syslogd: this script
# compiled lock.c but never its real neighbor, shims/yield.c -- a
# genuine missing-file omission, not an architecture gap. yield.c's own
# real _dispatch_wait_for_enqueuer() (a real arm64 WFE-based spin-wait,
# used by dispatch_sync()'s real enqueuer-wait fast path) was silently
# absent from libdispatch_real.a and, downstream, from libdispatch.dylib
# -- confirmed via a real QEMU crash ("symbol not found in flat
# namespace '__dispatch_wait_for_enqueuer'") the first time any real
# consumer (syslogd.macho) actually exercised that code path at runtime.
"$CLANG" "${CFLAGS[@]}" "$DISPATCH/src/shims/yield.c" -o "$OUT/dispatch_obj/yield.o"
# Real gap, same class as yield.c above, found immediately after fixing
# it (removing __dispatch_wait_for_enqueuer from build_libdispatch_dylib.sh's
# UNDEF_SYMS surfaced the next real crash): shims/lock.c's real semaphore
# cache (os_get_cached_semaphore()/os_put_cached_semaphore(), via
# <os/semaphore_private.h>) calls the real os_semaphore_t primitive
# (_os_semaphore_create/_os_semaphore_dispose/_os_semaphore_signal/
# _os_semaphore_wait), whose real, small, self-contained implementation
# lives at third_party/libplatform/src/os/semaphore.c -- NOT
# third_party/libdispatch/src/semaphore.c (already compiled above, the
# unrelated higher-level dispatch_semaphore_t public API) -- and was
# never compiled anywhere in this project. Real, additional include
# paths needed beyond this script's shared CFLAGS: $OUT/compat_include
# (real System/arm/cpu_capabilities.h forwarder, already built by an
# earlier tier) and $XNU/libkern (real os/atomic_private.h, genuinely
# present in this project's own xnu-7195 fork). Deliberately NOT added to
# the shared CFLAGS array above -- a real trial showed $XNU/libkern
# shadows a DIFFERENT, incompatible os/atomic_private.h the other real
# dispatch files above already resolve correctly through their own
# existing search order, breaking once.c/queue.c/etc with real atomic-
# macro type errors. This one file gets its own extended CFLAGS instead.
# Needs real semaphore_create/semaphore_destroy (Mach traps, deferred via
# build_libdispatch_dylib.sh's own -U mechanism -- not curated anywhere
# in this dylib's own dependency stack) and real semaphore_signal/
# semaphore_wait (already curated via that same script's existing
# semaphore_wrap.o extraction).
# (DAR-416 step 1a-ii, 2026-09-27: libplatform's os/semaphore.c is no longer
# compiled into libdispatch. It is libsystem_platform's now, its Apple home
# (build_libsystem_platform.sh), and lock.c binds _os_semaphore_* there.
# The history above explains why it was here. The rm keeps a stale object
# from an older build out of the archive below.)
rm -f "$OUT/dispatch_obj/os_semaphore.o"
echo "compiled: 20/20 core+data+event+lock+yield dispatch files (zero errors)"

# queue.c's _libdispatch_init() references _os_workgroup_tsd_cleanup (a
# TSD destructor), and with USE_OBJC object.m's workgroup classes need
# workgroup.c's dispose/debug functions. dispatch_workgroup_dispose_compat.c
# (DAR-238) carries those functions verbatim from workgroup.c. It
# replaces dispatch_workgroup_tsd_compat.c, which held only the TSD
# destructor.
rm -f "$OUT/dispatch_obj/workgroup_tsd_compat.o"
"$CLANG" "${CFLAGS[@]}" dispatch_workgroup_dispose_compat.c -o "$OUT/dispatch_obj/workgroup_dispose_compat.o"
echo "compiled: dispatch_workgroup_dispose_compat.c (DAR-238)"

# DAR-237 (2026-09-28): object.m and data.m, the USE_OBJC halves of the
# object model. object.m's _os_object_init() is objc4's only real caller
# of _objc_init() (src/object.m:72-75; objc-os.mm:922-945). Apple
# compiles them as Objective-C without ARC (object.m:24-26 #errors under
# ARC; libdispatch.xcconfig in rel/libdispatch-1173:54 sets
# CLANG_LINK_OBJC_RUNTIME = NO, and the 1271 snapshot's xcconfig is
# stripped). The objc/ headers come straight from third_party/objc4
# through a local symlink farm, the same flattening build_objc4.sh does,
# so this does not depend on libobjc.A.dylib having been built (which
# needs libsystem_notify.dylib, which needs this).
OBJC4_RT="$(cd ../../third_party/objc4/runtime && pwd)"
rm -rf "$OUT/dispatch_objc_headers"
mkdir -p "$OUT/dispatch_objc_headers/objc"
for h in "$OBJC4_RT"/*.h; do
    ln -sf "$h" "$OUT/dispatch_objc_headers/objc/$(basename "$h")"
done
for f in object data; do
    "$CLANG" "${CFLAGS[@]}" -x objective-c -fno-objc-arc -fobjc-exceptions \
        -I "$OUT/dispatch_objc_headers" \
        "$DISPATCH/src/$f.m" -o "$OUT/dispatch_obj/${f}_objc.o"
done
echo "compiled: object.m, data.m (Objective-C, no ARC)"

# real C++11 file (Blocks-runtime integration only, no ObjC -- see this
# script's own HONEST SCOPE STATEMENT); needed the same explicit
# (pthread_t) cast fix as everything else using the portable
# _dispatch_thread_port() path (C tolerates the implicit uintptr_t ->
# pthread_t conversion that macro used to do, C++ does not -- real,
# narrow patch, see libdispatch.patch).
"$CLANGXX" "${CFLAGS[@]}" -std=c++11 -fno-exceptions -x c++ "$DISPATCH/src/block.cpp" -o "$OUT/dispatch_obj/block.o"
echo "compiled: real block.cpp (C++11, no ObjC)"

# real mig client+server-side objects (self-contained, needs nothing
# from the rest of libdispatch beyond mach_msg/mig plumbing this project
# already has in libmach_ipc.a) -- server side provides
# _libdispatch_internal_protocol_server, dispatch_mach's real receive
# dispatcher.
"$CLANG" "${CFLAGS[@]}" "$OUT/dispatch_mig/protocolUser.c" -o "$OUT/dispatch_obj/protocolUser.o"
"$CLANG" "${CFLAGS[@]}" "$OUT/dispatch_mig/protocolServer.c" -o "$OUT/dispatch_obj/protocolServer.o"
echo "compiled: real protocolUser.c/protocolServer.c"

rm -f "$OUT/libdispatch_real.a"
rm -f "$OUT/libdispatch_real.a"   # ar rcs into an existing archive keeps members no longer built
ar rcs "$OUT/libdispatch_real.a" "$OUT"/dispatch_obj/*.o "$OUT"/dispatch_obj/event/*.o
echo "wrote $OUT/libdispatch_real.a ($(ls "$OUT"/dispatch_obj/*.o "$OUT"/dispatch_obj/event/*.o | wc -l | tr -d ' ') objects)"

# Real trial link -- same "real, functionally-exercising main(), not an
# empty one" precedent as build_libxpc.sh's own trial (an empty main()
# proves nothing: static linking only pulls in archive members actually
# referenced transitively). Informational, not a hard failure -- see
# this script's own "NOT DONE YET" note above for the real, expected gap.
#
# Trial main compiled into its OWN subdirectory ($OUT/dispatch_obj/
# trial/, not $OUT/dispatch_obj/ itself) -- fixed 2026-09-03 (DAR-229,
# per-library dedup series). Root cause of a real, confirmed bug:
# build_libdispatch_dylib.sh's own dylib link globs
# "$OUT"/dispatch_obj/*.o directly (loose object files, not this
# script's libdispatch_real.a), and since THIS script's real archive
# step (just above) already ran before this trial main gets compiled,
# libdispatch_real.a itself was always clean -- but dispatch_trial_main.o
# stayed on disk in the same directory afterward, and the NEXT script
# (build_libdispatch_dylib.sh, a separate later invocation) swept it up
# wholesale into the shipped libdispatch.dylib's own `_main` export.
# Confirmed via `nm -m`: libdispatch.dylib exported `_main` from exactly
# this object. A subdirectory the dylib build's own `*.o` glob can never
# match is a permanent fix, not a one-time cleanup.
mkdir -p "$OUT/dispatch_obj/trial"
"$CLANG" "${CFLAGS[@]}" dispatch_trial_main.c -o "$OUT/dispatch_obj/trial/dispatch_trial_main.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -o "$OUT/dispatch_trial.macho" \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT/dispatch_obj/trial/dispatch_trial_main.o" \
    "$OUT/libdispatch_real.a" \
    "$OUT/libmach_ipc.a" "$OUT/libclosure.a" "$OUT/libmalloc.a" "$OUT/cerror_stub.o" "$OUT/libplatform.a" \
    "$OUT/libpthread.a" "$OUT/libc_gen.a" "$OUT/libc_string.a" \
    "$OUT/libc_locale_for_stdio.a" "$OUT/libc_stdio.a" \
    "$OUT/simple_obj/sys__kqueue.o" "$OUT/simple_obj/apply_apply_sigsuspend_compat.o" \
    2>"$OUT/dispatch_trial_link.err" \
    && echo "real trial link: 0 undefined symbols -- fully closed" \
    || { echo "expected-for-now: real trial link found $(grep -c 'Undefined symbol' "$OUT/dispatch_trial_link.err") undefined symbol group(s):"; grep '^  "' "$OUT/dispatch_trial_link.err" | sed 's/,.*//' | sort -u; }
