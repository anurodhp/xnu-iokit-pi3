/* Real configuration_profile.h has no source anywhere in this project's
 * pinned repos, and no standalone apple-oss-distributions repo exists
 * for it either (confirmed: `apple-oss-distributions/configuration_profile`
 * 404s, not just unpinned) -- it's part of Apple's closed-source MDM/
 * enterprise-enrollment stack (ConfigurationProfiles.framework),
 * genuinely never open-sourced, same class of gap as Sandbox/corecrypto.
 * Only 2 real entry points are actually called anywhere in this checkout
 * (grepped): configuration_profile_copy_property_list() (asl_common.c,
 * asl_action.c) and configuration_profile_create_notification_key()
 * (asl_action.c only). Both let a real ASL install-time-configured
 * "profile" override per-facility log filtering via enterprise MDM --
 * structurally inapplicable on this target (no MDM enrollment concept
 * at all, bare-metal boot). Honest no-op stand-ins (real, callable,
 * always "no profile installed" -- not a fabricated success), matching
 * this project's existing sandbox_spawnattrs_compat.h/
 * transaction_private.h precedent for the same class of gap.
 */
#ifndef _CONFIGURATION_PROFILE_COMPAT_H_
#define _CONFIGURATION_PROFILE_COMPAT_H_

#include <xpc/xpc.h>

static inline xpc_object_t
configuration_profile_copy_property_list(const char *ident)
{
	(void)ident;
	return NULL; /* real callers already null-check this return */
}

static inline char *
configuration_profile_create_notification_key(const char *ident)
{
	(void)ident;
	return NULL; /* real callers already null-check this return */
}

#endif /* _CONFIGURATION_PROFILE_COMPAT_H_ */
