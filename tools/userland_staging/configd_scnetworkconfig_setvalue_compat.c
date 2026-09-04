/* Real, narrowly-extracted forwarding body for __SCNetworkConfigurationSetValue(),
 * closing a genuine runtime blocker found booting configd.tproj directly
 * (DAR-170 session, 2026-09-04, after the CFURLCopyResourcePropertyForKey
 * fix landed): dyld aborted at load with "symbol not found in flat
 * namespace '___SCNetworkConfigurationSetValue'".
 *
 * Real root cause: SCDHostName.c (already compiled into
 * libSystemConfiguration.dylib -- it implements SCDynamicStoreCopyComputerName
 * et al, core, unrelated to the excluded SCNetworkConfiguration/
 * SCNetworkInterface/SCNetworkSet family) calls
 * __SCNetworkConfigurationSetValue() three times (persisting the local/
 * computer hostname into the prefs plist). Its real implementation lives in
 * SCNetworkConfigurationInternal.c, which build_libsystemconfiguration_client.sh
 * deliberately did NOT compile in (that whole file is part of the much
 * larger, separately-scoped SCNetworkConfiguration/SCNetworkSet/
 * SCNetworkService/SCBridgeInterface/SCVLANInterface subsystem -- see
 * DAR-240 for the full family still blocking InterfaceNamer/
 * LinkConfiguration/PreferencesMonitor). That build script already
 * anticipated this exact gap and honestly `-Wl,-U`-deferred it (see its own
 * header comment: "___SCNetworkConfigurationSetValue ... real, honestly
 * -Wl,-U-deferred cross-ticket/out-of-scope gap").
 *
 * Real, narrow fix: __SCNetworkConfigurationSetValue()'s real body (copied
 * VERBATIM below from SCNetworkConfigurationInternal.c, not reinvented) is
 * fully self-contained -- every real symbol it touches
 * (isA_CFDictionary/kSCResvInactive/_SCErrorSet/SCPreferencesPathGetValue/
 * SCPreferencesPathSetValue/SCPreferencesPathRemoveValue/_SC_CFEqual/SCError)
 * is EITHER a header-only static inline (isA_CFDictionary, from the
 * already-reachable SCValidation.h) OR already a real, exported symbol in
 * the already-built libSystemConfiguration.dylib (confirmed via `nm` --
 * kSCResvInactive is even already an exported data symbol, not deferred).
 * This is the SAME "narrowly-extracted forwarding body, not the whole
 * file" precedent build_libsystemconfiguration_client.sh's own session
 * already established for mach_port_get_refs/audit_token_to_au32 -- real,
 * unmodified Apple source, just compiled from a smaller real translation
 * unit instead of pulling in the rest of SCNetworkConfigurationInternal.c
 * (which itself needs the much larger unported family).
 *
 * Linked as a loose .o directly into configd.macho (see build_configd.sh),
 * NOT added to libconfigd.a: nothing in configd's own object graph calls
 * this function directly -- it is only ever reached via
 * libSystemConfiguration.dylib's own flat-namespace lookup at runtime (the
 * same real Mach-O two-level-namespace `-Wl,-U` mechanism the
 * CFURLCopyResourcePropertyForKey fix already uses in this same build
 * script -- see configd_cfurl_filesize_compat.c's own header comment).
 *
 * Real, load-bearing difference from the original source: the real
 * declaration uses `__private_extern__` (hidden visibility), correct for
 * real Apple's build where this function is only ever called from within
 * the SAME image (their whole SystemConfiguration.framework is one link
 * unit). Here it must be reachable via CROSS-IMAGE flat-namespace lookup
 * from libSystemConfiguration.dylib, so hidden visibility is wrong --
 * confirmed empirically (a first real build with __private_extern__ still
 * produced the identical "symbol not found in flat namespace" abort; `nm`
 * showed the symbol present but lowercase `t`, i.e. a real, but
 * dyld-invisible, local symbol). Declared with default (external) visibility
 * below instead.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SCValidation.h>
#include <SystemConfiguration/SCPrivate.h>
#include "SCNetworkConfigurationInternal.h"

Boolean
__SCNetworkConfigurationSetValue(SCPreferencesRef	prefs,
				 CFStringRef		path,
				 CFDictionaryRef	config,
				 Boolean		keepInactive)
{
	Boolean			changed;
	CFDictionaryRef		curConfig;
	CFMutableDictionaryRef	newConfig	= NULL;
	Boolean			ok;

	if ((config != NULL) && !isA_CFDictionary(config)) {
		_SCErrorSet(kSCStatusInvalidArgument);
		return FALSE;
	}

	curConfig = SCPreferencesPathGetValue(prefs, path);
	curConfig = isA_CFDictionary(curConfig);

	if (config != NULL) {
		newConfig = CFDictionaryCreateMutableCopy(NULL, 0, config);
	}

	if (keepInactive) {
		if (config == NULL) {
			newConfig = CFDictionaryCreateMutable(NULL,
							      0,
							      &kCFTypeDictionaryKeyCallBacks,
							      &kCFTypeDictionaryValueCallBacks);
		}

		if (isA_CFDictionary(curConfig) && CFDictionaryContainsKey(curConfig, kSCResvInactive)) {
			// if currently disabled
			CFDictionarySetValue(newConfig, kSCResvInactive, kCFBooleanTrue);
		} else {
			// if currently enabled
			CFDictionaryRemoveValue(newConfig, kSCResvInactive);
		}
	}

	// check if the configuration changed
	changed = !_SC_CFEqual(curConfig, newConfig);

	// set new configuration
	if (!changed) {
		// if no change
		if (newConfig != NULL) CFRelease(newConfig);
		ok = TRUE;
	} else if (newConfig != NULL) {
		// if new configuration (or we are preserving a disabled state), update the prefs
		ok = SCPreferencesPathSetValue(prefs, path, newConfig);
		CFRelease(newConfig);
	} else {
		// update the prefs
		ok = SCPreferencesPathRemoveValue(prefs, path);
		if (!ok && (SCError() == kSCStatusNoKey)) {
			ok = TRUE;
		}
	}

	return ok;
}
