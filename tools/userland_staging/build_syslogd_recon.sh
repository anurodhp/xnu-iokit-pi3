#!/bin/bash
# Real -fsyntax-only reconnaissance pass over syslogd's own daemon
# source (third_party/syslog, tag syslog-385, pinned 2026-08-27 -- see
# setup_third_party.sh's own header comment) -- NOT a real build script
# yet (no .o/.a/link output), same "trial compile, fix real errors,
# repeat" methodology as build_launchd_recon.sh before it, and this
# script's own CFLAGS recipe is adapted directly from that one (dispatch/
# launchd/notify/libxpc include paths all reused, not re-derived).
#
# ============================================================
# HONEST SCOPE STATEMENT -- read this first
# ============================================================
# Real scope decision (docs/daemon-roadmap.md's Phase 6.7 syslogd
# section has the full writeup): a legacy-only syslogd -- everything
# EXCEPT dbserver.c (the real Mach/XPC live ASL query service) -- since
# dbserver.c needs a real asl_ipc.defs MIG regen (not attempted this
# pass, bounded future work, same pattern as notify_ipc.defs before it)
# and, more fundamentally, its own database_server() only matters once
# real ASL clients (libsystem_asl's asl_open()) can actually reach a
# live syslogd at all -- which needs the bootstrap-namespace registrar
# shim scoped for notifyd (same Phase 6.7 section), not yet built.
#
# CLEAN (0 real errors, all 10 real files -- more than expected; only
# dbserver.c itself turned out to need real new work):
#   syslogd.tproj/klog_in.c, bsd_in.c, bsd_out.c, udp_in.c (the Mach-free
#     legacy input/output modules -- real dispatch_source_t/AF_UNIX
#     based, no XPC, no bootstrap dependency at all),
#   syslogd.tproj/daemon.c, syslogd.c (the real event-loop glue --
#     daemon.c's database_server()/dispatch_main() tail call means this
#     project's real libdispatch is a real, exercised dependency here,
#     not just a syntax formality),
#   syslogd.tproj/asl_action.c, remote.c (both surprised this pass --
#     originally scoped as "probably needs XPC/CF, defer" based on their
#     #include lines alone, but neither has a real xpc_*/CF call site;
#     both compile clean once the real XPC/dispatch headers below are on
#     the include path),
#   aslcommon/asl_common.c, asl_memory.c (shared support code both the
#     daemon and, eventually, the real ASL client need).
#
# NOT attempted this pass: dbserver.c (needs asl_ipc.h, MIG-generated
# from aslcommon/asl_ipc.defs -- real, bounded, mechanical, just not
# done yet); aslmanager.tproj/ (log rotation/archival) and util.tproj/
# (the syslog(1) CLI) -- both lower priority, not yet even attempted.
#
# Three real, narrow compat headers were needed (all in
# syslog_compat_headers/, all documented in their own header comments --
# summarized here, not duplicated):
# - sys/kevent_qos_compat.h: real struct kevent_qos_s/kqueue_id_t,
#   verbatim from xnu-7195's own kernel-internal bsd/sys/event.h --
#   confirmed absent from this project's pinned SDK's exported
#   <sys/event.h> even under -DPRIVATE=1 (grepped directly, zero
#   mentions). Needed because daemon.c directly #includes <libproc.h> ->
#   <sys/proc_info.h>, which has a real PRIVATE-gated struct field of
#   this exact type -- the same class of gap the earlier real
#   proc_pidinfo extraction (docs/daemon-roadmap.md) sidestepped by NOT
#   including proc_info.h at all; daemon.c's own real #include can't be
#   avoided the same way, so this time it's supplied for real instead.
# - os/transaction_private.h: real os_transaction_t API (used by
#   daemon.c to defer idle-exit while work is pending) has no source
#   anywhere in this project's pinned repos -- closed-source libxpc/
#   libdispatch SPI. Honest no-op stand-in: this project's PID-1 has no
#   idle-exit lifecycle for spawned daemons at all, so there's nothing
#   for a real implementation to hook into regardless.
# - configuration_profile.h: real MDM/enterprise-profile API, confirmed
#   never open-sourced (apple-oss-distributions/configuration_profile
#   404s). Only 2 real entry points are ever called
#   (configuration_profile_copy_property_list/
#   _create_notification_key, both in asl_action.c/asl_common.c);
#   both real call sites already null-check the return, confirmed by
#   reading them, so an honest "no profile installed" no-op is safe, not
#   a fabricated success.
#
# `servers/bootstrap.h` (syslogd.c's own #include) resolves for real via
# PureDarwin's libxpc port (third_party/PureDarwin/.../libxpc/include/
# servers/bootstrap.h) -- same real bootstrap_look_up2()-declaring header
# already used for the notifyd/launchd bootstrap-namespace investigation,
# not a new gap.
#
# See docs/daemon-roadmap.md's Phase 6.7 section for the full syslogd
# writeup and docs/backlog.md's item 17 for the real, live follow-up
# (don't wire real callers onto libsystem_asl's syslog(3) until the
# bootstrap-namespace shim exists).
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
SYSLOG="../../third_party/syslog"
DISPATCH="../../third_party/libdispatch"
PTHREAD="../../third_party/libpthread"
PLATFORM="../../third_party/libplatform"
LIBC="../../third_party/Libc"
NOTIFY="../../third_party/Libnotify"
PD="../../third_party/PureDarwin/src/Libraries/XPC"
AVAIL_VERSIONS="../../build/xnu-latest-build/AvailabilityVersions/dst/usr/local/include"
LAUNCHD_COMPAT="launchd_compat_headers"
DCOMPAT="dispatch_compat_headers"
SCOMPAT="syslog_compat_headers"
OUT="libc_build"

