#!/bin/bash
# DAR-170 (post-split, IPMonitor/QoSMarking now DAR-239's scope): real
# configd plugin subsystems -- InterfaceNamer, LinkConfiguration,
# KernelEventMonitor, PreferencesMonitor -- all real, unmodified, plain-C
# Apple source from third_party/configd/Plugins/, confirmed (2026-09-04
# session) to be genuinely statically-linked "builtin_plugins[]" entries
# in real Apple's own configd.tproj/plugin_support.c even on iOS/embedded
# (grep of the real, unmodified source: builtin_plugins[] table literally
# lists load_InterfaceNamer/load_KernelEventMonitor/load_LinkConfiguration/
# load_PreferencesMonitor -- IPConfiguration is NOT in that table at all,
# see build_configd_plugins.sh's own IPConfiguration-scoped sibling work
# and the DAR-170 ticket comment for that real, separate finding).
#
# DAR-241 (2026-09-04, same session): KernelEventMonitor/eventmon.c
# compiled clean but failed at LINK time needing ~15 real symbols that
# only exist in IPMonitor's own ip_plugin.c (interfaceListAddInterface,
# link_add, ipv4_router_arp_alive, etc -- real Apple's plugin
# architecture has KernelEventMonitor calling directly into IPMonitor's
# internals by C function call, not just via SCDynamicStore). Root cause:
# Plugins/IPMonitor/ is a MIX -- 6 files are real plain C (ip_plugin.c,
# smb-configuration.c, proxy-configuration.c, dns-configuration.c,
# nat64-configuration.c, set-hostname.c) and only 5 are real ObjC
# (controller.m, proxyAgent.m, agent-monitor.m, dnsAgent.m, configAgent.m
# -- a "NetworkAgent" UI-prompt subsystem layered on top of the core C
# logic, confirmed real @interface usage, DAR-239's scope, untouched
# here). This build now compiles+links 5 of the 6 plain-C files (see
# "REAL FILE LIST" below for why smb-configuration.c is the 6th,
# deliberately excluded one) -- closing the KernelEventMonitor<->
# IPMonitor coupling and letting KernelEventMonitor link in for real.
#
# Produces libc_build/libconfigd_plugins.a. build_configd.sh links this
# archive INSTEAD of relying on configd_builtin_plugins_stub.c's stand-ins
# for these five real symbols (IPMonitor's own five ObjC "Agent" files
# stay a stand-in-only concern, DAR-239's scope, real ObjC wall not
# addressed here -- see DAR-241's own Linear comment for exactly which
# ObjC symbols ip_plugin.c needs and how each was resolved).
#
# ============================================================
# REAL FILE LIST (verified against configd.xcodeproj's own per-plugin
# PBXNativeTarget Sources build phases)
# ============================================================
# - InterfaceNamer: ifnamer.c (one real file, already literally named
#   load_InterfaceNamer() in source -- no wrapper needed).
# - LinkConfiguration: linkconfig.c (ditto, load_LinkConfiguration()).
# - PreferencesMonitor: prefsmon.c (ditto, load_/prime_PreferencesMonitor()).
# - KernelEventMonitor: eventmon.c, ev_dlil.c, ev_ipv4.c, ev_ipv6.c --
#   4 SEPARATE real translation units. CORRECTION (DAR-241, 2026-09-04):
#   this bullet previously (wrongly) claimed eventmon.c textually
#   #includes ev_dlil.c/ev_ipv4.c/ev_ipv6.c unconditionally as one TU.
#   Re-reading the real source found that #include is wrapped in
#   `#ifdef MAIN` (eventmon.c ~line 895) -- a standalone test-harness-only
#   path this project's build never defines -- so those 3 files were
#   NEVER actually being compiled at all, and every real function they
#   define (interfaceListAddInterface, link_add, ipv4_router_arp_alive,
#   ipv6_duplicated_address, nat64_prefix_request, etc -- the exact ~15-
#   symbol gap list from DAR-241's own original filing) was silently
#   absent from eventmon.o. This is why DAR-241 originally (wrongly)
#   attributed these symbols to IPMonitor's ip_plugin.c: they were
#   undefined at the same link attempt IPMonitor was ALSO newly being
#   linked in, and nothing in ip_plugin.c actually defines any of them
#   (confirmed by grep -- zero hits). The real, correct fix: compile
#   ev_dlil.c/ev_ipv4.c/ev_ipv6.c as their own real, independent
#   translation units (exactly matching real Apple's actual per-plugin
#   Xcode Sources-phase membership) -- each already has its own
#   eventmon.h-declared `extern` linkage for eventmon.c's shared globals
#   (`store`, `network_changed`, `_verbose`), so no shared-state gap
#   exists. The `#define appendAddress appendAddress_v4` / `#undef`
#   macro-renaming dance around the MAIN-only includes exists only to
#   let ev_ipv4.c's and ev_ipv6.c's own identically-named `static`
#   helpers coexist when pasted into ONE file for that test harness --
#   irrelevant once they're compiled as separate objects (each file's
#   `static` functions already have their own independent scope). See
#   DAR-241's own Linear comment for the full real link-error evidence
#   this correction is based on. ev_extra.m
#   (a 5th, real, Objective-C-*named*-but-zero-actual-ObjC-syntax file --
#   confirmed by grep, no @interface/@implementation/message-send anywhere)
#   is real but DELIBERATELY EXCLUDED: its is_expensive() helper needs the
#   SCNetworkInterface family (_SCNetworkInterfaceIsTethered() etc), which
#   build_libsystemconfiguration_client.sh already deliberately excluded
#   from libSystemConfiguration.dylib as a separate, substantial subsystem.
#   The one real symbol ev_dlil.c calls from it, interface_update_expensive(),
#   gets a narrow, functionally-honest stand-in
#   (configd_plugin_expensive_compat.c) instead -- see that file's own
#   header comment for why `return NULL` is the REAL correct answer for
#   this project's only real interface (en0, plain Ethernet, never
#   tethered/BT-PAN/WWAN), not a functional shortcut.
# - IPMonitor: ip_plugin.c, proxy-configuration.c, dns-configuration.c,
#   nat64-configuration.c, set-hostname.c -- 5 of the real 6 plain-C
#   files (real ObjC files controller.m/proxyAgent.m/agent-monitor.m/
#   dnsAgent.m/configAgent.m are DAR-239's scope, untouched here).
#   smb-configuration.c is the 6th plain-C file and is DELIBERATELY
#   EXCLUDED -- not a compat-header gap but a real, exact match of
#   Apple's own conditional compilation: ip_plugin.c's own only call
#   site, `load_smb_configuration(TRUE)` (~line 9279), is itself wrapped
#   in `#if !TARGET_OS_IPHONE`, and every kSCEntNetSMB/kSCPropNetSMB*/
#   kSCValNetSMBNetBIOSNodeType* symbol smb-configuration.c uses is
#   independently marked `API_UNAVAILABLE(ios, tvos, watchos, bridgeos)`
#   in real, pinned SCSchemaDefinitions.h -- i.e. real Apple's own SMB
#   File Sharing config sync is a macOS-only feature that was never
#   compiled for an iOS/embedded target in the first place. Since this
#   build already targets `arm64-apple-ios14.4`, simply not compiling
#   smb-configuration.c matches real Apple's own build exactly (nothing
#   else calls its one real entry point unconditionally).
#
# ============================================================
# REAL GAPS FOUND AND CLOSED, one file/header at a time
# ============================================================
# - <IOKit/IOKitLibPrivate.h>, <IOKit/usb/USB.h>: real, already-pinned
#   headers (IOKitUser, IOUSBFamily) just missing the IOKit/ path prefix
#   their real source tree doesn't itself provide -- symlinked into a
#   staged configd_plugin_compat/IOKit/ dir, same technique
#   build_libsystemconfiguration_client.sh already established for
#   IOKit/IOKitLib.h. <IOKit/IOKitKeysPrivate.h>/<IOKit/IOBSD.h>/
#   <IOKit/IOMessage.h>/<IOKit/network/*.h> are already real, pinned,
#   reachable via -I "$XNU/iokit" (xnu-7195/iokit/IOKit/...), no staging
#   needed.
# - <lockdown.h> + <SoftLinking/WeakLinking.h>: real, genuinely
#   never-open-sourced Apple-internal headers (confirmed via full
#   pinned-repo grep -- MobileLockdown isn't on apple-oss-distributions,
#   SoftLinking is an internal build-system umbrella). Narrow, honestly
#   documented compat stand-ins (configd_plugin_lockdown_compat.h,
#   configd_plugin_compat/SoftLinking/WeakLinking.h) -- real, load-bearing
#   fact confirmed by reading ifnamer.c directly: every real caller of
#   these symbols is gated behind haveLockdown() (ifnamer.c ~line 2399),
#   itself only consulted at ifnamer.c:3384, guarding a whole "Trust
#   Required" (iOS host-pairing) subsystem this Pi3/Ethernet target
#   genuinely has no equivalent for. Weak-importing these symbols with no
#   definition anywhere makes haveLockdown() correctly, honestly return
#   FALSE -- real Apple's own designed graceful-degradation path (the same
#   one real Mac hardware without MobileLockdown already relies on), not a
#   functional shortcut.
# - <nw/private.h>: real, never-open-sourced libnetcore/Network.framework
#   internal header. Originally an empty stand-in (eventmon.c genuinely
#   never references any nw_*/NW_* symbol). DAR-241 (2026-09-04) found
#   ip_plugin.c/nat64-configuration.c DO need real content from it:
#   opaque nw_endpoint_t/nw_parameters_t/nw_path_*/nw_resolver_*/
#   nw_array_t typedefs (SCNetworkReachabilityInternal.h struct fields
#   ip_plugin.c never reads/writes, just needs to exist so the struct is
#   well-formed), plus a real nw_nat64_prefix_t layout + the handful of
#   nw_nat64_prefixes_resolver_*/nw_interface_*/nw_parameters_* NAT64-
#   PLAT-prefix-discovery declarations nat64-configuration.c calls --
#   defined (as real, narrow no-ops, see configd_plugin_nat64_nw_compat.c's
#   own header comment) because RFC 7050 NAT64/DNS64 PLAT-prefix
#   discovery only matters on an IPv6-only network reaching IPv4-only
#   destinations, which does not describe this project's real target
#   (single Ethernet interface, real IPv4 DHCP, DAR-170). The real,
#   already-present kernel-facing half of nat64-configuration.c
#   (SIOCSIFNAT64PREFIX ioctl in xnu-7195's bsd/net/if.h) is untouched.
# - <dns_sd_private.h>: real, narrow compat stand-in
#   (configd_plugin_compat/dns_sd_private.h) for exactly the two real
#   mDNSResponder string constants ip_plugin.c uses
#   (kDNSServiceCompMulticastDNS/kDNSServiceCompPrivateDNS, as
#   SCDynamicStore key path components) -- mDNSResponder itself isn't on
#   this project's apple-oss-catalog pin list, values match mDNSResponder's
#   real, publicly documented header content.
# - <ppp/PPPControllerPriv.h>, <ppp/ppp_msg.h>: real, pinned-tag headers
#   (apple-oss-distributions/ppp at ppp-877.40.2, confirmed against
#   distribution-macOS's macos-112 release.json -- the exact tag this
#   project's other pins are matched against) staged into
#   configd_plugin_compat/ppp/ -- ip_plugin.c uses these for real
#   PPP_RUNNING/PPP_IDLE/PPP_STATERESERVED/IPSEC_RUNNING/IPSEC_IDLE
#   status-enum comparisons (SCNetworkReachability flag computation for
#   PPP/IPSec service entities).
# - `sa_dst_compare_no_dependencies`: NOT a stub -- a real forwarding
#   shim (configd_plugin_compat/sa_dst_compare_compat.h, -include'd) to
#   Libinfo's `si_destination_compare_no_dependencies` (lookup.subproj/
#   si_compare.c, already compiled into libinfo.dylib by
#   build_libinfo.sh). Same real RFC 6724 rules 6/8/10 algorithm, just
#   renamed between this project's pinned configd tag (old name) and
#   pinned Libinfo tag (new name) -- confirmed by grep of both repos,
#   not a functional gap.
# - VPN_RUNNING/VPN_IDLE/VPN_LOADING/VPN_LOADED/VPN_UNLOADING: real,
#   narrow compat stand-in (configd_plugin_compat/vpn_status_compat.h,
#   -include'd) for a VPN-controller status enum confirmed, by real
#   `git ls-remote` + full grep of ppp/IPSec/network_cmds, to be
#   genuinely never-open-sourced anywhere on apple-oss-distributions --
#   same category as this file's own lockdown.h stand-in. Only consulted
#   inside ip_plugin.c's `CFEqual(entity, kSCEntNetVPN)` branch, which
#   requires a real VPN configuration profile this project has no
#   equivalent of at all (single Ethernet interface, no VPN subsystem
#   ported) -- genuinely dead code for this target, not a DHCP/network-
#   config-relevant shortcut.
# - <netdb_async.h>: real, already-pinned Libinfo header
#   (lookup.subproj/netdb_async.h) just missing an -I path.
# - <FSEvents/FSEvents.h>: real, never-open-sourced CoreServices/
#   CarbonCore framework (confirmed by real `git ls-remote` against
#   FSEvents/fseventsd/CarbonCore/CoreServices, all 404). Narrow compat
#   stand-in (configd_plugin_compat/FSEvents/FSEvents.h) makes
#   dns-configuration.c's /etc/resolvers live-reload watch a real no-op
#   -- the actual DNS-configuration read/apply path
#   (dns_configuration_set()) does not depend on this watch and already
#   re-reads /etc/resolvers on every ordinary network/service change;
#   only hand-editing /etc/resolvers/* files with configd already
#   running and no other network change also occurring would miss an
#   auto-refresh, and this project's minimal userland ships no
#   /etc/resolvers directory at all.
# - <smb_server_prefs.h>: real, narrow compat stand-in
#   (configd_plugin_compat/smb_server_prefs.h) -- NOT used by the build
#   (smb-configuration.c itself is excluded, see "REAL FILE LIST" above)
#   but kept for documentation/future reference of the real, confirmed
#   kSMBPref* string values (from apple-oss-distributions/SMBClient's
#   lib/smb/smb_preferences.h, which reuses the server's own preference
#   key names) in case smb-configuration.c is ever revisited for a real
#   macOS (non-embedded) build target.
# - `_sethostname` (set-hostname.c): real, unmodified Apple/FreeBSD
#   source (Libc/gen/FreeBSD/sethostname.c, a plain sysctl(CTL_KERN,
#   KERN_HOSTNAME) wrapper) compiled DIRECTLY into libconfigd_plugins.a
#   here rather than added to libsystem_c.dylib's own build
#   (build_libsystem_c.sh), even though real Apple ships it there.
#   Reason: build_libsystem_c.sh's SIGLIST_CFLAGS also (re)compiles
#   gen/FreeBSD/arc4random.c, which was found DURING this same session to
#   already independently fail (`os/assumes.h` -> `_simple.h` ->
#   `os/base_private.h` -> real mach type gaps, `kern_return_t`/
#   `mach_port_status_t` -- confirmed pre-existing and unrelated to this
#   ticket by reverting to a clean checkout and re-running both
#   build_libsystem_c.sh AND build_libresolv.sh, which independently
#   compiles the same real arc4random.c file and fails the exact same
#   way). Not this ticket's scope to fix (a real, separate regression
#   worth its own Linear ticket) -- compiling sethostname.c here instead
#   avoids depending on that already-broken script without papering over
#   the real bug or silently working around it elsewhere.
# - `CC_SHA256_Init/Update/Final` (ip_plugin.c's get_signature_sha256()/
#   _nwi_state_compute_sha256_hash(), both internal change-detection
#   fingerprints, never DHCP/security-relevant): real corecrypto (real
#   Apple CommonCrypto's own actual backing) is present as source in
#   this project's pinned xnu-7195 (osfmk/corecrypto/ccsha256_*.c) but
#   wiring up its generic, kernel-shaped ccdigest/ccmode plumbing
#   standalone for a userland CommonCrypto-API shim is a substantial
#   side project -- same tradeoff this project's own kernel-side crypto
#   work already made ("the from-scratch SHA-256/384/512 replacing the
#   corecrypto-kext-dependent implementation") and md5_commoncrypto_
#   shim.c's own md5_rfc1321_core.c sibling ("from-scratch but already
#   kernel-proven") already established for MD5. configd_plugin_sha256_
#   compat.c is the same decision applied to SHA-256: a plain, from-
#   scratch FIPS 180-4 implementation, verified against the standard's
#   own published test vectors (empty string, "abc", the standard's
#   two-block test string, and the 1,000,000-'a' extended test vector --
#   all four confirmed byte-for-byte correct via a native host build
#   before wiring into this cross-compiled build).
#
# ============================================================
# REAL SUBSYSTEM PORTS closing the final configd.macho LINK gaps
# (DAR-241, 2026-09-04, found only after ip_plugin.c/eventmon.c's own
# direct symbol coupling was resolved -- these are a SECOND, later wave
# of real gaps at full-executable link time, not compile time)
# ============================================================
# - nwi/network_information.c (client accessor library --
#   nwi_ifstate_get_*/nwi_state_get_* -- ip_plugin.c reads back its own
#   locally-built nwi_state through this same real API it writes with)
#   and nwi/network_information_server.c (load_NetworkInformation(),
#   the real nwi shared-memory-state SERVER registration
#   __nwi_state_store() calls into): both real, unmodified, compile
#   clean once config-agent-info/config_agent_info.h (below) and
#   libSystemConfiguration/libSystemConfiguration_client.h are on the
#   include path.
# - dnsinfo/dnsinfo_server.c (load_DNSConfiguration()),
#   dnsinfo/dnsinfo_create.c (the real __dns_resolver_set_* family
#   dns-configuration.c's create_resolver() calls), and
#   dnsinfo/dnsinfo_flatfile.c (__dnsinfo_flatfile_add_resolvers/
#   set_flags, dns_configuration_set()'s real /etc/resolv.conf-writing
#   path): all three real, unmodified, compile clean with the same new
#   include paths above.
# - IPMonitorControl/IPMonitorControlServer.c
#   (IPMonitorControlServerStart/CopyInterfaceRankInformation) and its
#   real sibling Plugins/common/IPMonitorControlPrefs.c
#   (IPMonitorControlPrefsInit/IsVerbose) -- both real, unmodified. The
#   AWD (Apple Wireless Diagnostics) telemetry sub-call
#   (IPMonitorAWDReport.h) is real plain-C-DECLARED but ObjC-IMPLEMENTED
#   (IPMonitorAWDReport.m, DAR-239's scope) -- same shape of problem as
#   ip_plugin.c's own Agent coupling, resolved the same way: see
#   configd_plugin_awd_compat.c's own header for why
#   InterfaceAdvisoryReportCreate() returning NULL is real Apple's own
#   behavior when WirelessDiagnostics.framework/objc4 aren't present,
#   not a fabrication (pure one-way analytics telemetry, zero effect on
#   actual network/DHCP/DNS behavior). AWDIPMonitorGlobalEnums.h is
#   real, protocompiler-generated, already-pinned source
#   (IPMonitorControl/AWD/) -- just needed its own -I path.
# - config-agent-info/config_agent_info.h: real, self-contained header
#   (kConfigAgentTypeDNS/kConfigAgentTypeProxy string constants + a
#   handful of function prototypes nwi's real files declare but never
#   call) -- just needed its own -I path, no compat content required.
# - SystemConfiguration.fproj/scprefs_observer.c (real
#   scprefs_observer_watch(), an SCPreferences-plist-change dispatch-
#   source watcher dns-configuration.c/ip_plugin.c use) and
#   SystemConfiguration.fproj/DHCP.c (real DHCPInfoGetOptionData(),
#   reading a raw DHCP option byte range out of a lease's dynamic-store
#   dictionary -- genuinely DHCP-relevant, real, unmodified, already
#   compiles clean with the existing include set): both real,
#   unmodified, already compile clean, previously simply never added to
#   this build's FILES list.
# - <os/state_private.h>: real, never-open-sourced header (already a
#   narrow compat stand-in from an earlier DAR-170 pass, see
#   build_libsystemconfiguration_client.sh's own header comment) --
#   EXTENDED here (not re-derived) because dnsinfo_server.c's and
#   network_information_server.c's own real add_state_handler()
#   functions need the OS_STATE_DATA_CUSTOM variant + osd_decoder field
#   this compat header didn't have yet. Regenerate via
#   build_libsystemconfiguration_client.sh, not by hand-editing the
#   gitignored generated copy under libc_build/compat_include/.
# - `_process_AgentMonitor`/`_process_AgentMonitor_DNS`/
#   `_process_AgentMonitor_Proxy`, and the 5 real
#   `SCNetworkReachability*` functions set-hostname.c calls: both narrow,
#   honestly-documented no-op stand-ins matching a real, already-existing
#   Apple graceful-degradation path in each real caller -- see
#   configd_plugin_agentmonitor_compat.c's and configd_plugin_
#   reachability_compat.c's own header comments for the full real-
#   behavior justification (in short: agent-monitor.m's real
#   haveNetworkExtensionFramework() gate and set-hostname.c's own real
#   "PTR lookup didn't start, fall through to mDNS/localhost" fallback
#   chain, neither touching the real DHCP-provided-hostname path). Also
#   covers `copy_dns_information_for_agent_uuid`/`copy_proxy_information_
#   for_agent_uuid` (network_information_server.c's own real caller,
#   same agent-monitor.m gate) and `SCNetworkReachabilityGetFlags`
#   (dnsinfo_create.c, real but unreachable -- see that compat file's
#   own comment).
# - libSystemConfiguration/libSystemConfiguration_client.c and
#   libSystemConfiguration_server.c: real, unmodified Apple source,
#   compiled a SECOND time here (already compiled once into
#   libSystemConfiguration.dylib by build_libsystemconfiguration_client.sh)
#   -- confirmed this is the CORRECT real shape, not a duplicate-global-
#   state bug: every symbol these two files export
#   (libSC_info_client_create/release, libSC_info_available,
#   libSC_send_message_with_reply_sync, libSC_info_server_init/open/
#   close/get_data/set_data/in_sync/acknowledged) is real, unmodified
#   Apple `__private_extern__` (hidden visibility) -- by design, meant
#   for each real linkage unit (dylib vs. this project's configd.macho)
#   to carry its OWN private copy, not shared across a dylib boundary.
#   The one piece of file-scope state (`static boolean_t _available`,
#   client.c) is a simple, independently-reinitialized-per-image flag,
#   not a cross-image singleton -- confirmed by reading the whole file,
#   not assumed. nwi/network_information.c (client accessor) and
#   dnsinfo/dnsinfo_server.c + nwi/network_information_server.c (server
#   registration) all call directly into this transport layer, exactly
#   matching how real Apple's own configd.tproj must link its own copy
#   for the same reason.
# - `___log_SCPreferences`: real, verbatim (not reinvented) copy of
#   SystemConfiguration.fproj/SCPOpen.c's own `__private_extern__`
#   os_log_create() handle accessor (configd_plugin_log_scpreferences_
#   compat.c) -- same "hidden visibility, each linkage unit gets its own
#   copy" reasoning as the client/server transport files above,
#   scprefs_observer.c's own real SC_LOG_HANDLE macro expects exactly
#   this name.
set -e
cd "$(dirname "$0")"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi

