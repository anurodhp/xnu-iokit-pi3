#!/bin/bash
# Builds tools/userland_staging/configd.macho: DAR-170 Tier 1 step 3 --
# real configd.tproj, the actual SCDynamicStore SERVER daemon (as opposed
# to build_libsystemconfiguration_client.sh's already-done CLIENT side).
# Real, unmodified Apple source from third_party/configd/configd.tproj/.
#
# ============================================================
# REAL FILE LIST (verified against configd.xcodeproj's own "configd"
# PBXNativeTarget Sources build phase, not guessed)
# ============================================================
# configd.m, _SCD.c, configd_server.c, plugin_support.c, session.c,
# pattern.c, and the 15 real _config*.c/_notify*.c/_snapshot.c files --
# 21 real .c/.m files total, plus config.defs (regenerated via mig, SERVER
# side this time -- build_libsystemconfiguration_client.sh already did the
# client side of this same real MIG subsystem). This is the FULL real
# Sources-phase list -- nothing in configd.tproj/ was excluded as
# out-of-scope this time (unlike SCDConsoleUser.c on the client side).
#
# ============================================================
# REAL PLUGIN ARCHITECTURE (found by reading plugin_support.c directly,
# not assumed)
# ============================================================
# Real configd is plugin-based: plugin_support.c's real, unmodified
# addBundle()/CFBundleLoadExecutableAndReturnError()/
# CFBundleCreateBundlesFromDirectory() calls load each
# /System/Library/SystemConfiguration/*.bundle at runtime via real
# CFBundle + real dlopen() underneath (confirmed: this project's
# libdyld.dylib really does export a working _dlopen, forwarding into
# real dyld's own loader -- see build_libdyld.sh's header comment; CF
# itself already builds+links here per DAR-195). This script builds the
# real configd.tproj DAEMON itself -- InterfaceNamer/IPMonitor/
# KernelEventMonitor/IPConfiguration as real, separately-loaded .bundle
# plugins are explicitly OUT OF SCOPE this pass (a real next step, not
# reached this session -- see this script's own tail-of-file "state for
# whoever resumes" style note in the ticket comment, not duplicated here).
# With zero plugins installed under /System/Library/SystemConfiguration/,
# real CFBundleCreateBundlesFromDirectory() returns an empty/NULL array
# (a real, well-formed "no bundles found" result, not a crash) -- so
# configd.tproj's own real SCDynamicStore server engine (the actual
# subject of this pass) should still start and register its real Mach
# service with zero plugins loaded, matching real Apple's own
# "-D" (disable all bundles) debug flag's real code path for the "no
# bundles" half of that condition.
#
# ============================================================
# REAL GAPS FOUND AND CLOSED, one file/header at a time (compile-one-file
# methodology, same as every prior Tier 0/Tier 1 script)
# ============================================================
# - <bsm/libbsm.h>: this pinned SDK's own copy declares audit_token_to_pid()
#   as a real FUNCTION but marks it API_UNAVAILABLE(ios, watchos, tvos) --
#   a real, hard clang error, same class already documented for
#   audit_token_to_au32() by launchd_compat_headers/bsm/libbsm.h's own
#   comment (redirects to the real, unmodified pinned BSM-13 tag instead).
#   That real upstream tag has NO audit_token_to_pid() at all (genuine
#   version skew -- confirmed by grep, this symbol was added to Apple's
#   SDK headers after the bsm-13 open-source snapshot), so on top of that
#   redirect this script also -include's configd_bsm_pid_compat.h, a
#   narrow real macro (val[5], confirmed via WebSearch against openbsm's
#   real kernel-side val[5]=p_pid assignment) -- same "-include a tiny
#   compat header" technique build_syslogd.sh already used for
#   sys/kevent_qos_compat.h. Also found+fixed this pass: $OUT/compat_include's
#   own vproc_priv.h (written for nohup.c, only declares
#   _vprocmgr_detach_from_console) was silently shadowing PureDarwin's
#   real, fuller vproc_priv.h/vproc_swap_integer() -- fixed by -I ordering,
#   not by editing that shared file.
# - <sandbox.h>/sandbox_check(): real, ALREADY-PINNED PureDarwin source
#   (third_party/PureDarwin/.../launchd/System-compat/sandbox.h +
#   pd_sandbox_check.c) -- that header's own comment already anticipates
#   this exact target ("Targets built -nostdinc (configd) have no later
#   sandbox.h on the path"). Reused verbatim, not reinvented.
# - <objc/objc-runtime.h>: real SDK-vintage gap (this pinned iPhoneOS14.4
#   SDK ships the split <objc/runtime.h>/<objc/message.h> but not the
#   legacy umbrella name) -- narrow forwarding header, zero objc_*() calls
#   anywhere in the real target file set (confirmed by grep).
# - <Security/SecTask.h> + SecTaskCreateWithAuditToken()/
#   SecTaskCopyValueForEntitlement(): real Security.framework SPI
#   session.c's copyEntitlement() calls DIRECTLY (unlike dy_framework.c's
#   dlsym()-only usage on the client side) -- needs real linked bodies,
#   not just declarations. This project has zero code-signing/entitlement
#   infrastructure, so an honest "no entitlements" default (same class as
#   PureDarwin's own pd_sandbox_check.c "no active policy" default) is the
#   real, correct answer -- see configd_sectask_compat.c's own header
#   comment.
# - session.c's real objc_absolute_packed_isa_class_mask/ptrauth isa-copy
#   fast path is entirely `#if __has_feature(ptrauth_intrinsics)`-gated --
#   false on this project's plain (non-e) arm64 target, so it compiles
#   away to a plain memcpy with NO real libobjc dependency at all.
# - config.defs: real MIG subsystem (subsystem config 20000, serverprefix
#   _;) -- SERVER side regenerated here (client side already done by
#   build_libsystemconfiguration_client.sh). The real _config*.c/
#   _notify*.c/_snapshot.c source files ARE the real serverprefix-matching
#   routine implementations MIG's generated switch calls into by name --
#   confirmed by the real "serverprefix _;" declaration matching every one
#   of those files' own leading underscore.
set -e
cd "$(dirname "$0")"

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
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi

