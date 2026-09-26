/* Real, honest "not yet ported" stand-in for configd.tproj's real
 * `builtin_plugins[]` static table (plugin_support.c, guarded by real
 * `#if !TARGET_OS_IPHONE` -- confirmed by reading the real, unmodified
 * source: on iOS/embedded targets, real Apple configd does NOT dlopen
 * separate .bundle plugins at all, it links a curated set of plugin
 * subsystems DIRECTLY into the daemon binary, each exposing real,
 * fixed-name `load_<Name>`/`prime_<Name>` entry points matching
 * SystemConfiguration.fproj/SCDPlugin.h's real
 * SCDynamicStoreBundleLoadFunction/PrimeFunction typedefs.
 *
 * DAR-170 Tier 1 step 3 ported the real SCDynamicStore SERVER ENGINE
 * (configd.tproj's own 21 real Sources-phase .c/.m files).
 *
 * DAR-170's 2026-09-04 post-split session (ticket narrowed to the non-ObjC
 * plugins, IPMonitor/QoSMarking moved to DAR-239) got the real, plain-C
 * InterfaceNamer/LinkConfiguration/KernelEventMonitor/PreferencesMonitor
 * source files themselves compiling clean (see build_configd_plugins.sh,
 * which produces libc_build/libconfigd_plugins.a) but found two real,
 * hard architectural walls at the LINK stage.
 *
 * DAR-241 (same day, follow-up) closed wall #2 for real: IPMonitor's
 * ip_plugin.c (and 4 of its plain-C siblings -- proxy-configuration.c,
 * dns-configuration.c, nat64-configuration.c, set-hostname.c; a 6th,
 * smb-configuration.c, is deliberately excluded, real Apple's own
 * `#if !TARGET_OS_IPHONE` gate -- see build_configd_plugins.sh's own
 * comment) now compile+link clean into libconfigd_plugins.a alongside
 * eventmon.c. build_configd.sh now links libconfigd_plugins.a as a
 * whole archive AFTER this file's own object -- real ld64 archive
 * member selection means only the members that resolve a symbol still
 * outstanding at that point in the link get pulled in.
 *
 * DAR-240 (same day, follow-up) closed wall #1 for real, mostly: the real
 * SCNetworkConfiguration/SCNetworkSet/SCNetworkService/SCBridgeInterface/
 * SCVLANInterface family InterfaceNamer/ifnamer.c, LinkConfiguration/
 * linkconfig.c, and PreferencesMonitor/prefsmon.c all genuinely, heavily
 * call into (SCNetworkSetCopyCurrent, SCNetworkServiceCopyAll,
 * _SCNetworkInterfaceCreateCapabilities, _SCNetworkConfigurationUpgrade,
 * etc -- DAR-240's own ~25 real symbols, confirmed at real link time) is
 * now really ported into libSystemConfiguration.dylib
 * (build_libsystemconfiguration_client.sh: SCNetworkConfigurationInternal.c,
 * SCNetworkConfigurationPrivate.c, SCNetworkInterface.c, SCNetworkService.c,
 * SCNetworkSet.c, SCNetworkProtocol.c, SCPreferencesPathKey.c,
 * BridgeConfiguration.c, VLANConfiguration.c, LinkConfiguration.c -- real
 * file-name corrections vs. DAR-240's own filed guesses, see that
 * script's own FILES-list comment). `_SCNetworkConfigurationUpgrade`'s
 * real, load-time-critical call from `load_PreferencesMonitor()` itself
 * is now backed by a real, defined symbol
 * (SCNetworkConfigurationPrivate.c), not deferred.
 *
 * This file's load_LinkConfiguration/load_PreferencesMonitor/
 * prime_PreferencesMonitor stand-ins are REMOVED (below) for the same
 * real reason DAR-241 already removed load_IPMonitor/prime_IPMonitor/
 * load_KernelEventMonitor/prime_KernelEventMonitor: with no earlier-on-
 * the-link-line definition to shadow them, ld64's real archive-member
 * selection now pulls in libconfigd_plugins.a's own real
 * linkconfig.o/prefsmon.o (and their real object-file dependents) to
 * satisfy those 3 symbols instead -- confirmed via a real, clean,
 * 0-undefined-symbol link.
 *
 * load_InterfaceNamer stays a stand-in: DAR-240's real fix unblocked
 * ifnamer.o's own SCNetworkConfiguration-family symbols, but real linking
 * then surfaced a SEPARATE, genuinely distinct real wall -- ifnamer.c
 * also calls the real, high-level IOKitLib.h API (IOServiceGetMatchingService,
 * IORegistryEntryCreateCFProperty, IONotificationPortCreate, etc, ~28
 * symbols) plus a real lockdown.h/haveLockdown() notification family (5
 * symbols) neither of which any of DAR-240's other two plugins reference
 * at all (confirmed: the real link's own undefined-symbol report cited
 * ifnamer.o as the sole referencing object for every one of these). Real,
 * already-known, pre-existing wall: this project's own
 * build_iokituser.sh has documented since its own first pass that
 * IOKitLib.c itself (the real userland IOKitLib.framework implementation
 * these symbols come from) hard-depends on CoreFoundation/libdispatch/
 * libxpc and could not be built at that time; CF/dispatch/xpc dylibs do
 * exist now (this project's later work), but a real, timeboxed retest
 * this session found IOKitLib.c still does NOT compile clean even with
 * them available (hit a real missing-header gap + a real macro-expansion
 * syntax error in dispatch/source_private.h within the first two
 * iterations) -- a real, still-substantial, multi-gap port, not a quick
 * follow-on fix, and NOT part of DAR-240's own SCNetworkConfiguration-
 * family scope. Filed separately (see DAR-240's own Linear comment for
 * the exact new ticket ID) rather than guessed at or worked around here.
 *
 * QoSMarking (load_QoSMarking, no prime) remains a stand-in -- that's
 * real ObjC, DAR-239's scope, untouched.
 *
 * The two remaining stand-ins below are real, minimal, functionally-
 * honest implementations of the exact real SCDynamicStoreBundleLoadFunction
 * signature (SCDPlugin.h) plugin_support.c itself calls -- they log via
 * the real SC_log()/os_log path that they were never really loaded, then
 * return, so configd.tproj's own real bundle bookkeeping (allBundles,
 * exiting, etc) stays exactly as correct as it would be for a real
 * bundle whose load() function does nothing yet.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SCDPlugin.h>
#include <SystemConfiguration/SCPrivate.h>

/* DAR-266 investigation note (reverted): an earlier version of this file
 * forced real Apple SCD.c's own `_sc_debug` global on via a constructor, to
 * get plugin_support.c's LOG_DEBUG-level "calling load()/start()/prime()
 * for %@" trace to print. That trace is gated behind the SC_log() macro's
 * own `if (__SC_log_enabled(...))` check (SCPrivate.h:497-514), which means
 * the %@ argument expression -- CFBundleGetIdentifier(bundleInfo->bundle)
 * in plugin_support.c, CFBundleGetIdentifier(bundle) in eventmon.c's own
 * load_KernelEventMonitor() -- is normally never even EVALUATED (C's
 * short-circuit if, not lazy varargs) when `_sc_debug` is FALSE, which it
 * always is in real configd.m (no command-line flag there ever sets it;
 * only scutil/scselect/dnsinfo_server.c/network_information_server.c/
 * dns-configuration.c's own internal debug paths do, none of which run in
 * configd's own startup).
 *
 * Forcing it on made THOSE calls execute for the first time ever on this
 * port, and hit a real, separate, previously-unreached bug: eventmon.c:834's
 * second CFBundleGetIdentifier(bundle) call (immediately after
 * plugin_support.c's callLoadFunction already called it once successfully
 * on the same bundle one line earlier, populating bundle->_infoDict for the
 * first time via CFBundleGetInfoDictionary's _CFBundleRefreshInfo
 * DictionaryAlreadyLocked() -> _CFBundleInfoPlistFixupInfoDictionary() path)
 * crashes inside __CFLock(&bundle->_lock) -> pthread_mutex_lock() with the
 * mutex pointer reading as a small near-NULL offset (0xd8, matching struct
 * __CFBundle's real _lock field offset from a NULL base) -- consistent
 * with the Info.plist fixup path corrupting the bundle object on its first,
 * lazy-populate run. That bug is real (filed as a follow-up, see DAR-266's
 * Linear comment) but is NOT DAR-266 itself: it is unreachable in every
 * stock build (nothing else here ever sets `_sc_debug`), so forcing it on
 * was actively counterproductive for finding DAR-266's real pc=0/lr=0
 * fault -- it made configd crash EARLIER, on a different, CF-related bug,
 * before ever reaching whatever DAR-266's real crash site is. Reverted;
 * DAR-266's own investigation used CF-free fprintf(stderr, ...) markers
 * (plugin_support.c) instead, which don't evaluate any new CF call.
 *
 * DAR-267 follow-up (2026-09-17). Investigated the "eventmon.c:834 ->
 * CFBundleGetIdentifier() -> __CFLock(&bundle->_lock) -> pthread_mutex_lock()
 * on a near-NULL pointer" crash noted above. Static/control-flow analysis
 * against this project's real, pinned sources:
 *
 *   RULED OUT (each independently traced/verified, not guessed):
 *   - Struct layout mismatch: the DAR-266 note's own address arithmetic
 *     (0xd8 matches struct __CFBundle's real `_lock` field offset,
 *     CFBundle_Internal.h:78-140) already confirms this project's struct
 *     layout is correct -- the crash address decodes as (NULL base + real
 *     offset), not a wrong offset.
 *   - Bundle retain/release imbalance from plugin_support.c's own
 *     ALT_CFRelease(bundle) "over-retain correction" (plugin_support.c
 *     line ~938, comment cites <rdar://problems/4912137&6078752>): traced
 *     the real refcount arithmetic through this project's own
 *     CFBundleCreateBundlesFromDirectory() (swift-corelibs-foundation
 *     CFBundle.c:796-819) -- it really does leave bundles it returns with
 *     one extra, undropped retain (CFBundleCreate()'s own +1, plus the
 *     array's +1 from CFArrayAppendValue, with the `#ifdef
 *     __clang_analyzer__`-only CFRelease() never compiled in for a real
 *     build) -- i.e. this project's CFBundle really does reproduce Apple's
 *     documented over-retain bug, so ALT_CFRelease() is a correct
 *     correction here too, not an over-release. Also: CFBundle.c's global
 *     `_allBundles` table (line ~126-243) permanently retains every real
 *     bundle for the life of the process ("the existence of
 *     CFBundleGetBundleWithIdentifier/CFBundleGetAllBundles means any
 *     bundle we hand out must be permanently retained" -- CFBundle.c's own
 *     comment), so a real created-from-disk bundle is not freed at all
 *     during configd's run.
 *   - CFLock_t zero-init: CFLocking.h's TARGET_OS_MAC branch (the one this
 *     project's iPhoneOS-SDK target actually compiles) makes CFLock_t a
 *     real `pthread_mutex_t`, not a plain zeroable int -- but
 *     _CFBundleCreate() (CFBundle.c:741) does explicitly
 *     `bundle->_lock = CFLockInit;` (`= PTHREAD_ERRORCHECK_MUTEX_INITIALIZER`)
 *     for every real bundle it creates, so the lock is genuinely
 *     initialized, not relying on zero-fill.
 *   - Control flow: plugin_support.c's own loadBundle() (line 351) calls
 *     `CFBundleGetIdentifier(bundleInfo->bundle)` UNCONDITIONALLY (not
 *     gated by any SC_log/_sc_debug check) for every bundle in allBundles,
 *     and that call must succeed (a NULL/failed identifier causes the
 *     bundle to be skipped and never dispatched at all -- so
 *     KernelEventMonitor could not have reached callLoadFunction()/
 *     load_KernelEventMonitor() otherwise). The exact same
 *     `bundleInfo->bundle` pointer is what callLoadFunction() then passes
 *     as load_KernelEventMonitor()'s `bundle` argument moments later
 *     (plugin_support.c line 478), with nothing in between that CFReleases
 *     it. So a literally-NULL `bundle` argument at eventmon.c:834 is hard
 *     to reconcile with the traced control flow on its own.
 *
 *   REAL BUG FOUND AND FIXED (independently valid regardless of whether it
 *   is THE exact fault mechanism above -- not dynamically confirmed, see
 *   caveat below): SCPrivate.h's real, unmodified SC_log() macro
 *   (~line 497-514) expands to
 *       size_t __pack_size = os_log_pack_size(__format, ##__VA_ARGS__);
 *       ...
 *       os_log_pack_fill(__pack, __pack_size, errno, __format, ##__VA_ARGS__);
 *   passing the SAME __VA_ARGS__ -- for a `%@` conversion, a real,
 *   arbitrary, possibly side-effecting C expression, e.g. this exact
 *   `CFBundleGetIdentifier(bundle)` -- to TWO call sites. Real Apple's own
 *   macro gets away with this because its size call is
 *   `__builtin_os_log_format_size`, a Clang compiler builtin that computes
 *   the encoded size from the format string and each argument's static
 *   TYPE alone, without emitting code to evaluate the argument
 *   expressions. This project's compat shim (os_log_compat_headers/os/
 *   log_private.h + sc_log_pack_compat.c) implemented os_log_pack_size()
 *   as a real, ordinary variadic C function instead -- which the C
 *   standard requires to evaluate every argument at the call site -- so
 *   every `%@` argument with a real body (a lock/dictionary-lookup/unlock
 *   via CFBundleGetInfoDictionary(), in this exact case) was silently
 *   evaluated TWICE per SC_log() call: once here (result discarded), once
 *   more in os_log_pack_fill() to actually format the message. Fixed
 *   2026-09-17 (DAR-267): os_log_pack_size is now a macro
 *   (os_log_compat_headers/os/log_private.h) that discards its arguments
 *   at the PREPROCESSOR level -- never handed to the compiler as an
 *   expression, so never evaluated -- and calls a real, argument-free
 *   os_log_pack_size_impl() (sc_log_pack_compat.c) instead. Verified via a
 *   real rebuild (build_libsystemconfiguration_client.sh,
 *   build_bootp_dhcp_engine.sh, build_configd_plugins.sh,
 *   build_ipconfiguration_plugin.sh, build_configd.sh, each 0 gaps / 0
 *   undefined symbols) and `nm`: the old variadic `_os_log_pack_size`
 *   symbol no longer appears anywhere in configd.macho or
 *   libSystemConfiguration.dylib, and eventmon.o's own undefined-symbol
 *   list now shows `_os_log_pack_size_impl` in its place.
 *
 *   VERIFIED 2026-09-25 (DAR-267 closed): a QEMU boot of configd built
 *   with a constructor that sets `_sc_debug = 1` (linked directly on
 *   configd.macho's link line -- inside libconfigd.a an unreferenced
 *   constructor is never pulled in) runs every plugin load() through its
 *   debug SC_log() calls, including eventmon.c:834's
 *   `CFBundleGetIdentifier(bundle)`, for KernelEventMonitor,
 *   LinkConfiguration and PreferencesMonitor. No crash, configd stays up,
 *   en0 keeps its lease. The value it prints equals plugin_support.c:397's
 *   "adding %@" bundle-ID pointer, so the bundle and its identifier are
 *   both real (%@ renders as %p here, see sc_log_pack_compat.c). A
 *   DYLD_INSERT_LIBRARIES dylib set through the configd plist's
 *   EnvironmentVariables did NOT take effect, so use the link-line
 *   constructor for this kind of test.
 *
 *   Original caveat, kept for history: this pass did not run a QEMU boot (per this ticket's
 *   own instructions -- another agent may be using QEMU concurrently).
 *   The double-evaluation bug is real and independently worth fixing
 *   (Apple-parity deviation, doubles real work -- including a full extra
 *   lock/unlock -- on every %@-argument SC_log() call site with
 *   `_sc_debug` on), and removing one whole extra nested call chain from
 *   the exact crashing statement is a plausible contributor given it adds
 *   real stack depth and a real second mutex acquisition right on the
 *   faulting path -- but it is NOT dynamically confirmed to be the sole or
 *   exact mechanism of the observed near-NULL `bundle` fault. Recommended
 *   follow-up: re-enable `_sc_debug=1` (same constructor approach the
 *   DAR-266 note above describes, since stock configd.m never sets it),
 *   boot under QEMU, and confirm the eventmon.c:834 crash is gone; if it
 *   still reproduces, get a real register dump at the fault PC via the
 *   QEMU monitor's `x` (virtual memory -- not `xp`, see
 *   feedback_qemu_monitor_xp_vs_x) against the unstripped configd.macho's
 *   own symbol table to see what `bundle` (x0 at the CFBundleGetIdentifier
 *   call) actually holds, rather than continuing from address-offset
 *   inference alone. */

#define NOT_YET_PORTED(name) \
	void \
	load_##name(CFBundleRef bundle, Boolean bundleVerbose) \
	{ \
		(void)bundle; \
		(void)bundleVerbose; \
		SC_log(LOG_NOTICE, "[" #name "] real plugin not yet ported (DAR-239/DAR-240 -- builtin stand-in)"); \
	}

NOT_YET_PORTED(QoSMarking)
/* InterfaceNamer: real since DAR-251 (ifnamer.o + libiokituser.dylib). */
