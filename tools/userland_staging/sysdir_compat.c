/* Real sysdir_start_search_path_enumeration()/
 * sysdir_get_next_search_path_enumeration(): confirmed never open-
 * sourced anywhere (no apple-oss-distributions repo ships them). Real
 * Darwin resolves these against a real per-domain (user/local/network/
 * system) directory search-path table this project has no equivalent
 * of -- no multi-domain Library/Application Support layout, no
 * NSSearchPathForDirectoriesInDomains-style config anywhere in this
 * project's flat single-root filesystem model.
 *
 * UPDATED 2026-09-04 (DAR-170): this blanket "always return 0" stub was
 * a real, previously-undiscovered blocker for the ENTIRE configd plugin
 * architecture, not just IPConfiguration. Root cause, confirmed by
 * reading plugin_support.c's real, unmodified plugin_exec() directly:
 * even "builtin" (statically-linked) plugins are only actually invoked
 * (load()/start()/prime()) if plugin_exec()'s own real bundle-directory
 * scan first discovers a matching CFBundleRef -- and that scan's one and
 * only real call is
 *   state = sysdir_start_search_path_enumeration(SYSDIR_DIRECTORY_LIBRARY,
 *                                                SYSDIR_DOMAIN_MASK_SYSTEM);
 *   while ((state = sysdir_get_next_search_path_enumeration(state, path)))
 * With the old blanket stub, this returns 0 on the very first call, so
 * plugin_exec() enumerates ZERO directories, appends nothing to
 * allBundles, and every plugin -- InterfaceNamer, LinkConfiguration,
 * KernelEventMonitor, PreferencesMonitor, IPMonitor, QoSMarking, and
 * (DAR-170's own new) IPConfiguration -- silently never gets its load()/
 * start()/prime() called, REGARDLESS of being statically linked into
 * configd.macho with zero undefined symbols. A clean link proves the
 * code is present; it does not prove plugin_exec() will ever call it --
 * confirmed empirically via a real QEMU direct invocation
 * (`configd -d -v`) showing zero bundle-related SC_log activity and an
 * immediate return to the shell instead of the expected persistent
 * CFRunLoopRun().
 *
 * Real, narrow, DOCUMENTED-behavior fix (not a fabricated path list):
 * on real Darwin, SYSDIR_DIRECTORY_LIBRARY (5) + SYSDIR_DOMAIN_MASK_SYSTEM
 * (1<<3) resolves to exactly one real, public, well-known path,
 * "/System/Library" -- this is the exact same value
 * NSSearchPathForDirectoriesInDomains(NSLibraryDirectory,
 * NSSystemDomainMask, YES) returns on every real Darwin system, and
 * matches plugin_support.c's own comment directly above its
 * BUNDLE_DIRECTORY macro ("[/System/Library]/..."). This project's own
 * single-root filesystem genuinely has a real, real-content
 * /System/Library directory (this same fix requires
 * inject_into_sd_image.sh to actually populate
 * /System/Library/SystemConfiguration/*.bundle -- see that script's own
 * new IPConfiguration/plugin-bundle install block), so returning this
 * one real path for this one real, exact (dir, domainMask) pair is not
 * a shortcut -- it is the one case this project can genuinely answer
 * correctly. Every other (dir, domainMask) combination still honestly
 * returns "no match" (0), unchanged from the prior stub, since this
 * project still has no other real per-domain directory layout (no
 * ~/Library, no /Library, no /Network) to answer for.
 *
 * State-machine encoding note: the real, canonical
 * "while ((state = get_next(state, path)))" idiom requires get_next()
 * to write `path` and return a truthy "continue" state IN THE SAME CALL
 * that produces the last real path (so the loop body still runs for it),
 * then return 0 on the NEXT call (with no further write) to terminate.
 * This implementation's state values (2 = "one real path pending",
 * 1 = "already delivered, nothing left") are an honest, self-consistent
 * two-step encoding that produces exactly this real, correct observable
 * behavior for the one real case handled -- not a guess at Apple's own
 * undocumented internal bitmask representation. */
#include <stddef.h>
#include <string.h>

typedef unsigned int sysdir_search_path_enumeration_state;

#define SYSDIR_DIRECTORY_LIBRARY_COMPAT		5u
#define SYSDIR_DOMAIN_MASK_SYSTEM_COMPAT	(1u << 3)

sysdir_search_path_enumeration_state
sysdir_start_search_path_enumeration(unsigned int dir, unsigned int domainMask)
{
	if (dir == SYSDIR_DIRECTORY_LIBRARY_COMPAT
	    && (domainMask & SYSDIR_DOMAIN_MASK_SYSTEM_COMPAT) != 0) {
		/* one real path pending: "/System/Library" */
		return 2;
	}
	return 0;
}

sysdir_search_path_enumeration_state
sysdir_get_next_search_path_enumeration(sysdir_search_path_enumeration_state state, char *path)
{
	if (state == 2) {
		if (path != NULL) {
			strcpy(path, "/System/Library");
		}
		/* signal "a path was written this call, one more call to
		 * reach real exhaustion" */
		return 1;
	}
	/* state == 1 (already delivered) or state == 0 (never matched):
	 * real exhaustion, write nothing further */
	return 0;
}
