#!/bin/bash
# Builds libc_build/system/libsystem_notify.dylib: static-to-dylib
# migration, docs/backlog.md item d8's "still-static, genuine conversion
# candidates" list (docs/daemon-roadmap.md's "Static-vs-dylib
# architecture debt" section). Real product name confirmed via
# build_libnotify.sh's own header comment (read against
# third_party/Libnotify/Libnotify.xcodeproj/project.pbxproj's own
# PBXNativeTarget): "product libsystem_notify.dylib" -- part of the real
# libSystem umbrella, not a standalone top-level /usr/lib/ dylib the way
# libresolv is.
#
# Real, single-dylib merge of TWO existing static archives, matching
# real Apple's own single-product structure (confirmed no member-name
# collision via `comm -12` before merging, not assumed): libnotify.a
# (the raw MIG transport -- notify_ipcUser.o + special-reply-port
# helpers, build_libnotify.sh) and libnotify_client.a (the real public
# API that calls it -- libnotify.o/notify_client.o/table.o +
# this project's own notify_dyld_compat.o/notify_fileport_syscalls.o,
# build_libnotify_client.sh). Both already real, already-verified member
# objects -- reused via `ar x`, not recompiled.
#
# Real nm-based intra-archive forensic pass (same 3-step method
# build_libresolv_dylib.sh's own header established: extract every
# member from both archives, union defined vs undefined symbols across
# ALL of them together, subtract to get the true cross-archive-boundary
# gap -- NOT either archive's own raw `nm -u`, which overcounts
# intra-archive refs) found 12 real gaps not covered by any of this
# project's 8 already-built sibling dylibs (libsystem_c/_malloc/
# _platform/_kernel/_pthread, libdispatch, libsystem_blocks,
# libSystem.B). All 12 deferred via `-Wl,-U`, individually classified,
# none papered over:
#   - xpc_dictionary_create/_set_string/_set_uint64/_release,
#     __xpc_bool_true: real XPC API this library's real notify_post()/
#     notify_register_dispatch() paths use to build an XPC message to
#     notifyd -- genuine, but libxpc.a was still a static archive in this
#     project as of this pass (its own dylib conversion was later in this
#     same migration sweep). RESOLVED 2026-09-01 (DAR-164): a real
#     libxpc.dylib now exists and is linked directly below, not deferred
#     via `-Wl,-U` anymore -- see this file's own updated comment at the
#     prerequisite check above for the real gap this closed.
#   - bootstrap_look_up2: real bootstrap/launchd port-lookup routine --
#     this project has no full launchd bootstrap namespace registrar for
#     this library's client-side lookup path to call into (same class of
#     gap already accepted project-wide for vproc_swap_integer et al,
#     see docs/daemon-roadmap.md).
#   - creat, pipe, shm_open, usleep: real BSD syscalls genuinely not yet
#     wrapped anywhere in this project's dylib graph (confirmed via
#     `nm -gU` against all 8 sibling dylibs -- zero hits, not guessed).
#     Same real, project-wide "BSD syscall wrapper" gap class
#     docs/backlog.md item d7 already flagged for CFSocket.c, not
#     resolv/notify-specific.
#   - os_fault_with_payload, os_variant_has_internal_diagnostics: real
#     Apple internal diagnostic/telemetry helpers, not implemented
#     anywhere in this project (this port has no internal-diagnostics
#     variant concept at all).
#
# Real gotcha hit writing this script, worth remembering for the rest of
# this migration batch: the real linker symbol for `__xpc_bool_true` has
# TWO leading underscores (real Apple's own internal name, one more than
# the usual single compiler-added underscore) -- a real `ld: symbol(s)
# not found` even with `-Wl,-U,_xpc_bool_true` set, because that's simply
# the wrong symbol name, not a defer-vs-fixup-chains issue. Always
# confirm the exact spelling via `nm -u` on the specific .o that
# references it before writing the `-Wl,-U` flag, don't just add one
# underscore to whatever a text-processing pipeline printed.
set -e
cd "$(dirname "$0")"

OUT="libc_build"
EXTRACT="$OUT/libsystem_notify/extracted"
rm -rf "$EXTRACT"
mkdir -p "$EXTRACT"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -f "$OUT/libnotify.a" ] || [ ! -f "$OUT/libnotify_client.a" ]; then
    echo "error: libnotify.a / libnotify_client.a not found -- run build_libnotify.sh and build_libnotify_client.sh first" >&2
    exit 1
fi
for d in libsystem_c libsystem_malloc libsystem_platform libsystem_kernel libsystem_pthread libdispatch libsystem_blocks libncurses libxpc; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2
        exit 1
    fi
