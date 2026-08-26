#!/bin/bash
# Builds libc_build/libdispatch_real.a: the REAL Apple libdispatch
# (third_party/libdispatch, tag libdispatch-1271.40.12, ~28K real LOC),
# replacing this project's earlier dispatch_min.a (a 393-line
# hand-written single-thread shim, see build_libdispatch_min.sh) --
# real-source-available shims are architecture debt in this project, not
# a design goal (see docs/daemon-roadmap.md's "libdispatch real port"
# section). This script currently only proves the real, -fsyntax-only
# COMPILABILITY of the core dispatch files against this project's real
# SDK+xnu-7195+libpthread+libplatform headers -- it does not yet compile
# to real .o files, archive, or link (see "NOT DONE YET" below).
#
# ============================================================
# HONEST SCOPE STATEMENT -- read this first
# ============================================================
# CLEAN (0 real compile errors, confirmed via real `clang -fsyntax-only`,
# not assumed): once.c, semaphore.c, apply.c, time.c, allocator.c,
# init.c, queue.c, object.c, source.c, voucher.c, io.c, mach.c,
# eventlink.c, transform.c -- 14 of the real core dispatch source files.
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
# NOT DONE YET (real, concrete next steps): compile these 14 files to
# real .o files and archive them (this script currently only runs
# `-fsyntax-only`, proving compilability, not producing link-ready
# objects); a real trial link against this project's existing
# libc/libpthread/libplatform/libmach_ipc stack to find the real
# undefined-symbol surface (expect at least: _firehose_task_buffer/
# _firehose_task_buffer_pred, declared extern in voucher_internal.h,
# now genuinely unresolvable since their only real definition was inside
# the #if 0-disabled block above -- needs a small real compat definition,
# not a re-enable); rewiring build_libxpc.sh/build_libnotify.sh/etc away
# from libdispatch_min.a onto this real archive once it links; and only
# after all of that, deciding whether dispatch_min.a can be deleted
# outright or should stay as a documented fallback during the migration.
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

CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -fsyntax-only \
    -Wno-nullability-completeness -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 -DOS_OBJECT_USE_OBJC=0 \
    -DOS_VOUCHER_ACTIVITY_SPI=0 -DOS_FIREHOSE_SPI=0 \
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
    -idirafter "$PLATFORM/private" -idirafter "$PLATFORM/include" -idirafter "$PLATFORM/internal")

ok=0
fail=0
for f in once semaphore apply time allocator init queue object source voucher io mach eventlink transform; do
    if "$CLANG" "${CFLAGS[@]}" "$DISPATCH/src/$f.c" 2>"$OUT/dispatch_mig/${f}_syntax.err"; then
        ok=$((ok+1))
    else
        fail=$((fail+1))
        echo "FAIL: $f.c -- see $OUT/dispatch_mig/${f}_syntax.err" >&2
    fi
done
echo "real -fsyntax-only trial: $ok/14 core dispatch files compile clean (workgroup.c/block.cpp/firehose/swift deliberately excluded, see HONEST SCOPE STATEMENT above)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi
