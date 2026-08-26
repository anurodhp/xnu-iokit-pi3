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
if [ ! -f "$OUT/libxpc.a" ] || [ ! -f "$OUT/liblaunchd_mig.a" ] || [ ! -f "$OUT/liblaunchd_syscalls.a" ]; then
    echo "error: $OUT/libxpc.a, $OUT/liblaunchd_mig.a, and $OUT/liblaunchd_syscalls.a required -- run build_libxpc.sh / build_launchd_mig.sh / build_launchd_syscalls.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/libCoreFoundation.a" ] || [ ! -f "$OUT/cf_libc_extras.a" ]; then
    echo "error: $OUT/libCoreFoundation.a and $OUT/cf_libc_extras.a required -- run build_corefoundation.sh first" >&2
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

# Real Mach host-administration/port-management routines (docs/
# backlog.md item d5's own "Mach voucher/host-special-port" scope,
# extended here): host_reboot/host_set_exception_ports/
# host_set_special_port/host_set_UNDServer (real, clean MIG-generated
# names, no `_kernelrpc_` fast-path wrapper exists for these on real
# Apple either -- host_privUser.c compiled whole, checked via `nm`
# first for zero collisions against this project's existing stack, not
# assumed) + mach_port_set_mscount/get_set_status/move_member/
# request_notification/get_attributes/set_attributes (real MIG names
# from mach_port.defs are ALL `_kernelrpc_`-prefixed -- the clean public
# names come from a real, separate hand-written libsyscall/mach/
# mach_port.c; curated here, real logic minus the x86-only kernel fast-
# path trap optimization for 3 of the 6 -- see
# launchd_mach_port_compat.c's own header for the full real citation).
mkdir -p "$OUT/launchd_host_mig/mach"
MACH_PORT_DEFS="$(cd "$(dirname "$XNU/osfmk/mach/mach_port.defs")" && pwd)/mach_port.defs"
HOST_PRIV_DEFS="$(cd "$(dirname "$XNU/osfmk/mach/host_priv.defs")" && pwd)/host_priv.defs"
MIG="$(which mig)"
if [ -z "$MIG" ]; then
    echo "error: mig tool not found (needed for real mach_port.defs/host_priv.defs regen)" >&2
    exit 1
fi
"$MIG" -arch arm64 \
    -header "$OUT/launchd_host_mig/mach/mach_port_internal.h" \
    -user "$OUT/launchd_host_mig/mach_portUser.c" -server "$OUT/launchd_host_mig/mach_portServer.c" -sheader "$OUT/launchd_host_mig/mach_portServerHeader.h" \
    "$MACH_PORT_DEFS" >/dev/null
"$MIG" -arch arm64 \
    -header "$OUT/launchd_host_mig/mach/host_priv_internal.h" \
    -user "$OUT/launchd_host_mig/host_privUser.c" -server "$OUT/launchd_host_mig/host_privServer.c" -sheader "$OUT/launchd_host_mig/host_privServerHeader.h" \
    "$HOST_PRIV_DEFS" >/dev/null
HOSTMIG_FLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -DPRIVATE=1 \
    -I "$OUT/launchd_host_mig" -I "$OUT/launchd_host_mig/mach" \
    -idirafter "$XNU/osfmk" -idirafter "$XNU/libkern" -idirafter "$XNU/bsd")
"$CLANG" "${HOSTMIG_FLAGS[@]}" "$OUT/launchd_host_mig/mach_portUser.c" -o "$OUT/launchd_core_obj/mach_portUser.o"
"$CLANG" "${HOSTMIG_FLAGS[@]}" "$OUT/launchd_host_mig/host_privUser.c" -o "$OUT/launchd_core_obj/host_privUser.o"
"$CLANG" "${HOSTMIG_FLAGS[@]}" launchd_mach_port_compat.c -o "$OUT/launchd_core_obj/launchd_mach_port_compat.o"
# Real arm64 Mach trap stub for pid_for_task (real trap -46, 2 args --
# see pid_for_task_trap.s's own header for the full citation).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -arch arm64 \
    pid_for_task_trap.s -o "$OUT/launchd_core_obj/pid_for_task_trap.o"
# Real mig_allocate() (libsyscall/mach/mig_allocate.c, unmodified --
# real MIG-interface memory allocator, needed once mach_portUser.c/
# host_privUser.c above are real code paths that can actually run).
"$CLANG" "${HOSTMIG_FLAGS[@]}" "$XNU/libsyscall/mach/mig_allocate.c" -o "$OUT/launchd_core_obj/mig_allocate.o"
echo "compiled: real host_priv/mach_port MIG regen + real mach_port.c-derived compat + real pid_for_task trap + real mig_allocate (zero errors)"

