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
#     notifyd -- genuine, but libxpc.a is still a static archive in this
#     project as of this pass (its own dylib conversion is later in this
#     same migration sweep), so there is no real sibling dylib to link
#     against yet. Re-verify these resolve for real once
#     libsystem_xpc.dylib (or equivalent) exists.
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
for d in libsystem_c libsystem_malloc libsystem_platform libsystem_kernel libsystem_pthread libdispatch libsystem_blocks; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2
        exit 1
    fi
done

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

mkdir -p "$OUT/system"
rm -f "$OUT/system/libsystem_notify.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libsystem_notify.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-U,_xpc_dictionary_create -Wl,-U,_xpc_dictionary_set_string \
    -Wl,-U,_xpc_dictionary_set_uint64 -Wl,-U,_xpc_release -Wl,-U,__xpc_bool_true \
    -Wl,-U,_bootstrap_look_up2 \
    -Wl,-U,_creat -Wl,-U,_pipe -Wl,-U,_shm_open -Wl,-U,_usleep \
    -Wl,-U,_os_fault_with_payload -Wl,-U,_os_variant_has_internal_diagnostics \
    -o "$OUT/system/libsystem_notify.dylib" \
    "$EXTRACT"/*.o \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_pthread.dylib" "$OUT/system/libdispatch.dylib" \
    "$OUT/system/libsystem_blocks.dylib" \
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
UNEXPECTED=$(xcrun dyld_info -fixups "$OUT/system/libsystem_notify.dylib" 2>/dev/null | grep -v -E '<flat-namespace>|<weak-def-coalesce>' | grep 'bind' | grep -v -E 'libsystem_c/|libsystem_malloc/|libsystem_platform/|libsystem_kernel/|libsystem_pthread/|libdispatch/|libsystem_blocks/' || true)
if [ -n "$UNEXPECTED" ]; then
    echo "error: real, unexpected non-sibling bind targets found (not flat-namespace, not one of the 7 linked sibling dylibs):" >&2
    echo "$UNEXPECTED" >&2
    exit 1
fi
echo "libsystem_notify.dylib: every non-deferred external reference binds to a real, named sibling dylib -- no unexpected/dangling references"
