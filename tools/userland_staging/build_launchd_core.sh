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
# All files (pd_launchd_boot.c, kill2.c, pd_sandbox_check.c,
# pd_launchd_plist.c, pd_job_forward_server.c -- added 2026-08-28, real
# honest job_mig_look_up2_forward()/job_mig_parent_forward() stubs
# already returning BOOTSTRAP_NOT_PRIVILEGED in PureDarwin's own real,
# pinned source, just never compiled in before -- log.c, ipc.c,
# launchd.c, core.c, runtime.c, ktrace.c) compile to real .o's with zero
# errors, archived here. pd_console_login.c also compiles clean (real,
# zero errors) but is deliberately kept OUT of the archive -- it's its
# own separate real executable (launchd supervises it as a process, does
# not link into it), and its own real main() hard-conflicts with
# launchd.c's real main() otherwise (found via a real trial link).
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
LD="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi

# Real, verified fix (2026-08-28, see docs/backlog.md item d9 for the
# full trail and tools/userland_staging/build_simple_cmds.sh's own
# NEWLD_BINDIR header comment for the isolated repro this is based on):
# Xcode 12's own bundled ld64-609 has a real internal crash under
# -Wl,-fixup_chains that a much newer ld does not. Compiling still uses
# Xcode 12's clang/SDK above (untouched -- that vintage match is a
# separate, still-load-bearing invariant); only the final link step's
# linker binary changes, via clang's -B override.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
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
ICU_LIB="icu_build/target/lib"

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

rm -rf "$OUT/launchd_core_obj"
mkdir -p "$OUT/launchd_core_obj"
for f in pd_launchd_boot kill2 pd_sandbox_check pd_launchd_plist pd_job_forward_server log ipc launchd core runtime ktrace; do
    "$CLANG" "${CFLAGS[@]}" "$PD/launchd/$f.c" -o "$OUT/launchd_core_obj/$f.o"
done
# pd_console_login.c: real, but a genuinely separate real executable
# (its own header: "PureDarwin console login helper... launchd
# supervises this helper" -- launchd spawns it as its own process, does
# not exec/link into it) -- its own real main() duplicate-symbol-
# conflicts with launchd.c's real main() if bundled into the same
# archive (found via a real trial link, not assumed). Compiled here
# into a separate directory (still verified real, zero errors) but
# deliberately kept OUT of liblaunchd_core.a's own object glob below --
# needs its own real standalone build/binary target eventually, out of
# scope for this gap-closing pass.
mkdir -p "$OUT/pd_console_login_obj"
"$CLANG" "${CFLAGS[@]}" "$PD/launchd/pd_console_login.c" -o "$OUT/pd_console_login_obj/pd_console_login.o"
"$CLANG" "${CFLAGS[@]}" ld2xpc_compat.c -o "$OUT/launchd_core_obj/ld2xpc_compat.o"
# Real, from-scratch xpc_copy() -- same class of confirmed gap as
# ld2xpc/xpc2ld above (real libxpc/xpc_misc.c has no plain deep-copy
# function at all) -- see xpc_copy_compat.c's own header for the full
# real citation and scope.
"$CLANG" "${CFLAGS[@]}" xpc_copy_compat.c -o "$OUT/launchd_core_obj/xpc_copy_compat.o"
# Real os_log(3) engine: confirmed never open-sourced (no daemon to
# deliver to here regardless) -- see os_log_compat.c's own header for
# the full real-fix writeup (same honest-no-op shape as this project's
# existing pd_sandbox_check.c).
"$CLANG" "${CFLAGS[@]}" os_log_compat.c -o "$OUT/launchd_core_obj/os_log_compat.o"
# Real sysdir_start_search_path_enumeration()/_get_next_...(): confirmed
# never open-sourced -- see sysdir_compat.c's own header for the full
# real-fix writeup.
"$CLANG" "${CFLAGS[@]}" sysdir_compat.c -o "$OUT/launchd_core_obj/sysdir_compat.o"
# Real ASL client shim -- see asl_client_compat.c's own header for the
# full real-fix writeup (Phase 6.7/6.8's bootstrap-registrar
# prerequisite, not wired here on purpose).
"$CLANG" "${CFLAGS[@]}" asl_client_compat.c -o "$OUT/launchd_core_obj/asl_client_compat.o"
# Real setlogmask(3) -- see setlogmask_compat.c's own header for the
# full real-fix writeup (previously attempted via oldsyslog.o and
# reverted as a net loss; this standalone extraction avoids that
# dependency chain).
"$CLANG" "${CFLAGS[@]}" setlogmask_compat.c -o "$OUT/launchd_core_obj/setlogmask_compat.o"
# Real scalbn(3), extracted standalone -- see libm_scalbn_only_compat.c's
# own header for why the whole libm.a can't be linked here (real
# duplicate pow()/modf() vs CoreFoundation's own libm_compat.o).
"$CLANG" "${CFLAGS[@]}" libm_scalbn_only_compat.c -o "$OUT/launchd_core_obj/libm_scalbn_only_compat.o"