rm -f "$OUT/liblaunchd_core.a"
ar rcs "$OUT/liblaunchd_core.a" "$OUT"/launchd_core_obj/*.o
echo "wrote $OUT/liblaunchd_core.a ($(ls "$OUT"/launchd_core_obj/*.o | wc -l | tr -d ' ') objects)"

# Real, curated extraction of a handful of already-built-elsewhere BSD
# libc functions launchd/core.c's own trial link needs -- same selective
# `ar x` pattern build_libdispatch_dylib.sh/build_corefoundation.sh
# already established, not whole-archive linking (same C++/xattr
# pollution risk documented in docs/backlog.md item d7). Each member
# checked via a real trial link before inclusion; several other
# candidates (ctime_r/localtime_r via libopenpam.a's localtime.o,
# setlogmask via its oldsyslog.o) were tried and reverted the same way
# CFError's own selective-extraction pass reverted localtime.o/
# libproc.o/strtoul.o -- real, deep, not-locally-satisfied dependency
# chains for one function each, a net loss.
mkdir -p "$OUT/launchd_libc_extras_obj"
( cd "$OUT/launchd_libc_extras_obj" && \
  ar x "../libncurses.a" ncurses_extra_syscalls.o Libc_sys_sigaction.o libplatform_sigaction.o && \
  ar x "../libresolv.a" getifaddrs.o arc4random.o libresolv_socket_syscalls.o libresolv_getentropy_syscall.o && \
  ar x "../libcopyfile.a" copy.o && \
  ar x "../dyld_core.a" isnull.o && \
  ar x "../libopenpam.a" ls_fenv_compat.o && \
  ar x "../libremovefile.a" fstatat64.o qsort.o heapsort.o removefile_syscalls.o )
rm -f "$OUT/launchd_libc_extras.a"
ar rcs "$OUT/launchd_libc_extras.a" "$OUT"/launchd_libc_extras_obj/*.o
echo "wrote $OUT/launchd_libc_extras.a ($(ls "$OUT"/launchd_libc_extras_obj/*.o | wc -l | tr -d ' ') objects: chdir/access/strstr/sleep/sigprocmask/sigaction/getifaddrs/freeifaddrs/arc4random_uniform/bind/getentropy/uuid_copy/uuid_is_null/fegetenv/fesetenv/fstatat/qsort)"

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
#
# Real DYNAMIC link (not `-static`): now that CoreFoundation is wired in
# and depends on the real `libdispatch.dylib`, the static link mode this
# script used to use can't resolve dispatch/os_log symbols at all (real
# `ld64` limitation, not a real gap -- see docs/backlog.md item d7's
# "link-mode artifact" finding). Same recipe `build_dyld_test_exec.sh`/
# `build_corefoundation.sh` already established: no `-static`, plain
# dylib links on the command line, `-Wl,-fixup_chains`. `libdispatch_real.a`
# (the old static archive) is dropped in favor of `libdispatch.dylib`,
# per this project's 2026-08-25 dylib-first directive.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -Wl,-fixup_chains \
    -o "$OUT/launchd_trial.macho" \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    -Wl,-force_load,"$OUT/liblaunchd_core.a" \
    "$OUT/libCoreFoundation.a" "$OUT/cf_libc_extras.a" "$OUT/launchd_libc_extras.a" \
    "$OUT/libxpc.a" "$OUT/liblaunchd_mig.a" \
    "$OUT/libxpc_syscalls.a" "$OUT/libxpc_syscalls2.a" "$OUT/liblaunchd_syscalls.a" \
    "$OUT/libmach_ipc.a" "$OUT/libclosure.a" "$OUT/libmalloc.a" "$OUT/cerror_stub.o" "$OUT/libplatform.a" \
    "$OUT/libpthread.a" "$OUT/libc_gen.a" "$OUT/libc_string.a" "$OUT/libc_stdio.a" "$OUT/libc_printf.a" \
    "$OUT/libc_locale_for_stdio.a" \
    "$OUT/system/libdispatch.dylib" "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib" \
    2>"$OUT/launchd_trial_link.err" \
    && echo "real trial link: 0 undefined symbols -- fully closed" \
    || { n=$(grep -c 'Undefined symbol' "$OUT/launchd_trial_link.err" || true); echo "expected-for-now: real trial link found $n undefined symbol group(s):"; grep '^  "' "$OUT/launchd_trial_link.err" | sed 's/,.*//' | sort -u; }