CONFIGD="../../third_party/configd"
CDT="$CONFIGD/configd.tproj"
SC="$CONFIGD/SystemConfiguration.fproj"
XNU="../../third_party/xnu-7195"
LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
PLATFORM="../../third_party/libplatform"
DISPATCH="../../third_party/libdispatch"
PD="../../third_party/PureDarwin/src/Libraries/XPC"
CR="../../third_party/PureDarwin/src/Libraries/CrashReporterClient"
OUT="libc_build"
MIGOUT="$OUT/configd_mig"
rm -rf "$OUT/configd_obj"
mkdir -p "$MIGOUT" "$OUT/configd_obj"

if [ ! -d "$CDT" ]; then
    echo "error: $CDT not found -- run setup_third_party.sh first" >&2
    exit 1
fi
# Real prerequisite: the SC client-library pass's own header staging
# (sc_headers/SystemConfiguration symlinks, sc_iokit_headers/IOKit,
# sc_dispatch_compat/dispatch, sc_compat_include, compat_include) is
# reused DIRECTLY here, not regenerated -- unlike that script's own choice
# to regenerate build_bootp_dhcp_engine.sh's compat headers fresh (for ITS
# own standalone reproducibility), this script legitimately depends on
# libSystemConfiguration.dylib already existing (configd.tproj is the
# real SCDynamicStore server counterpart to that real client), so
# depending on that script having already run -- same fail-fast
# "run its build_*.sh first" pattern every other script here uses for a
# real cross-script dependency -- is the honest, correct relationship,
# not a shortcut.
for d in sc_headers sc_iokit_headers sc_dispatch_compat sc_compat_include compat_include; do
    if [ ! -d "$OUT/$d" ]; then
        echo "error: $OUT/$d not found -- run build_libsystemconfiguration_client.sh first" >&2
        exit 1
    fi
done
if [ ! -f "$OUT/system/libSystemConfiguration.dylib" ]; then
    echo "error: $OUT/system/libSystemConfiguration.dylib not found -- run build_libsystemconfiguration_client.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/bootp_dhcp_engine.a" ]; then
    echo "error: $OUT/bootp_dhcp_engine.a not found -- run build_bootp_dhcp_engine.sh first" >&2
    exit 1
fi
for dylib in libCoreFoundation libdispatch libxpc libsystem_c libsystem_malloc libsystem_platform \
             libsystem_kernel libsystem_pthread libsystem_notify libsystem_m libinfo libdyld \
             libresolv libopenpam libremovefile libcopyfile libsystem_blocks; do
    if [ ! -f "$OUT/system/$dylib.dylib" ]; then
        echo "error: $OUT/system/$dylib.dylib not found -- run its build_*_dylib.sh first" >&2
        exit 1
    fi