# Real mktime/localtime/localtime_r/ctime_r/asctime_r: fourth attempt,
# real fix found by looking at how PureDarwin's own CMake build gets
# past this exact wall -- it depends on a real `libc_features` step
# that generates libc-features.h via third_party/Libc's own real
# xcodescripts/generate_features.pl against a real per-platform
# Makefile.inc (third_party/Libc/Platforms/iphoneos/Makefile.inc).
# Neither PureDarwin's copy of that machinery nor ours had ever been
# run -- this project already has the real generator AND the real
# platform config pinned, just never invoked. Real, self-contained
# regen here (own tracked libc_features_platform/Platforms/iphoneos/
# Makefile.inc -- a real copy of the pinned one with exactly one real,
# documented change: FEATURE_TIMEZONE_CHANGE_NOTIFICATION disabled,
# since enabling it makes localtime.c call the real notify(3) CLIENT
# API this project has never ported -- only notifyd's real MIG SERVER
# side exists so far, see build_notify.sh). Also needed
# `-fno-stack-check` (a real, standard, safe clang flag -- avoids
# needing a from-scratch, safety-critical `__chkstk_darwin` stack-probe
# ABI routine with no real source anywhere in this project; real
# behavior is unaffected, this file just loses the early-detection
# guard-page safety net for unusually large stack frames) and
# localtime_private_decls.h (same "private name" declaration gap class
# as launchd_syscall_compat_headers/private_names_decls.h -- all real,
# already-linked functions, just missing declarations in this compile).
mkdir -p "$OUT/libc_features_gen"
ARCHS=arm64 VARIANT_PLATFORM_NAME=iphoneos SRCROOT="$(cd libc_features_platform && pwd)" \
    DERIVED_FILES_DIR="$(cd "$OUT" && pwd)/libc_features_gen" \
    perl "$LIBC/xcodescripts/generate_features.pl" >/dev/null
LOCALTIME_FLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -fno-stack-check \
    -include localtime_private_decls.h \
    -I "$LIBC/include" -I "$OUT/libc_features_gen/arm64" -I "$PTHREAD/private" \
    -idirafter "$LIBC/fbsdcompat" -idirafter "$LIBC/darwin" -idirafter "$XNU/bsd" -idirafter "$XNU/osfmk")
"$CLANG" "${LOCALTIME_FLAGS[@]}" "$LIBC/stdtime/FreeBSD/localtime.c" -o "$OUT/launchd_core_obj/localtime.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -DPRIVATE=1 \
    "$LIBC/secure/strcat_chk.c" -o "$OUT/launchd_core_obj/strcat_chk.o"

# Real __st_set_timezone/__st_get_timezone (localtime.c's own real
# tzset() path) -- real, zero-dependency source
# (third_party/Libc/stdtime/timezone_unix03.c), confirmed via nm this
# project's own libopenpam.a already has a copy too (unused here to
# avoid any archive-selection ambiguity -- this file is cheap enough to
# just compile fresh).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -DPRIVATE=1 \
    "$LIBC/stdtime/timezone_unix03.c" -o "$OUT/launchd_core_obj/timezone_unix03.o"

# NOT fixed, real gap confirmed: asctime()/asctime_r() (localtime.c's
# own ctime() needs the former; core.c's jobmgr_shutdown/_remove call
# the latter directly) both real-source-exist
# (stdtime/FreeBSD/asctime.c) but need real strftime_l(), which pulls
# the exact same deep xlocale.c locale-loading-machinery chain already
# tried and reverted for `strtoul_l` (docs/backlog.md item d7) --
# confirmed via real trial compile this pass, not re-guessed. Left
# deferred.

# Real __sprintf_chk/__vsprintf_chk (core.c calls fortified sprintf
# directly) + real __mach_errors data table (backing libxpc_syscalls.a's
# own mach_error_string.o, which this project already links but never
# had this table for).
"$CLANG" "${CFLAGS[@]}" "$LIBC/secure/sprintf_chk.c" -o "$OUT/launchd_core_obj/sprintf_chk.o"
"$CLANG" "${CFLAGS[@]}" "$LIBC/secure/vsprintf_chk.c" -o "$OUT/launchd_core_obj/vsprintf_chk.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -DPRIVATE=1 \
    -idirafter "$XNU/osfmk" -idirafter "$XNU/bsd" -idirafter "$XNU/libkern" -idirafter "$XNU/iokit" \
    "$XNU/libsyscall/mach/error_codes.c" -o "$OUT/launchd_core_obj/error_codes.o"

# Real __udivti3 (compiler-rt builtin, real standard binary-long-division
# algorithm -- no compiler-rt source is pinned anywhere in this project,
# see udivti3_compat.c's own header) -- runtime.c's real 128-bit
# fixed-point nanosecond-time math needs it.
"$CLANG" "${CFLAGS[@]}" udivti3_compat.c -o "$OUT/launchd_core_obj/udivti3_compat.o"

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
# Real kdebug_signpost/_start/_end (docs/backlog.md's own
# "runtime_ktrace gated behind a macro" theory was wrong -- root cause
# was simpler: real ktrace.c, third_party/PureDarwin's own real
# implementation of runtime_ktrace/0/1, was just never compiled into
# this archive. Its own real dependency, kdebug_signpost(), has no real
# source anywhere in this project -- see launchd_kdebug_signpost_compat.c's
# own header for why a safe no-op is correct here, not a simplification
# of reachable behavior.)
"$CLANG" "${CFLAGS[@]}" launchd_kdebug_signpost_compat.c -o "$OUT/launchd_core_obj/launchd_kdebug_signpost_compat.o"
echo "compiled: real host_priv/mach_port MIG regen + real mach_port.c-derived compat + real pid_for_task trap + real mig_allocate + real ktrace.c (zero errors)"

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
rm -rf "$OUT/launchd_libc_extras_obj"
mkdir -p "$OUT/launchd_libc_extras_obj"
( cd "$OUT/launchd_libc_extras_obj" && \
  ar x "../libncurses.a" ncurses_extra_syscalls.o Libc_sys_sigaction.o libplatform_sigaction.o && \
  ar x "../libresolv.a" getifaddrs.o arc4random.o libresolv_getentropy_syscall.o && \
  ar x "../libcopyfile.a" copy.o && \
  ar x "../dyld_core.a" isnull.o && \
  ar x "../libopenpam.a" ls_fenv_compat.o && \
  ar x "../libremovefile.a" fstatat64.o qsort.o heapsort.o removefile_syscalls.o )
# Real, narrowed re-exports (ld -r -exported_symbol, same precedent
# build_libdispatch_dylib.sh's own mach_make_memory_entry_64 extraction
# already established) for ncurses_extra_syscalls.o/removefile_syscalls.o:
# a real trial link found both objects independently define public
# read()/gettimeofday() (and ncurses_extra_syscalls.o also
# pthread_testcancel()) -- real, redundant, hard duplicate-symbol
# conflicts once both land in the same launchd_libc_extras.a, not a
# fabricated concern. Narrowing here (rather than editing either shared
# source file, both used by other consumers too) keeps every OTHER real
# symbol each object provides while dropping just the collisions --
# read()/gettimeofday() come from removefile_syscalls.o instead (kept,
# not duplicated), pthread_testcancel() comes from libpthread.a's own
# real pthread_guard_compat.o (already linked, more authoritative).
ar p "$OUT/libncurses.a" ncurses_extra_syscalls.o > "$OUT/launchd_libc_extras_obj/ncurses_extra_syscalls_raw.o"
"$LD" -r -arch arm64 \
    -exported_symbol ___darwin_check_fd_set_overflow -exported_symbol ___sigaction \
    -exported_symbol __sigtramp -exported_symbol _access -exported_symbol _chdir \
    -exported_symbol _fpathconf -exported_symbol _getpgrp -exported_symbol _kill \
    -exported_symbol _link -exported_symbol _mkdir -exported_symbol _select \
    -exported_symbol _sigprocmask -exported_symbol _sleep -exported_symbol _strstr \
    "$OUT/launchd_libc_extras_obj/ncurses_extra_syscalls_raw.o" \
    -o "$OUT/launchd_libc_extras_obj/ncurses_extra_syscalls.o"