done
# Real gap, found 2026-09-13: "libdispatch" in the loop above derives to
# the wrong existing script (build_libdispatch.sh builds an unrelated
# static archive; the real dylib producer is build_libdispatch_dylib.sh).
if [ ! -f "$OUT/system/libdispatch.dylib" ]; then
    echo "error: $OUT/system/libdispatch.dylib not found -- run build_libdispatch_dylib.sh first" >&2
    exit 1
fi
# libxpc.dylib real dependency added (DAR-164, real gap found root-causing
# pd-console-login's post-exec abort -- see build_libsystem_kernel.sh's own
# updated comment for the wider real kernel-side-probe trail): this
# header's own comment above ("libxpc.a is still a static archive... Re-
# verify these resolve for real once libsystem_xpc.dylib (or equivalent)
# exists") is now stale -- a real libxpc.dylib was ported later in this
# same migration and DOES exist, just never wired back into this dylib's
# own link. Real, dyld-instrumented QEMU boot (a temporary raw-write probe
# added to third_party/dyld's Loader.cpp, reverted after use) confirmed
# the actual failure: `libresolv.dylib` (unconditionally reexported by
# libSystem.B.dylib, so present in EVERY process) loads this dylib, whose
# real notify_post()/notify_register_dispatch() code does a `<flat-
# namespace>` lookup for `__xpc_bool_true`/`_xpc_dictionary_*`/
# `_xpc_release` -- a lookup that only succeeds if the SPECIFIC PROCESS
# also happens to separately, directly link libxpc.dylib (true for
# syslogd.macho, false for pd-console-login/login/getty and, by the same
# reasoning, likely most of this project's other real binaries). Real
# Apple's shared cache always has libxpc.dylib mapped, masking this same
# structural gap; this project has no shared cache, so the fix is to make
# libxpc.dylib a real, direct load dependency of the one dylib whose own
# code needs it (matches this project's own established single-owner
# precedent, e.g. the `_dlclose`/ICU fix in build_libsystem.sh) rather
# than depending on every future consumer happening to link it too. No
# circular dependency (confirmed via a real `otool -L` on libxpc.dylib:
# it does not link libsystem_notify.dylib back).

# Real, verified fix (2026-08-28, see docs/backlog.md item d9 for the
# full trail): Xcode 12's own bundled ld64-609 has a real internal crash
# under -Wl,-fixup_chains that a much newer ld does not. Compiling still
# uses Xcode 12's clang/SDK above; only the final link step's linker
# binary changes, via clang's -B override.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi

(cd "$EXTRACT" && ar x "../../libnotify.a" && ar x "../../libnotify_client.a")
# notify_fileport_syscalls.o REMOVED from THIS dylib's own extract dir
# 2026-08-31 (static-linking cleanup, dylib-vs-dylib duplicate-symbol
# pass) -- not from libnotify_client.a itself, which build_bsm_dylib.sh/
# build_bsm.sh/build_libresolv_dylib.sh/build_syslogd.sh also consume
# (confirmed via grep none of them extract this specific member).
# `_fileport_makefd`/`_fileport_makeport` are real BSD syscalls
# (#430/#431) whose real Apple home is libsystem_kernel.dylib, which now
# generates them for real via the same create-syscalls.pl recipe used
# for geteuid/getegid/etc (see build_libsystem_kernel.sh's own comment)
# -- this dylib already links libsystem_kernel.dylib as a sibling below.
# `notify_client.o`'s one real, undefined reference to
# `_fileport_makeport` (confirmed via nm -m) now resolves there instead.
rm -f "$EXTRACT/notify_fileport_syscalls.o"

# notify_mig_special_reply_port_trap.o REMOVED from THIS dylib's own
# extract dir 2026-08-31 (DAR-124 sweep, same session as the fileport
# fix above): this real, 2-instruction Mach trap stub
# (thread_get_special_reply_port(), trap -50) was this dylib's own
# private copy, needed by notify_mig_special_reply_port.o's real
# mig_get_special_reply_port() (see that file's own header). A DAR-124
# audit gave libsystem_kernel.dylib real, canonical ownership of this
# exact symbol this same session (see build_libsystem_kernel.sh's own
# comment -- it was previously a genuine, confirmed flat-namespace gap
# in libdispatch.dylib, resolved only by accident wherever a process
# happened to carry a local copy). This dylib already links
# libsystem_kernel.dylib as a sibling below, so notify_mig_special_
# reply_port.o's one real reference now resolves there instead of
# carrying a second, independently-compiled copy -- confirmed via `nm
# -m` after rebuild. Left in place inside libnotify.a itself (unchanged)
# for any other, still-static consumer of that archive.
rm -f "$EXTRACT/notify_mig_special_reply_port_trap.o"

