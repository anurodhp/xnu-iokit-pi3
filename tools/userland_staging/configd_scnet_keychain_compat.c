/* Real, honestly-documented no-op stand-in for the 6 real Keychain-backed
 * "shared secret" storage functions SCNetworkConfigurationInternal.c and
 * SCNetworkInterface.c call (SCPreferencesKeychainPrivate.h, real,
 * unmodified, unpinned-only-because-it-needs-a-real-Security.framework
 * header): _SCSecKeychainPasswordItemCopy/Exists/Remove and
 * _SCPreferencesSystemKeychainPasswordItemCopy/Exists/Remove.
 *
 * Real, confirmed scope: these read/write a PPP/VPN/8021X network
 * service's stored shared-secret password to/from the macOS Keychain.
 * This project has no Keychain/Security subsystem at all (no
 * securityd, no Security.framework, no keychain file format) --
 * same real, already-established "genuinely inapplicable subsystem"
 * category as this same build's lockdown.h/VPN_RUNNING/SMB compat
 * stand-ins (see build_libsystemconfiguration_client.sh's own header
 * comment). InterfaceNamer/LinkConfiguration/PreferencesMonitor (this
 * project's real target plugins, DAR-240) never manage individual
 * per-service PPP/VPN passwords at plugin-load time -- confirmed by
 * reading ifnamer.c/linkconfig.c/prefsmon.c, zero calls into any
 * SCNetworkConfigurationInternal.c/SCNetworkInterface.c code path that
 * reaches these 6 functions.
 *
 * Real behavior match, not a fabrication: on a real Mac with no matching
 * Keychain item (e.g. a freshly-restored/never-configured system), these
 * functions' real Apple implementations already return exactly
 * "not found"/"nothing to remove" -- FALSE for the two Exists checks,
 * NULL for the two Copy accessors, TRUE (vacuous success, nothing to do)
 * for the two Remove calls -- which is what every real caller already
 * handles as a normal, non-error outcome (confirmed by reading each real
 * call site). This stand-in always returns that same real "not present"
 * answer rather than ever calling into a Keychain that does not exist
 * here. DAR-240.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SCPreferences.h>
#include <Security/Security.h>

CFDataRef
_SCSecKeychainPasswordItemCopy(SecKeychainRef keychain, CFStringRef unique_id)
{
	(void)keychain;
	(void)unique_id;
	return NULL;
}

Boolean
_SCSecKeychainPasswordItemExists(SecKeychainRef keychain, CFStringRef unique_id)
{
	(void)keychain;
	(void)unique_id;
	return FALSE;
}

Boolean
_SCSecKeychainPasswordItemRemove(SecKeychainRef keychain, CFStringRef unique_id)
{
	(void)keychain;
	(void)unique_id;
	return TRUE;
}

CFDataRef
_SCPreferencesSystemKeychainPasswordItemCopy(SCPreferencesRef prefs, CFStringRef unique_id)
{
	(void)prefs;
	(void)unique_id;
	return NULL;
}

Boolean
_SCPreferencesSystemKeychainPasswordItemExists(SCPreferencesRef prefs, CFStringRef unique_id)
{
	(void)prefs;
	(void)unique_id;
	return FALSE;
}

Boolean
_SCPreferencesSystemKeychainPasswordItemRemove(SCPreferencesRef prefs, CFStringRef unique_id)
{
	(void)prefs;
	(void)unique_id;
	return TRUE;
}

/* _SCPreferencesSystemKeychainPasswordItemSet: unlike the Copy/Exists/
 * Remove accessors above (all real, honest "not present" answers), a real
 * WRITE genuinely cannot succeed with no Keychain subsystem present --
 * FALSE is the real, honest answer, not a fabricated success. Every real
 * call site (SCNetworkInterface.c) already handles a FALSE result as a
 * normal, non-fatal "didn't persist the secret" outcome and simply
 * doesn't update its in-memory config dictionary, confirmed by reading
 * each of the 5 real call sites. */
Boolean
_SCPreferencesSystemKeychainPasswordItemSet(SCPreferencesRef prefs,
					     CFStringRef unique_id,
					     CFStringRef label,
					     CFStringRef description,
					     CFStringRef account,
					     CFDataRef password,
					     CFDictionaryRef options)
{
	(void)prefs;
	(void)unique_id;
	(void)label;
	(void)description;
	(void)account;
	(void)password;
	(void)options;
	return FALSE;
}