rm -f "$OUT/launchd_libc_extras_obj/ncurses_extra_syscalls_raw.o"
ar p "$OUT/libremovefile.a" removefile_syscalls.o > "$OUT/launchd_libc_extras_obj/removefile_syscalls_raw.o"
"$LD" -r -arch arm64 \
    -exported_symbol _flsl -exported_symbol _getiopolicy_np -exported_symbol _gettimeofday \
    -exported_symbol _read -exported_symbol _reallocarray -exported_symbol _rename \
    -exported_symbol _rmdir -exported_symbol _unlink -exported_symbol _unlinkat \
    "$OUT/launchd_libc_extras_obj/removefile_syscalls_raw.o" \
    -o "$OUT/launchd_libc_extras_obj/removefile_syscalls.o"
rm -f "$OUT/launchd_libc_extras_obj/removefile_syscalls_raw.o"
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
# per this project's 2026-08-25 dylib-first directive. Also links
# `libsystem_pthread.dylib` directly (real localtime.c calls the
# internal `_pthread_rwlock_*` variants -- double-underscore names,
# distinct from the public POSIX ones -- which `libSystem.B.dylib`'s
# own small hand-picked re-export list, item d7's "export-parity gap"
# finding, doesn't include; the real sub-image that actually provides
# them is already built, just needed adding to this link directly).
    # __mh_dylib_header alias: real CFUniChar.c's __CFGetSectDataPtr()
    # hunts _dyld_get_image_header() results for a match against
    # &_mh_dylib_header to find ITS OWN image's __UNICODE section -- a
    # real assumption that CoreFoundation is always its own separate
    # dylib (true on real Apple systems, ld64 always synthesizes
    # _mh_dylib_header for a -dynamiclib link). This project statically
    # archives CoreFoundation into the executable instead (known dylib-
    # migration debt, docs/daemon-roadmap.md), so CF's own __UNICODE
    # section actually lives in the executable's image -- the real
    # equivalent ld64-synthesized symbol for THAT image is
    # _mh_execute_header. Aliasing makes the address comparison match
    # for real, not a fabrication: for this specific static-linked
    # build, the executable's own image IS the one CF's data lives in.
    # Legacy short-form CFLocale/CFDateFormatter constant aliases
    # (kCFLocaleLanguageCode etc): real CFLocaleKeys.c only defines these
    # via `__attribute__((alias(...)))` under `#if TARGET_OS_LINUX ||
    # TARGET_OS_BSD || TARGET_OS_WIN32` -- real macOS/iOS are excluded on
    # the assumption Apple's own proprietary CoreFoundation.framework
    # provides them some other way. Tried widening that guard to
    # TARGET_OS_MAC directly (2026-08-28) and reverted: clang rejects
    # `__attribute__((alias(...)))` outright on Darwin ("aliases are not
    # supported on darwin") -- confirms exactly why the guard excluded
    # Darwin in the first place. Real fix has to be a linker alias at
    # final-link time instead, same mechanism as __mh_dylib_header above
    # -- this project statically links this exact source as its entire
    # CoreFoundation, so these real, already-defined ...Key-suffixed
    # constants are the correct alias base.
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
    -o "$OUT/launchd_trial.macho" \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    -Wl,-force_load,"$OUT/liblaunchd_core.a" \
    "$OUT/libCoreFoundation.a" "$OUT/cf_libc_extras.a" "$OUT/launchd_libc_extras.a" \
    "$OUT/libxpc.a" "$OUT/liblaunchd_mig.a" \
    "$OUT/libxpc_syscalls.a" "$OUT/libxpc_syscalls2.a" "$OUT/liblaunchd_syscalls.a" \
    "$OUT/libmach_ipc.a" "$OUT/libclosure.a" "$OUT/libmalloc.a" "$OUT/cerror_stub.o" "$OUT/libplatform.a" \
    "$OUT/libpthread.a" "$OUT/libc_gen.a" "$OUT/libc_string.a" "$OUT/libc_stdio.a" "$OUT/libc_printf.a" \
    "$OUT/libc_locale_for_stdio.a" "$OUT/libbsm.a" \
    "$OUT/system/libdispatch.dylib" "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib" \
    "$OUT/system/libsystem_pthread.dylib" \
    "$ICU_LIB/libicuuc.dylib" "$ICU_LIB/libicui18n.dylib" "$ICU_LIB/libicudata.dylib" "$OUT/system/libc++.dylib" \
    2>"$OUT/launchd_trial_link.err" \
    && echo "real trial link: 0 undefined symbols -- fully closed" \
    || { n=$(grep -c 'Undefined symbol' "$OUT/launchd_trial_link.err" || true); echo "expected-for-now: real trial link found $n undefined symbol group(s):"; grep '^  "' "$OUT/launchd_trial_link.err" | sed 's/,.*//' | sort -u; }