# notify_dyld_compat.o REMOVED from THIS dylib's own extract dir 2026-08-31
# (static-linking cleanup, dylib-vs-dylib duplicate-symbol pass) -- same
# "extract everything, then drop the one member a real sibling now
# covers" treatment as notify_fileport_syscalls.o above. That file's own
# header comment ("this project's fully-static (no dyld) build") is
# stale: real dyld exists now (Phase 6.5), and this project's real
# libdyld.dylib genuinely implements `_dyld_is_memory_immutable()` for
# real (libdyldGlue.cpp, real Apple source) -- notify_client.o's one
# call site (`_notify_strdup_if_mutable()`) gets the real answer instead
# of this project's own honest-but-now-superseded "always false" stand-in.
# "libdyld.dylib does not depend on libsystem_notify.dylib, so linking
# it here is not circular" was true when written (2026-08-31) but was
# silently invalidated 2026-09-14 by an unrelated fix elsewhere: this
# dylib is now a real, indirect prerequisite of libdyld.dylib
# (libdyld.sh -> build_libresolv_dylib.sh -> this script, the first
# edge load-bearing/real per that script's own header comment, the
# second added when libresolv_dylib.sh's own real-but-previously-
# unchecked need for libsystem_notify.dylib was fixed). Direct-linking
# libdyld.dylib here would make that a genuine cycle. Deferred via
# `-Wl,-U` instead -- same established pattern already used just below
# for _bootstrap_look_up2/_pipe, safe because libdyld.dylib is always
# loaded alongside via libSystem.B.dylib at real process runtime, so
# flat-namespace resolution still finds it.
rm -f "$EXTRACT/notify_dyld_compat.o"

# REAL BUG FOUND+FIXED 2026-08-29: creat/shm_open/usleep/
# os_fault_with_payload/os_variant_has_internal_diagnostics were
# deferred via `-Wl,-U` on the assumption an unreached symbol is
# harmless; it is not (real dyld eager chained-fixups binding fails at
# real process load time regardless of whether the call path executes,
# confirmed via a real QEMU boot of launchd: "symbol not found in flat
# namespace '_creat'"). Real fixes -- see
# libsystem_notify_misc_compat.c's own header.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libsystem_notify_misc_compat.c -o "$EXTRACT/libsystem_notify_misc_compat.o"

mkdir -p "$OUT/system"
rm -f "$OUT/system/libsystem_notify.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libsystem_notify.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-U,_bootstrap_look_up2 \
    -Wl,-U,_pipe \
    -Wl,-U,__dyld_is_memory_immutable \
    -o "$OUT/system/libsystem_notify.dylib" \
    "$EXTRACT"/*.o \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_pthread.dylib" "$OUT/system/libdispatch.dylib" \
    "$OUT/system/libsystem_blocks.dylib" "$OUT/system/libncurses.dylib" \
    "$OUT/system/libxpc.dylib" \
    2>"$OUT/libsystem_notify_link.err" \
    && echo "wrote $OUT/system/libsystem_notify.dylib" \
    || { echo "libsystem_notify.dylib link found real undefined symbols:" >&2; grep '^  "' "$OUT/libsystem_notify_link.err" | sed 's/,.*//' | sort -u >&2; cat "$OUT/libsystem_notify_link.err" >&2; exit 1; }

otool -hv "$OUT/system/libsystem_notify.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
# Real verification method (NOT plain `nm -u` emptiness -- that overcounts
# every symbol correctly bound to a sibling dylib too; see
# build_libresolv_dylib.sh's own verification comment for the full
# real-world discovery behind this): `dyld_info -fixups` shows each
# declared `-Wl,-U` deferral bound to `<flat-namespace>` and everything
# else bound to a real, named sibling dylib.
echo "declared -Wl,-U deferrals (expect: <flat-namespace> bind target, resolved at real load time):"
xcrun dyld_info -fixups "$OUT/system/libsystem_notify.dylib" 2>/dev/null | grep '<flat-namespace>' | awk '{print "  " $NF}'
UNEXPECTED=$(xcrun dyld_info -fixups "$OUT/system/libsystem_notify.dylib" 2>/dev/null | grep -v -E '<flat-namespace>|<weak-def-coalesce>' | grep 'bind' | grep -v -E 'libsystem_c/|libsystem_malloc/|libsystem_platform/|libsystem_kernel/|libsystem_pthread/|libdispatch/|libsystem_blocks/|libncurses|libdyld/|libxpc/' || true)
if [ -n "$UNEXPECTED" ]; then
    echo "error: real, unexpected non-sibling bind targets found (not flat-namespace, not one of the 8 linked sibling dylibs):" >&2
    echo "$UNEXPECTED" >&2
    exit 1
fi
echo "libsystem_notify.dylib: every non-deferred external reference binds to a real, named sibling dylib -- no unexpected/dangling references"