if [ ! -d "$SYSLOG/syslogd.tproj" ]; then
    echo "error: $SYSLOG/syslogd.tproj not found -- run setup_third_party.sh first" >&2
    exit 1
fi

CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -fsyntax-only \
    -Wno-nullability-completeness -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -D__API_UNAVAILABLE_PLATFORM_bridgeos=ios,unavailable \
    -D__API_AVAILABLE_PLATFORM_bridgeos\(x\)=ios,introduced=x \
    -DSPI_AVAILABLE\(...\)= -D__SPI_AVAILABLE\(...\)= \
    -include "$SCOMPAT/sys/kevent_qos_compat.h" \
    -I "$SCOMPAT" -I "$LAUNCHD_COMPAT" -I "$DCOMPAT" \
    -I "$SYSLOG/syslogd.tproj" -I "$SYSLOG/aslcommon" -I "$SYSLOG/libsystem_asl.tproj/include" \
    -I "$NOTIFY" -I "$PD/launchd/System-compat" -I "$PD/libxpc/include" \
    -I "$DISPATCH" -I "$DISPATCH/private" \
    -idirafter "$AVAIL_VERSIONS" \
    -idirafter "$XNU/libsyscall/wrappers/libproc" -idirafter "$XNU/libsyscall/wrappers/spawn" \
    -idirafter "$XNU/bsd" -idirafter "$XNU/osfmk" -idirafter "$XNU/libsyscall" -idirafter "$XNU/libsyscall/mach" \
    -idirafter "$PTHREAD/include" -idirafter "$PTHREAD/private" \
    -idirafter "$PLATFORM/private" -idirafter "$PLATFORM/include" -idirafter "$PLATFORM/internal" \
    -idirafter "$XNU/BUILD.hdrs/dst/usr/local/include" -idirafter "$XNU/EXTERNAL_HEADERS" \
    -idirafter "$LIBC/darwin" -idirafter "$LIBC" -idirafter "$XNU/libkern")

ok=0
fail=0
for f in klog_in bsd_in bsd_out udp_in daemon syslogd asl_action remote; do
    if "$CLANG" "${CFLAGS[@]}" "$SYSLOG/syslogd.tproj/$f.c" 2>/dev/null; then
        ok=$((ok+1))
    else
        fail=$((fail+1))
        echo "FAIL: syslogd.tproj/$f.c" >&2
    fi
done
for f in asl_common asl_memory; do
    if "$CLANG" "${CFLAGS[@]}" "$SYSLOG/aslcommon/$f.c" 2>/dev/null; then
        ok=$((ok+1))
    else
        fail=$((fail+1))
        echo "FAIL: aslcommon/$f.c" >&2
    fi
done
echo "real -fsyntax-only trial: $ok/10 legacy-scope syslogd files compile clean"

echo "=== dbserver.c -- real remaining error count (needs asl_ipc.h MIG regen, not attempted this pass) ==="
mkdir -p "$OUT"
"$CLANG" "${CFLAGS[@]}" "$SYSLOG/syslogd.tproj/dbserver.c" 2>"$OUT/syslogd_dbserver_syntax.err" || true
grep -c "error:" "$OUT/syslogd_dbserver_syntax.err" || true
echo "(see $OUT/syslogd_dbserver_syntax.err for the real, current list)"