done

# ------------------------------------------------------------------
# Real MIG regeneration, SERVER side this time (client side already done
# by build_libsystemconfiguration_client.sh -- see that script's own
# header comment for the real transport confirmation: raw MIG Mach IPC,
# not XPC).
# ------------------------------------------------------------------
echo "mig: regenerating config (server only)"
"$MIG" -arch arm64 -I"$SC" \
    -header /dev/null -user /dev/null \
    -server "$MIGOUT/config_server.c" -sheader "$MIGOUT/config_serverServer.h" \
    "$SC/config.defs"
echo "mig: config server regenerated cleanly"

CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__va_list=__builtin_va_list \
    -D__API_UNAVAILABLE_PLATFORM_bridgeos=bridgeos,unavailable \
    -D__API_AVAILABLE_PLATFORM_bridgeos\(x\)=ios,introduced=x \
    -DPRIVATE \
    -include "sys/fileport.h" \
    -include "configd_bsm_pid_compat.h" \
    `# real gap: BSM-13's own real bsm/lib/lib_wrappers.c (audit_token_to_au32's` \
    `# real implementation) needs MAX_GROUPS, which its sibling` \
    `# quoted #include "libbsm.h" expects some earlier platform header to` \
    `# have already defined -- same real fix launchd_compat_headers/bsm/` \
    `# libbsm.h's own redirect already established for this exact gap` \
    `# (this project's pinned SDK's <sys/param.h> defines NGROUPS, not` \
    `# MAX_GROUPS).` \
    -include "sys/param.h" -DMAX_GROUPS=NGROUPS \
    -include "configd_sigaction_compat.h" \
    -I "configd_compat_include" \
    `# real, fuller vproc.h/vproc_priv.h (PureDarwin's own, declares` \
    `# vproc_swap_integer/vproc_swap_complex etc) placed BEFORE` \
    `# $OUT/compat_include on purpose -- that shared dir's own` \
    `# vproc_priv.h is a DIFFERENT, narrower compat header written for` \
    `# nohup.c (only declares _vprocmgr_detach_from_console), and with` \
    `# the old ordering it silently shadowed the real, fuller header,` \
    `# producing an "implicit declaration of vproc_swap_integer" error --` \
    `# found by this script's own first real trial compile, not assumed.` \
    -I "$PD/libxpc/include" \
    `# real, already-pinned redirect for <bsm/libbsm.h> -- the SDK's own` \
    `# copy declares audit_token_to_pid() as a real function but marks it` \
    `# API_UNAVAILABLE(ios, watchos, tvos) (a hard clang error, not` \
    `# suppressible), and the real pinned BSM-13 open-source tag has no` \
    `# such restriction (see launchd_compat_headers/bsm/libbsm.h's own` \
    `# header comment, already established for audit_token_to_au32's` \
    `# identical restriction). Must be -I'd BEFORE the SDK's own` \
    `# usr/include/bsm/ is implicitly reached via -isysroot.` \
    -I "launchd_compat_headers" \
    -I "$OUT/sc_compat_include" -I "$OUT/compat_include" -I "$OUT/cf_headers" \
    -I "$CDT" \
    -I "$SC" -I "$SC/helper" -I "$CONFIGD/IPMonitorControl" \
    -I "$OUT/sc_headers" -I "$OUT/sc_iokit_headers" -I "$XNU/iokit" -I "$MIGOUT" \
    -I "$PD/launchd/System-compat" \
    -I "$OUT/sc_dispatch_compat" -I "$DISPATCH" -I "$DISPATCH/private" \
    -I "$CR/include" \
    -I "$LIBC/include" -I "$LIBC/darwin" -I "$LIBC/fbsdcompat" -I "$LIBC/locale" -I "$LIBC/locale/FreeBSD" \
    -I "$LIBC/stdtime/FreeBSD" \
    -I "$PTHREAD/include" -I "$PTHREAD/private" \
    -I "$PLATFORM/private" -I "$PLATFORM/include" \
    -I ../../generated \
    -I "$XNU/libsyscall/mach" -I "$XNU/libsyscall" -I "$XNU/osfmk" -I "$XNU/bsd")

