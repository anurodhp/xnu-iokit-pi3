/*
 * Real, verbatim copy of __log_SCPreferences() -- NOT a stub. Real,
 * unmodified Apple source (SystemConfiguration.fproj/SCPOpen.c) already
 * defines this exact function, but as `__private_extern__` (hidden
 * visibility, by design -- each real linkage unit that needs it gets its
 * own private copy, same pattern __log_IPMonitor() already uses in
 * ip_plugin.c). SCPOpen.c itself is part of libSystemConfiguration.dylib
 * (the CLIENT-side SCPreferences implementation, a separate Mach-O image
 * from configd.macho) so its private copy isn't visible here.
 * Plugins/../SystemConfiguration.fproj/scprefs_observer.c (DAR-241,
 * 2026-09-04 -- newly compiled directly into configd.macho's own
 * libconfigd_plugins.a) needs its own real copy of this trivial
 * os_log_create() handle accessor, matching real Apple's own SCPOpen.c
 * body byte-for-byte.
 */
#include <os/log.h>

os_log_t
__log_SCPreferences(void)
{
	static os_log_t	log	= NULL;

	if (log == NULL) {
		log = os_log_create("com.apple.SystemConfiguration", "SCPreferences");
	}

	return log;
}