CONFIGD="../../third_party/configd"
SC="$CONFIGD/SystemConfiguration.fproj"
XNU="../../third_party/xnu-7195"
LIBC="../../third_party/Libc"
LIBINFO="../../third_party/Libinfo"
PTHREAD="../../third_party/libpthread"
PLATFORM="../../third_party/libplatform"
DISPATCH="../../third_party/libdispatch"
PD="../../third_party/PureDarwin/src/Libraries/XPC"
IOKITUSER="../../third_party/IOKitUser"
IOUSBFAMILY="../../third_party/IOUSBFamily/IOUSBFamily/Headers"
OUT="libc_build"
rm -rf "$OUT/configd_plugins_obj"
mkdir -p "$OUT/configd_plugins_obj"

for d in sc_headers sc_iokit_headers sc_dispatch_compat sc_compat_include compat_include; do
    if [ ! -d "$OUT/$d" ]; then
        echo "error: $OUT/$d not found -- run build_libsystemconfiguration_client.sh first" >&2
        exit 1
    fi
done

# Real, staged IOKit/ header symlinks -- regenerated fresh every run for
# reproducibility (per repo convention: no manual/ad-hoc staging).
rm -rf "$OUT/configd_plugin_iokit_headers"
mkdir -p "$OUT/configd_plugin_iokit_headers/IOKit/usb"
ln -sf "$(pwd)/$IOKITUSER/IOKitLibPrivate.h" "$OUT/configd_plugin_iokit_headers/IOKit/IOKitLibPrivate.h"
ln -sf "$(pwd)/$IOUSBFAMILY/USB.h" "$OUT/configd_plugin_iokit_headers/IOKit/usb/USB.h"

CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__va_list=__builtin_va_list \
    -D__API_UNAVAILABLE_PLATFORM_bridgeos=bridgeos,unavailable \
    -D__API_AVAILABLE_PLATFORM_bridgeos\(x\)=ios,introduced=x \
    -DPRIVATE \
    `# -idirafter "$LIBC" below (2026-09-14): needed for the real` \
    `# third_party/Libc/os/variant_private.h forwarding shim` \
    `# (libc_build/compat_include/os/variant_private.h's own` \
    `# #include_next, see build_libsystemconfiguration_client.sh's header` \
    `# for the real shared-stub-collision fix this replaced) to have` \
    `# somewhere to actually resolve to -- was missing here entirely.` \
    -include "sys/fileport.h" \
    -include "network_config_check_compat.h" \
    -include "IOKit/usb/USBSpec_ifnamer_compat.h" \
    -include "sa_dst_compare_compat.h" \
    -include "vpn_status_compat.h" \
    -I "configd_plugin_compat" \
    -I "$OUT/configd_plugin_iokit_headers" \
    -I "$PD/libxpc/include" \
    -I "launchd_compat_headers" \
    -I "$OUT/sc_compat_include" -I "$OUT/compat_include" -I "$OUT/cf_headers" \
    -I "$SC" -I "$SC/helper" -I "$CONFIGD/IPMonitorControl" -I "$CONFIGD/IPMonitorControl/AWD" \
    -I "$CONFIGD/Plugins/common" \
    -I "$CONFIGD/Plugins/InterfaceNamer" -I "$CONFIGD/Plugins/LinkConfiguration" \
    -I "$CONFIGD/Plugins/PreferencesMonitor" -I "$CONFIGD/Plugins/KernelEventMonitor" \
    -I "$CONFIGD/Plugins/IPMonitor" \
    -I "$CONFIGD/dnsinfo" -I "$CONFIGD/nwi" -I "$CONFIGD/libSystemConfiguration" -I "$CONFIGD/config-agent-info" \
    -I "$LIBINFO/lookup.subproj" \
    -I "$OUT/sc_headers" -I "$OUT/sc_iokit_headers" -I "$XNU/iokit" \
    -I "$PD/launchd/System-compat" \
    -I "$OUT/sc_dispatch_compat" -I "$DISPATCH" -I "$DISPATCH/private" \
    -I "$LIBC/include" -I "$LIBC/darwin" -I "$LIBC/fbsdcompat" -I "$LIBC/locale" -I "$LIBC/locale/FreeBSD" \
    -I "$LIBC/stdtime/FreeBSD" \
    -I "$PTHREAD/include" -I "$PTHREAD/private" \
    -I "$PLATFORM/private" -I "$PLATFORM/include" \
    -I ../../generated \
    -I "$XNU/libsyscall/mach" -I "$XNU/libsyscall" -I "$XNU/osfmk" -I "$XNU/bsd" \
    -idirafter "$LIBC")

FILES="$CONFIGD/Plugins/InterfaceNamer/ifnamer.c \
    $CONFIGD/Plugins/LinkConfiguration/linkconfig.c \
    $CONFIGD/Plugins/PreferencesMonitor/prefsmon.c \
    $CONFIGD/Plugins/KernelEventMonitor/eventmon.c \
    $CONFIGD/Plugins/KernelEventMonitor/ev_dlil.c \
    $CONFIGD/Plugins/KernelEventMonitor/ev_ipv4.c \
    $CONFIGD/Plugins/KernelEventMonitor/ev_ipv6.c \
    $CONFIGD/Plugins/IPMonitor/ip_plugin.c \
    $CONFIGD/Plugins/IPMonitor/proxy-configuration.c \
    $CONFIGD/Plugins/IPMonitor/dns-configuration.c \
    $CONFIGD/Plugins/IPMonitor/nat64-configuration.c \
    $CONFIGD/Plugins/IPMonitor/set-hostname.c \
    $LIBC/gen/FreeBSD/sethostname.c \
    $CONFIGD/nwi/network_information.c \
    $CONFIGD/nwi/network_information_server.c \
    $CONFIGD/dnsinfo/dnsinfo_server.c \
    $CONFIGD/dnsinfo/dnsinfo_create.c \
    $CONFIGD/dnsinfo/dnsinfo_flatfile.c \
    $CONFIGD/IPMonitorControl/IPMonitorControlServer.c \
    $CONFIGD/Plugins/common/IPMonitorControlPrefs.c \
    $SC/scprefs_observer.c \
    $SC/DHCP.c \
    $CONFIGD/libSystemConfiguration/libSystemConfiguration_client.c \
    $CONFIGD/libSystemConfiguration/libSystemConfiguration_server.c \
    configd_plugin_expensive_compat.c \
    configd_plugin_network_config_compat.c \
    configd_plugin_nat64_nw_compat.c \
    configd_plugin_sha256_compat.c \
    configd_plugin_agentmonitor_compat.c \
    configd_plugin_reachability_compat.c \
    configd_plugin_awd_compat.c \
    configd_plugin_log_scpreferences_compat.c"

ok=0
fail=0
for f in $FILES; do
    base=$(basename "$f" .c)
    out="$OUT/configd_plugins_obj/${base}.o"
    if "$CLANG" "${CFLAGS[@]}" "$f" -o "$out" 2>"$OUT/configd_plugins_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/configd_plugins_obj/${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OUT/configd_plugins_obj/${base}.err")"
    fi
done
echo "configd_plugins: $ok compiled, $fail gaps (see $OUT/configd_plugins_obj/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi

rm -f "$OUT/libconfigd_plugins.a"
ar rcs "$OUT/libconfigd_plugins.a" "$OUT"/configd_plugins_obj/*.o
echo "wrote $OUT/libconfigd_plugins.a ($(ls "$OUT"/configd_plugins_obj/*.o | wc -l | tr -d ' ') objects)"