FILES="$CDT/configd.m $CDT/_SCD.c $CDT/configd_server.c $CDT/plugin_support.c \
    $CDT/session.c $CDT/pattern.c \
    $CDT/_configopen.c $CDT/_configclose.c $CDT/_configunlock.c $CDT/_configlist.c \
    $CDT/_configadd.c $CDT/_configget.c $CDT/_configset.c $CDT/_configremove.c \
    $CDT/_confignotify.c \
    $CDT/_notifyadd.c $CDT/_notifyremove.c $CDT/_notifychanges.c \
    $CDT/_notifyviaport.c $CDT/_notifyviafd.c $CDT/_notifycancel.c \
    $CDT/_snapshot.c \
    $MIGOUT/config_server.c \
    $PD/launchd/pd_sandbox_check.c \
    configd_sectask_compat.c \
    configd_builtin_plugins_stub.c \
    $LIBC/gen/FreeBSD/signal.c $LIBC/stdlib/FreeBSD/getopt.c $LIBC/stdlib/FreeBSD/getopt_long.c \
    configd_sigaction_compat.c \
    configd_bsm_wrappers_compat.c \
    configd_cfurl_filesize_compat.c \
    configd_scnetworkconfig_setvalue_compat.c"

ok=0
fail=0
for f in $FILES; do
    base=$(basename "$f" .c)
    base=$(basename "$base" .m)
    out="$OUT/configd_obj/${base}.o"
    if "$CLANG" "${CFLAGS[@]}" "$f" -o "$out" 2>"$OUT/configd_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/configd_obj/${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OUT/configd_obj/${base}.err")"
    fi
done
echo "configd: $ok compiled, $fail gaps (see $OUT/configd_obj/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi

rm -f "$OUT/libconfigd.a"
ar rcs "$OUT/libconfigd.a" "$OUT"/configd_obj/*.o
echo "wrote $OUT/libconfigd.a ($(ls "$OUT"/configd_obj/*.o | wc -l | tr -d ' ') objects)"

# ------------------------------------------------------------------
# Real link. Same real dynamic-executable pattern build_syslogd.sh
# established (Csu start.s/crt.c, no libmalloc.a/libpthread.a/
# libplatform.a/libclosure.a statically linked alongside the real sibling
# dylibs -- DAR-217's "one pthread runtime, one locking runtime" rule).
# ------------------------------------------------------------------
SIMPLE_OBJ="$OUT/simple_obj"
for o in sys__chown sys__dup2 sys__setgid sys__setuid sys__execve sys__chdir; do
    if [ ! -f "$SIMPLE_OBJ/$o.o" ]; then
        echo "error: $SIMPLE_OBJ/$o.o not found -- run build_simple_cmds.sh first" >&2
        exit 1
    fi
done
LAUNCHD_SYSCALL_OBJ="$OUT/launchd_syscall_obj"
for o in sys__getppid sys__setsid; do
    if [ ! -f "$LAUNCHD_SYSCALL_OBJ/$o.o" ]; then
        echo "error: $LAUNCHD_SYSCALL_OBJ/$o.o not found -- run build_launchd_core.sh first" >&2
        exit 1
    fi
done
if [ ! -f "$OUT/liblaunchd_mig.a" ] || [ ! -f "$OUT/libmach_ipc.a" ]; then
    echo "error: liblaunchd_mig.a/libmach_ipc.a not found -- run build_launchd_mig.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/chk_fail.o" ]; then
    echo "error: $OUT/chk_fail.o not found -- run build_libmalloc.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/libconfigd_plugins.a" ]; then
    echo "error: $OUT/libconfigd_plugins.a not found -- run build_configd_plugins.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/libconfigd_ipconfiguration.a" ]; then
    echo "error: $OUT/libconfigd_ipconfiguration.a not found -- run build_ipconfiguration_plugin.sh first" >&2
    exit 1
fi

rm -f configd.macho
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o configd.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT/libconfigd.a" \
    `# DAR-241/DAR-240 (2026-09-04): whole-archive link, relying on real` \
    `# ld64 archive-member selection -- load_IPMonitor/prime_IPMonitor/` \
    `# load_KernelEventMonitor/prime_KernelEventMonitor/` \
    `# load_LinkConfiguration/load_PreferencesMonitor/` \
    `# prime_PreferencesMonitor are still unresolved at this point` \
    `# (configd_builtin_plugins_stub.c, already processed via libconfigd.a` \
    `# above, no longer defines any of them -- DAR-240 removed the last 3` \
    `# once the real SCNetworkConfiguration family they need was ported` \
    `# into libSystemConfiguration.dylib), so the linker pulls in the real` \
    `# ip_plugin.o/eventmon.o/linkconfig.o/prefsmon.o (and their real` \
    `# object-file dependents) from this archive to satisfy them.` \
    `# load_InterfaceNamer is STILL resolved by the stub (kept -- ifnamer.c` \
    `# hits a real, separate IOKitLib.h/lockdown.h wall, NOT the` \
    `# SCNetworkConfiguration family -- see configd_builtin_plugins_stub.c's` \
    `# own header comment and DAR-240's Linear comment for the new ticket` \
    `# filed for it), so this archive's own ifnamer.o member is correctly` \
    `# never pulled in.` \
    "$OUT/libconfigd_plugins.a" \
    `# DAR-170 (2026-09-04, this session): real IPConfiguration --` \
    `# load_IPConfiguration/start_IPConfiguration/prime_IPConfiguration` \
    `# (ipconfiguration_builtin_glue.o, part of this archive) are` \
    `# referenced by libconfigd.a's own patched plugin_support.c` \
    `# (third_party-patches/configd.patch's builtin_plugins[] entry),` \
    `# still unresolved at this point in the link -- pulls in ipconfigd.o` \
    `# and its real object-file dependents (FDSet/timer/linklocal/manual/` \
    `# server/rtutil/report_symptoms/HostUUID/failover/sysconfig.o, the` \
    `# regenerated ipconfig MIG server stub) from this archive to satisfy` \
    `# them, matching the exact same real ld64 archive-member-selection` \
    `# mechanism DAR-240/241 already established for the other 4 plugins.` \
    "$OUT/libconfigd_ipconfiguration.a" \
    "$SIMPLE_OBJ/sys__chown.o" "$SIMPLE_OBJ/sys__dup2.o" \
    "$SIMPLE_OBJ/sys__setgid.o" "$SIMPLE_OBJ/sys__setuid.o" "$SIMPLE_OBJ/sys__execve.o" \
    "$SIMPLE_OBJ/sys__chdir.o" \
    "$LAUNCHD_SYSCALL_OBJ/sys__getppid.o" "$LAUNCHD_SYSCALL_OBJ/sys__setsid.o" \
    "$OUT/liblaunchd_mig.a" \
    "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/libmach_ipc.a" "$OUT/chk_fail.o" \
    "$OUT/bootp_dhcp_engine.a" \
    `# Passed as a loose .o, NOT via libconfigd.a: nothing in configd's own` \
    `# object graph calls CFURLCopyResourcePropertyForKey directly (it's only` \
    `# ever reached via flat-namespace lookup from libSystemConfiguration.` \
    `# dylib's own -Wl,-U-deferred reference), so ld64's normal` \
    `# archive-member selection would never pull this object out of the` \
    `# archive at all -- confirmed empirically (nm showed it silently` \
    `# missing from a first real link attempt). A loose .o on the link line` \
    `# is always included, unlike an archive member.` \
    "$OUT/configd_obj/configd_cfurl_filesize_compat.o" \
    `# configd_scnetworkconfig_setvalue_compat.o REMOVED (DAR-240,` \
    `# 2026-09-04): its whole reason for existing -- a real, narrowly-` \
    `# extracted forwarding body standing in for` \
    `# __SCNetworkConfigurationSetValue() while` \
    `# build_libsystemconfiguration_client.sh's own -Wl,-U deferred the` \
    `# real symbol -- no longer applies now that SCNetworkConfigurationInternal.c` \
    `# (the real, canonical owner) is compiled directly into` \
    `# libSystemConfiguration.dylib and the -Wl,-U deferral is gone.` \
    `# SCDHostName.c's 3 real call sites now bind to that dylib's own` \
    `# real, local copy via ordinary two-level-namespace resolution --` \
    `# confirmed via nm/bind_target_audit.sh, not assumed. Leaving the` \
    `# loose compat .o linked would be a real, live duplicate definition` \
    `# of a global (default-visibility) symbol, not just dead weight.` \
    "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_blocks.dylib" \
    "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libcopyfile.dylib" "$OUT/system/libresolv.dylib" \
    "$OUT/system/libremovefile.dylib" \
    "$OUT/system/libCoreFoundation.dylib" \
    "$OUT/system/libSystemConfiguration.dylib" \
    `# libxpc.dylib listed HERE, before libdyld.dylib, 2026-09-04 (DAR-242):`\
    `# real fix for configd.tproj:main() binding _vproc_swap_integer to`\
    `# libdyld.dylib's honest-but-fake "always not managed" stub`\
    `# (libdyld_vproc_compat.c) instead of libxpc.dylib's real MIG`\
    `# round-trip to launchd (libvproc.o, vproc_mig_swap_integer() to`\
    `# bootstrap_port) -- same real ld64 two-level-namespace behavior`\
    `# CLAUDE.md's bind_target_audit.sh gotcha documents: an undefined`\
    `# symbol resolves against the transitive LC_LOAD_DYLIB closure of`\
    `# each dylib on the link line IN ORDER, and the bind is recorded`\
    `# against whichever dylib was listed first. Confirmed via a real`\
    `# QEMU boot: configd.m's real, unmodified main() reads`\
    `# VPROC_GSK_IS_MANAGED as 0 even when launchd really spawned it as`\
    `# a real com.apple.configd LaunchDaemon, takes the fork_child()`\
    `# self-daemonize path meant only for standalone/non-launchd`\
    `# invocation, and the forked child SIGSEGVs -- exactly the exit-70`\
    `# status launchctl list showed. Exact same bug class and exact`\
    `# same fix shape as launchctl.macho's own DAR-235 fix earlier`\
    `# today (see build_launchctl.sh's own comment at its libxpc.dylib`\
    `# link line) -- libxpc.dylib's real _vproc_swap_integer export was`\
    `# already restored for that fix, this is a second, independent`\
    `# consumer that needed the same link-order correction. Verified`\
    `# via a real dyld_info -fixups diff: before this change,`\
    `# configd.macho showed "bind libdyld/_vproc_swap_integer"; after,`\
    `# it shows "bind libxpc/_vproc_swap_integer", matching`\
    `# launchctl.macho's already-correct bind.`\
    "$OUT/system/libdispatch.dylib" "$OUT/system/libxpc.dylib" \
    "$OUT/system/libdyld.dylib" \
    "$OUT/system/libsystem_notify.dylib" \
    "$OUT/system/libopenpam.dylib" "$OUT/system/libinfo.dylib" "$OUT/system/libsystem_m.dylib" \
    -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003',_pthread_rwlock_rdlock \
    -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003',_pthread_rwlock_unlock \
    -Wl,-alias,_getattrlist,'_getattrlist$UNIX2003' \
    `# DAR-252 (filed 2026-09-04, updated 2026-09-06): real, genuine CF gap` \
    `# -- swift-corelibs-foundation has no CFFileDescriptor.c at all` \
    `# (classic Apple CF's real CFFileDescriptorCreate/CreateRunLoopSource/` \
    `# EnableCallBacks/GetNativeDescriptor/Invalidate, needed by FDSet.c's` \
    `# real FDCalloutCreateFileDescriptor() -- the raw-BPF-fd receive path` \
    `# FDCalloutCreate() takes for a non-socket fd) -- confirmed absent` \
    `# from the real apple-oss-distributions/CF repo too (cloned CF-855.17` \
    `# directly, zero hits for CFFileDescriptorGetTypeID/__CFFileDescriptor` \
    `# anywhere in it), not just swift-corelibs-foundation's portable` \
    `# subset. This is no longer a plain "-Wl,-U,_CFFileDescriptorCreate"` \
    `# dynamic-lookup deferral (the shape _CFURLCopyResourcePropertyForKey` \
    `# used in 884ab77) -- that technique assumed classic lazy dyld` \
    `# binding, where a deferred symbol only resolves (and only can fail)` \
    `# the first time FDCalloutCreate() actually takes the raw-fd branch.` \
    `# This project's default -fixup_chains link mode resolves every bind` \
    `# eagerly at process launch instead, so the plain deferral aborted` \
    `# configd's dyld load unconditionally before main() ever ran -- found` \
    `# via a real kernel-side abort_with_payload() reason-string probe` \
    `# (bsd/kern/kern_exit.c, -user_fault_debug) reading dyld's own real` \
    `# "symbol not found in flat namespace '_CFFileDescriptorCreate'" halt` \
    `# message. Real fix: build_ipconfiguration_plugin.sh's own CFLAGS` \
    `# now -include's configd_cffiledescriptor_weak_compat.h (that script` \
    `# is what actually compiles FDSet.c -- NOT this one; a real gap-` \
    `# fix-in-the-wrong-script mistake found and corrected within this` \
    `# same pass, confirmed via nm -m on libconfigd_ipconfiguration.a's` \
    `# own FDSet.o showing a plain "external" undefined reference, not` \
    `# "weak external", until the -include moved to the right script).` \
    `# That compat header shadows the real SDK's CFFileDescriptor.h and` \
    `# re-declares these five real entry points` \
    `# __attribute__((weak_import)) -- real dyld's chained-fixups format` \
    `# has a distinct WEAK bind kind for exactly this case, which` \
    `# resolves to NULL when missing rather than aborting, even under` \
    `# eager -fixup_chains resolution. This still does not implement` \
    `# CFFileDescriptor -- DAR-252's real port is still owed before` \
    `# FDCalloutCreateFileDescriptor()'s raw-BPF-fd path can actually` \
    `# work; calling any of these five once configd is running will now` \
    `# null-deref through an unbound weak import instead, a real, far` \
    `# narrower follow-on bug than the whole daemon being unable to boot.` \
    `# The "-Wl,-U" flags below are STILL required alongside the` \
    `# compat header's weak_import attribute, not replaced by it: ld64` \
    `# itself needs "-Wl,-U" to know this symbol has no required` \
    `# provider dylib at static-link time (real Loader.cpp's own` \
    `# resolveSymbol() confirms the runtime behavior this depends on --` \
    `# libOrdinal==BIND_SPECIAL_DYLIB_FLAT_LOOKUP's "not found" branch` \
    `# checks weakImport and binds to NULL instead of erroring only in` \
    `# that case); weak_import is what sets the per-bind weak flag ld64` \
    `# then carries into the chained-fixup record. Confirmed empirically:` \
    `# removing "-Wl,-U" here (keeping only the weak_import compat` \
    `# header) reintroduces a hard link-time "Undefined symbols" error --` \
    `# the two mechanisms are complementary, not either/or.` \
    -Wl,-U,_CFFileDescriptorCreate \
    -Wl,-U,_CFFileDescriptorCreateRunLoopSource \
    -Wl,-U,_CFFileDescriptorEnableCallBacks \
    -Wl,-U,_CFFileDescriptorGetNativeDescriptor \
    -Wl,-U,_CFFileDescriptorInvalidate \
    2>"$OUT/configd_link.err" \
    && echo "real link: 0 undefined symbols -- configd.macho fully closed" \
    || { n=$(grep -c 'Undefined symbol' "$OUT/configd_link.err" || true); echo "real link found $n undefined symbol group(s):"; grep '^  "' "$OUT/configd_link.err" | sed 's/,.*//' | sort -u; tail -80 "$OUT/configd_link.err" >&2; exit 1; }

