#!/bin/bash
# Real -fsyntax-only reconnaissance pass over launchd's own daemon
# source (third_party/PureDarwin/src/Libraries/XPC/launchd/*.c) --
# NOT a real build script yet (no .o/.a/link output), a reproducible
# checkpoint of the CFLAGS recipe + real compat headers found while
# investigating how close this source is to compiling, matching every
# other tier's "trial compile, fix real errors, repeat" methodology
# before committing to a real build script shape.
#
# ============================================================
# HONEST SCOPE STATEMENT -- read this first
# ============================================================
# CLEAN (0 real errors, all 8 real daemon source files): pd_launchd_boot.c,
# kill2.c, pd_sandbox_check.c, pd_console_login.c, pd_launchd_plist.c,
# log.c, ipc.c, launchd.c (the PID-1 entry point), and -- as of the real
# fixes below -- core.c (12,162 real lines, launchd's actual job-
# management engine). Every real daemon source file in launchd/ now
# compiles clean.
#
# Real fixes needed for launchd.c (all already-established patterns from
# elsewhere in this project, reused here, not new mechanisms):
# - `-include net/if_dl.h`: the real SDK's own copy already has a
#   complete `struct sockaddr_dl` (confirmed, unlike the mach_vm.h/
#   mach_time.h class of gap) -- it just isn't reached before
#   netinet6/nd6.h uses it, since core.c/launchd.c's own real #include
#   order (`net/if.h`, `net/if_var.h`, `netinet/in.h`, `netinet/in_var.h`,
#   `netinet6/nd6.h`) doesn't include it directly and nothing else in
#   that chain does either on this SDK.
# - launchd_compat_headers/sockaddr_in_4_6_compat.h (new): the real
#   `union sockaddr_in_4_6` xnu's own newer `bsd/netinet/in.h` has (used
#   by that same file's own `in_var.h`, a few hundred lines later) is
#   invisible here because this project's pinned iPhoneOS14.4 SDK ships
#   its own OLDER `netinet/in.h` under the identical include guard
#   (`_NETINET_IN_H_`), found first via the normal sysroot search -- the
#   same "SDK's own same-named older header wins" class of gap this
#   project already worked around for `mach/mach_vm.h`/`mach/mach_time.h`
#   (compat header with just the missing piece, not a search-order fight).
# - `-idirafter $XNU/libsyscall/wrappers/spawn`: real
#   `spawn_private.h` was simply never on the include path before.
#
# core.c (12,162 real lines) needs all of the above PLUS:
# - `-I $PD/libinfo` for `libinfo.h` (real, present in the sparse
#   PureDarwin checkout, just not on this reconnaissance's include path
#   before now).
# - `-idirafter third_party/Libinfo/lookup.subproj` for `aliasdb.h`:
#   PureDarwin's own bundled `libinfo/si_module.h` (a simplified local
#   copy) references `aliasdb.h`, which ISN'T in PureDarwin's own
#   sparse-checked-out libinfo/ directory -- but IS in this project's
#   separately-pinned real `third_party/Libinfo` repo (apple-oss-
#   distributions, tag Libinfo-542.40.3, already cloned for a different
#   purpose), which has the complete real `lookup.subproj/` this
#   simplified copy is missing pieces of.
# - the same `dispatch_compat_headers/mach/mach_vm.h` narrow compat
#   header build_libdispatch_dylib.sh already established (reused
#   directly, not re-derived) for `mach_vm_deallocate`.
#
# The remaining 7 errors are now ALL CLOSED too:
# - `LAUNCH_JOBKEY_DEFAULTS` (launchd_compat_headers/
#   launch_jobkey_defaults_compat.h, new): real launch.h has 88
#   LAUNCH_JOBKEY_* string-key macros but is genuinely missing this one
#   real embedded-only key (confirmed via grep); value ("Defaults")
#   matches that header's own uniform CamelCase-plist-key convention.
# - `struct sandbox_spawnattrs`/`sandbox_spawnattrs_init`/
#   `_setprofilename`/`_setcontainer` (launchd_compat_headers/
#   sandbox_spawnattrs_compat.h, new): real Apple's actual sandbox
#   spawn-attributes API, part of the closed-source Sandbox.framework/
#   libsandbox (never open-sourced -- same class of gap as corecrypto).
#   Extends this project's own already-established real precedent
#   (PureDarwin's vendored `launchd/System-compat/sandbox.h`, which
#   already makes the OLDER sandbox_check()/sandbox_init() API real,
#   honest "always allow" stand-ins since this target has no
#   Sandbox.kext policy engine at all) to the newer spawnattrs API the
#   exact same way -- a real, minimal struct plus no-op setters, not a
#   functional sandbox implementation this target already lacks.
# - `ld2xpc`/`xpc2ld` (ld2xpc_compat.c + launchd_compat_headers/
#   ld2xpc_compat.h, new): CONFIRMED via a fresh clone of the real,
#   historically-open-sourced apple-oss-distributions/launchd tag
#   `launchd-842.92.1` (the last launchd Apple ever published source
#   for) -- not just this project's own PureDarwin checkout -- that
#   these two launch_data_t<->xpc_object_t converters are called but
#   NEVER DEFINED anywhere in either real repository. They must come
#   from a private, closed-source part of Apple's real libxpc that both
#   real launchd trees link against but never ship source for. Real,
#   from-scratch recursive implementation (last resort, only after
#   confirming no real Apple source exists anywhere for it) mapping
#   between the two real, well-documented, structurally similar tagged-
#   union type systems (`launch.h`/`xpc/xpc.h`, both already fully
#   working in this project) -- see that file's own header comment for
#   the full per-type mapping and the one documented, never-actually-
#   exercised lossy edge case (raw fd/errno/mach-port values, which
#   never appear in the real Defaults-plist data this project's only
#   two real call sites process).
#
# See docs/backlog.md's P2 "d6" item for the tracked version of this list.
set -e
cd "$(dirname "$0")"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi

XNU="../../third_party/xnu-7195"
PD="../../third_party/PureDarwin/src/Libraries/XPC"
CR="../../third_party/PureDarwin/src/Libraries/CrashReporterClient"
BSM="../../third_party/BSM"
DISPATCH="../../third_party/libdispatch"
PTHREAD="../../third_party/libpthread"
PLATFORM="../../third_party/libplatform"
LIBC="../../third_party/Libc"
LIBINFO_REAL="../../third_party/Libinfo/lookup.subproj"
AVAIL_VERSIONS="../../build/xnu-latest-build/AvailabilityVersions/dst/usr/local/include"
OUT="libc_build"
COMPAT="launchd_compat_headers"
DCOMPAT="dispatch_compat_headers"
MIGOUT="$OUT/launchd_mig"

if [ ! -d "$PD/launchd" ]; then
    echo "error: $PD/launchd not found -- run setup_third_party.sh first" >&2
    exit 1
fi

CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -fsyntax-only \
    -Wno-nullability-completeness -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 -DOS_OBJECT_USE_OBJC=0 \
    -DOS_VOUCHER_ACTIVITY_SPI=0 -DOS_FIREHOSE_SPI=0 \
    -D__API_UNAVAILABLE_PLATFORM_bridgeos=ios,unavailable \
    -D__API_AVAILABLE_PLATFORM_bridgeos\(x\)=ios,introduced=x \
    -DSPI_AVAILABLE\(...\)= -D__SPI_AVAILABLE\(...\)= \
    -DMAX_GROUPS=16 \
    -include net/if_dl.h \
    -include "$COMPAT/sockaddr_in_4_6_compat.h" \
    -include "$COMPAT/launch_jobkey_defaults_compat.h" \
    -include "$COMPAT/sandbox_spawnattrs_compat.h" \
    -include "$COMPAT/ld2xpc_compat.h" \
    -I "$DCOMPAT" -I "$COMPAT" \
    -I "$OUT/dyld_compat_sys" -I "$OUT/dyld_compat_sys_override" \
    -I "$PD/launchd/System-compat" -I "$MIGOUT" \
    -I "$PD/libxpc/include" -I "$PD/libxpc" -I "$PD/launchd" -I "$PD/libnv" -I "$PD/libinfo" \
    -I "$CR/include" -I "$BSM/bsm/lib" -I "$DISPATCH" \
    -idirafter "$LIBINFO_REAL" \
    -idirafter "$AVAIL_VERSIONS" \
    -idirafter "$XNU/libsyscall/wrappers/libproc" -idirafter "$XNU/libsyscall/wrappers/spawn" \
    -idirafter "$XNU/bsd" -idirafter "$XNU/osfmk" -idirafter "$XNU/libsyscall" -idirafter "$XNU/libsyscall/mach" \
    -idirafter "$PTHREAD/include" -idirafter "$PTHREAD/private" \
    -idirafter "$PLATFORM/private" -idirafter "$PLATFORM/include" -idirafter "$PLATFORM/internal" \
    -idirafter "$XNU/BUILD.hdrs/dst/usr/local/include" -idirafter "$XNU/EXTERNAL_HEADERS" \
    -idirafter "$LIBC/darwin" -idirafter "$LIBC" -idirafter "$XNU/libkern")

ok=0
fail=0
for f in pd_launchd_boot kill2 pd_sandbox_check pd_console_login pd_launchd_plist log ipc launchd runtime; do
    if "$CLANG" "${CFLAGS[@]}" "$PD/launchd/$f.c" 2>/dev/null; then
        ok=$((ok+1))
    else
        fail=$((fail+1))
        echo "FAIL: $f.c" >&2
    fi
done
echo "real -fsyntax-only trial: $ok/9 small launchd files compile clean"

echo "=== core.c (12,162 lines) -- real remaining error count ==="
"$CLANG" "${CFLAGS[@]}" "$PD/launchd/core.c" 2>"$OUT/launchd_core_syntax.err" || true
grep -c "error:" "$OUT/launchd_core_syntax.err" || true
echo "(see $OUT/launchd_core_syntax.err for the real, current list -- also documented in this script's own header comment and docs/backlog.md's P2 d6 item)"
