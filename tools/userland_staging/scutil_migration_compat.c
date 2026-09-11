/*
 * DAR-283: narrow stand-in for the 2 SCNetworkMigration entry points
 * scutil's experimental `--net` mode's `migrate` command calls.
 *
 * REAL BOUNDARY, FOUND BY A REAL TRIAL LINK (not assumed)
 * ======================================================
 * SystemConfiguration.fproj/SCNetworkMigration.c is real, pinned Apple
 * source and it COMPILES clean here -- this build did compile it, and
 * the reason it is not linked is the trial link that followed. Real
 * Apple builds SCNetworkMigration.c inside the SystemConfiguration
 * framework itself, and it leans on 20 sibling-file internals that are
 * `__private_extern__` (hidden) in that framework:
 *
 *   __SCNetworkConfigurationBackup, __SCNetworkInterfaceCopyAll_IONetworkInterface,
 *   __SCNetworkInterfaceCopyStorageEntity, __SCNetworkInterfaceCopyStoredWithPreferences,
 *   __SCNetworkInterfaceCreateCopy, __SCNetworkInterfaceCreateMappingUsingBSDName,
 *   __SCNetworkInterfaceCreateWithNIPreferencesUsingBSDName,
 *   __SCNetworkInterfaceGetEntitySubType, __SCNetworkInterfaceGetEntityType,
 *   __SCNetworkInterfaceGetUserDefinedName, __SCNetworkInterfaceIsMember,
 *   __SCNetworkInterfaceMatchesName, __SCNetworkInterfaceSaveStoredWithPreferences,
 *   __SCNetworkInterfaceSetIOInterfaceUnit, __SCNetworkServiceAddProtocolToService,
 *   __SCNetworkServiceCopyAllInterfaces, __SCNetworkServiceCreate,
 *   __SCNetworkServiceMigrateNew, __SCPreferencesIsEmpty,
 *   __log_SCNetworkConfiguration
 *
 * Every one of those really exists and really works in this project --
 * SCNetworkInterface.c / SCNetworkService.c / SCP*.c are all built into
 * libc_build/system/libSystemConfiguration.dylib by
 * build_libsystemconfiguration_client.sh -- they are simply not
 * *exported* from it, because Apple's own source marks them
 * __private_extern__ (verified with `nm -gU` on the built dylib, and by
 * reading the declarations, not inferred from the link error alone).
 * That is the correct architecture, not a defect: SCNetworkMigration.c
 * belongs INSIDE that dylib, alongside the files it is private to.
 *
 * Putting it there means rebuilding libSystemConfiguration.dylib, which
 * configd.macho / ipconfig.macho / the DAR-170 DHCP milestone all
 * currently sit on top of and which is boot-proven as built. That is a
 * separate, deliberately-scoped change, filed as its own ticket rather
 * than folded into this one.
 *
 * WHY THESE RETURN VALUES ARE REAL APPLE BEHAVIOUR
 * ===============================================
 * Both call sites in net.c already handle exactly these values, with
 * Apple's own code and Apple's own user-visible wording:
 *
 *     migrationFiles = _SCNetworkConfigurationPerformMigration(...);
 *     if (migrationFiles != NULL) { ...("Migration complete\n")... }
 *     else                        { SCPrint(TRUE, stdout, CFSTR("Migration failed\n")); }
 *
 *     isValid = _SCNetworkMigrationAreConfigurationsIdentical(...);
 *     SCPrint(TRUE, stdout, CFSTR("Configuration at location %s %s\n"),
 *             configuration, isValid ? "is valid" : "is NOT valid");
 *
 * So the tool reports a real, honest failure rather than a fabricated
 * success. Both are also doubly unreachable in practice: `migrate` only
 * exists in the `commands_net` table, and scutil.c refuses to enter
 * `--net` mode at all unless ENABLE_EXPERIMENTAL_SCUTIL_COMMANDS is set
 * in the environment.
 *
 * Nothing in the DAR-283 verification path (the dynamic store, --dns,
 * --nwi) touches either function.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SystemConfiguration.h>

CFArrayRef
_SCNetworkConfigurationPerformMigration(CFURLRef sourceDir, CFURLRef currentDir,
					CFURLRef targetDir, CFDictionaryRef options)
{
	(void)sourceDir; (void)currentDir; (void)targetDir; (void)options;
	return NULL;
}

Boolean
_SCNetworkMigrationAreConfigurationsIdentical(CFURLRef configurationURL,
					      CFURLRef expectedConfigurationURL)
{
	(void)configurationURL; (void)expectedConfigurationURL;
	return FALSE;
}