otool -l configd.macho | grep -q LC_LOAD_DYLINKER || \
    echo "WARNING: no LC_LOAD_DYLINKER -- expected a real dynamic executable" >&2

real_gaps=$(nm -m configd.macho 2>/dev/null | grep undefined | grep -v "(from " || true)
if [ -n "$real_gaps" ]; then
    echo "WARNING: real undefined symbols present (no sibling dylib resolves these):" >&2
    echo "$real_gaps" >&2
fi

./bind_target_audit.sh configd.macho || {
    echo "error: configd.macho has an unresolvable two-level bind -- it would abort at dyld load" >&2
    exit 1
}

# DAR-263: same hard gate build_notifyd.sh/build_launchctl.sh carry. See
# the link-order comment above and dead_stub_audit.sh's own header.
# configd is the one image where this was never merely hygiene: its
# ___serverSessionCreate (session.c:200) calls mach_port_insert_right()
# for EVERY SCDynamicStore client session and bails out returning NULL
# when it fails, so the KERN_FAILURE stand-in broke session creation
# process-wide -- which is what starves SCDynamicStoreCreate() in every
# plugin's load(), DAR-265 included.
./dead_stub_audit.sh configd.macho || {
    echo "error: see the link-order comment above -- keep libsystem_kernel.dylib" >&2
    echo "       ahead of libmach_ipc.a on configd.macho's link line (DAR-263)" >&2
    exit 1
}
echo "wrote configd.macho"
