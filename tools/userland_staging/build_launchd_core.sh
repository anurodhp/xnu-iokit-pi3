#!/bin/bash
# Builds libc_build/liblaunchd_core.a: real .o files for all 9 of
# launchd's own daemon source files (third_party/PureDarwin's
# launchd/*.c), promoting build_launchd_recon.sh's own -fsyntax-only
# reconnaissance (see that script's HONEST SCOPE STATEMENT for the full
# per-file real-fix writeup -- reused verbatim here, not re-derived)
# into real compiled objects.
#
# ============================================================
# HONEST SCOPE STATEMENT -- read this first
# ============================================================
# All 9 files (pd_launchd_boot.c, kill2.c, pd_sandbox_check.c,
# pd_console_login.c, pd_launchd_plist.c, log.c, ipc.c, launchd.c,
# core.c) compile to real .o's with zero errors, archived here.
#
# NOT DONE YET: an actual link into a real launchd executable. This
# script's own trial link (below) is informational only, against this
# project's existing static libc/libpthread/libplatform/libmach_ipc/
# liblaunchd_mig/libxpc/libdispatch_real stack -- expect a real,
# nontrivial undefined-symbol list (launchd's own core.c is ~12K lines
# calling deep into real BSD/Mach/XPC APIs this project's curated stack
# doesn't fully cover yet, plus the 5 real SERVER-side MIG files
# build_launchd_mig.sh already generates but doesn't compile --
# jobServer.c etc, which need core.c's own internal job_t type and
# aren't compiled into this archive either, a distinct follow-up).
# Real launchd is also dynamically linked against libSystem on real
# Darwin; this project's own libdispatch is now a real .dylib
# (build_libdispatch_dylib.sh) with its own separate real duplicate-
# global-state debt (docs/backlog.md P2 d1) -- the eventual real
# launchd executable's own link model (static vs. dynamic against that
# dylib) is a decision for the actual "wire launchd up as PID-1" phase,
# not this compile-and-archive step.
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
if [ ! -f "$OUT/libxpc.a" ] || [ ! -f "$OUT/liblaunchd_mig.a" ]; then
    echo "error: $OUT/libxpc.a and $OUT/liblaunchd_mig.a required -- run build_libxpc.sh / build_launchd_mig.sh first" >&2
    exit 1
fi

CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -Wno-deprecated-declarations -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 -DOS_OBJECT_USE_OBJC=0 \
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

mkdir -p "$OUT/launchd_core_obj"
for f in pd_launchd_boot kill2 pd_sandbox_check pd_console_login pd_launchd_plist log ipc launchd core runtime; do
    "$CLANG" "${CFLAGS[@]}" "$PD/launchd/$f.c" -o "$OUT/launchd_core_obj/$f.o"
done
"$CLANG" "${CFLAGS[@]}" ld2xpc_compat.c -o "$OUT/launchd_core_obj/ld2xpc_compat.o"

# real, unmodified BSM lib_wrappers.c (audit_token_to_au32() and 9 other
# real functions) -- see launchd_compat_headers/bsm/libbsm.h's own
# header comment for why runtime.c needs the real, unmodified upstream
# BSM repo's own libbsm.h (via that redirect) instead of this project's
# pinned SDK's own shipped copy, which marks audit_token_to_au32()
# API_UNAVAILABLE(ios,...) at the resolved-symbol level -- a real, hard
# clang error neither `__asm` symbol-renaming nor C-identifier-renaming
# can bypass (confirmed by real trial, not assumed).
CFLAGS_BSM=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -DMAX_GROUPS=16 -D__APPLE_API_PRIVATE=1 \
    -I "$COMPAT" -I "$BSM/bsm/lib" -idirafter "$XNU/bsd")
"$CLANG" "${CFLAGS_BSM[@]}" "$BSM/bsm/lib/lib_wrappers.c" -o "$OUT/launchd_core_obj/bsm_lib_wrappers.o"

# real SERVER-side MIG dispatch files build_launchd_mig.sh already
# generates but never compiled (that script's own header comment
# documented this as the concrete next step -- needs core.c's own
# internal job_t type, now provably reachable since core.c itself
# compiles clean). job_forwardServer.c specifically needs `-include
# core.h` (job_forward.defs's own generated header uses the real job_t
# typedef but doesn't declare/include it itself -- the other 6 files
# happen not to need it directly).
for f in jobServer job_forwardServer job_replyServer internalServer domainServer notifyServer mach_excServer; do
    "$CLANG" "${CFLAGS[@]}" -include core.h "$MIGOUT/$f.c" -o "$OUT/launchd_core_obj/$f.o"
done
echo "compiled: 9 real launchd daemon source files + ld2xpc_compat.c + real BSM lib_wrappers.c + 7 real SERVER-side MIG files (zero errors)"

rm -f "$OUT/liblaunchd_core.a"
ar rcs "$OUT/liblaunchd_core.a" "$OUT"/launchd_core_obj/*.o
echo "wrote $OUT/liblaunchd_core.a ($(ls "$OUT"/launchd_core_obj/*.o | wc -l | tr -d ' ') objects)"

# Real, informational trial link. First attempt linked plainly against
# liblaunchd_core.a (launchd.c has its own real main(), so unlike every
# other tier's own hand-written trial_main.c none was needed) and
# reported "0 undefined symbols" -- but a real check afterwards (`nm`
# for core.c's own well-known functions in the output binary) showed
# core.o/ipc.o/log.o were never actually pulled into the link at all:
# launchd.c's real main() reaches them only indirectly (dispatch source
# handlers, XPC connection callbacks stored as function pointers), which
# static linking's archive-member pull-in can't see as a "reference" --
# the same "empty main proves nothing" lesson from the libxpc tier,
# recurring in a subtler form. `-force_load` (real ld64 flag: include
# every member of this one archive unconditionally, regardless of
# whether anything visibly references it -- the correct tool for "prove
# this archive's full, real content links clean standalone", not a
# workaround) gives the real, honest picture instead.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -o "$OUT/launchd_trial.macho" \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    -Wl,-force_load,"$OUT/liblaunchd_core.a" \
    "$OUT/libxpc.a" "$OUT/liblaunchd_mig.a" "$OUT/libdispatch_real.a" \
    "$OUT/libxpc_syscalls.a" "$OUT/libxpc_syscalls2.a" \
    "$OUT/libmach_ipc.a" "$OUT/libclosure.a" "$OUT/libmalloc.a" "$OUT/cerror_stub.o" "$OUT/libplatform.a" \
    "$OUT/libpthread.a" "$OUT/libc_gen.a" "$OUT/libc_string.a" "$OUT/libc_stdio.a" "$OUT/libc_printf.a" \
    2>"$OUT/launchd_trial_link.err" \
    && echo "real trial link: 0 undefined symbols -- fully closed" \
    || { n=$(grep -c 'Undefined symbol' "$OUT/launchd_trial_link.err" || true); echo "expected-for-now: real trial link found $n undefined symbol group(s):"; grep '^  "' "$OUT/launchd_trial_link.err" | sed 's/,.*//' | sort -u; }
