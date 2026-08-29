#!/bin/bash
# Builds libc_build/system/libbsm.dylib: static-to-dylib migration,
# docs/backlog.md item d8 -- d8's own note is confirmed for real here:
# third_party/BSM/bsm/lib/Makefile (real upstream) already targets a
# dylib directly (`$(GCC) -dynamiclib ... -install_name /usr/lib/
# $(LIB_NAME) -compatibility_version $(LIBCURVERS) -current_version
# $(LIBCURVERS)` with LIB_NAME=libbsm.dylib, LIBCURVERS=1.0) -- this
# project chose `.a` anyway for consistency with sibling scripts at the
# time; this script is the real conversion, using the exact real
# install_name/versioning from that upstream Makefile, not guessed.
#
# Real, unmodified source: reuses every already-built member object out
# of libc_build/libbsm.a (ar x, not recompiled -- see build_bsm.sh's own
# header for the full scope), plus two real, cheap, already-compiled
# objects (geteuid.o/getuid.o, extracted from syslogd_libc_extras.a --
# same real objects build_libdispatch_dylib.sh already reuses the same
# way; confirmed via nm that their only dependency, _cerror_nocancel, is
# already resolved via the libsystem_kernel.dylib sibling link below)
# that a real dylib link surfaces as genuine external references this
# archive's own narrower smoketest never needed.
#
# Real, honestly-classified `-Wl,-U` deferrals (found via a real
# nm-based intra-archive resolution pass -- extract every libbsm.a
# member, union defined symbols, subtract from union of undefined refs,
# then subtract every symbol the sibling dylibs below actually export;
# not guessed):
#   - getegid/getgid: real BSD syscalls, confirmed absent from this
#     checkout's Libc (no getegid.c/getgid.c found anywhere in
#     third_party/Libc) -- genuinely missing project-wide, same
#     "confirmed absent, not guessed" standard as docs/backlog.md item
#     d7's sysdir_* entries. geteuid/getuid ARE closable (see above);
#     these two siblings are not, yet.
#   - getgrgid/getpwuid: real Libinfo passwd/group-database lookups,
#     genuinely heavier (NSS-style module dispatch on real Darwin) --
#     not attempted this pass.
#   - notify_cancel/notify_check/notify_register_check: real functions,
#     confirmed already defined in libc_build/libnotify.a (`nm -g`) --
#     genuinely resolvable, just not yet as a sibling .dylib
#     (libnotify.dylib is a separate, not-yet-done unit in this same
#     migration pass -- see docs/backlog.md item d8). Deferred via
#     `-Wl,-U` now, to be verified resolving for real once
#     libnotify.dylib exists and both are linked by the same real
#     consumer (same iterative pattern build_libresolv_dylib.sh already
#     used for these exact three symbols).
#   - syslog: real function, no ASL/syslog client wired into this
#     project's dylib graph yet (docs/backlog.md item d6's `asl_*`
#     entry -- pinned as syslog-385, not yet built as a sibling dylib).
#   - strtok_r: real, but this project's libsystem_c.dylib only exports
#     the internal double-underscore `__strtok_r` (confirmed via
#     `nm -gU`), not the plain public alias -- and per this project's
#     own documented `-Wl,-alias` gotcha (alias BASE must be locally
#     compiled in the SAME link, or ld64 silently emits a malformed
#     reexport -- see memory/feedback on this), this dylib can't safely
#     alias to a symbol that's only reachable cross-dylib. Deferred, not
#     a new gap (same underlying libsystem_c.dylib export-parity class
#     docs/backlog.md item d7 already tracks).
#
# Real, cheap gap CLOSED via a sibling dylib link, not deferred:
# inet_ntoa/inet_ntop -- real functions, now a real, direct export of
# this same migration pass's own libresolv.dylib (build_libresolv_dylib.sh,
# committed earlier this pass) -- linked as a real sibling dependency
# below instead of deferred.
set -e
cd "$(dirname "$0")"

OUT="libc_build"
EXTRACT="$OUT/libbsm_dylib/extracted"
rm -rf "$EXTRACT"
mkdir -p "$EXTRACT"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -f "$OUT/libbsm.a" ]; then
    echo "error: $OUT/libbsm.a not found -- run build_bsm.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libresolv.dylib" ]; then
    echo "error: $OUT/system/libresolv.dylib not found -- run build_libresolv_dylib.sh first (this script links it as a real sibling for inet_ntoa/inet_ntop)" >&2
    exit 1
fi
for d in libsystem_c libsystem_malloc libsystem_platform libsystem_kernel libsystem_pthread; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2
        exit 1
    fi
done

# Real, verified fix (docs/backlog.md item d9): Xcode 12's own bundled
# ld64-609 has a real internal crash under -Wl,-fixup_chains that a much
# newer ld does not. Compiling still uses Xcode 12's clang/SDK above;
# only the final link step's linker binary changes, via clang's -B
# override.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi

(cd "$EXTRACT" && ar x "../../libbsm.a")
(cd "$EXTRACT" && ar x "../../syslogd_libc_extras.a" geteuid.o getuid.o)

mkdir -p "$OUT/system"
rm -f "$OUT/system/libbsm.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/libbsm.dylib \
    -compatibility_version 1.0 -current_version 1.0 \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-U,_getegid -Wl,-U,_getgid \
    -Wl,-U,_getgrgid -Wl,-U,_getpwuid \
    -Wl,-U,_notify_cancel -Wl,-U,_notify_check -Wl,-U,_notify_register_check \
    -Wl,-U,_syslog -Wl,-U,_strtok_r \
    -o "$OUT/system/libbsm.dylib" \
    "$EXTRACT"/*.o \
    "$OUT/system/libresolv.dylib" \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_pthread.dylib" \
    2>"$OUT/libbsm_dylib_link.err" \
    && echo "wrote $OUT/system/libbsm.dylib" \
    || { echo "libbsm.dylib link found real undefined symbols:" >&2; grep '^  "' "$OUT/libbsm_dylib_link.err" | sed 's/,.*//' | sort -u >&2; exit 1; }

otool -hv "$OUT/system/libbsm.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
# Real check (NOT plain `nm -u` -- that lists every externally-resolved
# symbol too, bound or not, which is not itself a failure signal; see
# build_libresolv_dylib.sh's own verification comment for the full
# reasoning): `dyld_info -fixups` shows every declared `-Wl,-U` deferral
# bound to `<flat-namespace>` and everything else bound to a real, named
# sibling dylib.
echo "declared -Wl,-U deferrals (expect: <flat-namespace> bind target):"
xcrun dyld_info -fixups "$OUT/system/libbsm.dylib" 2>/dev/null | grep '<flat-namespace>' | awk '{print "  " $NF}'
UNEXPECTED=$(xcrun dyld_info -fixups "$OUT/system/libbsm.dylib" 2>/dev/null | grep -v -E '<flat-namespace>|<weak-def-coalesce>' | grep 'bind' | grep -v -E 'libsystem_c/|libsystem_malloc/|libsystem_platform/|libsystem_kernel/|libsystem_pthread/|libresolv/' || true)
if [ -n "$UNEXPECTED" ]; then
    echo "error: real, unexpected non-sibling bind targets found:" >&2
    echo "$UNEXPECTED" >&2
    exit 1
fi
echo "libbsm.dylib: every non-deferred external reference binds to a real, named sibling dylib -- no unexpected/dangling references"
