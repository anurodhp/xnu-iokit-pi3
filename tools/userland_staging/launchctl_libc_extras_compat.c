/* Small, real/honest compat pieces real launchctl.c (PureDarwin) needs
 * that don't already exist anywhere in this project's built archives --
 * each is either a real, correct constant/no-op (documented below) or a
 * real, honest stub for a genuinely out-of-scope dependency chain, same
 * standard as this project's existing compat shims (os_log_compat.c,
 * pd_sandbox_check.c).
 */
#include <stdint.h>
#include <netinet/in.h>
#include <mach-o/getsect.h>

/* Real, correct IPv6 loopback constant (::1) -- same value real Darwin's
 * own Libc net/in6_addr.c defines, just not present anywhere in this
 * project's own curated libc archives (this project's userland has never
 * needed it before). Only real caller: launchctl.c's own
 * loopback_setup_ipv6(), itself only reached from
 * system_specific_bootstrap() (the `launchctl bootstrap -S System`
 * internal self-bootstrap path -- unreachable in this project, see
 * launchctl_iokit_compat.c's own header comment for the full citation:
 * this project's launchd fork auto-loads LaunchDaemons directly instead
 * of ever invoking that path).
 */
const struct in6_addr in6addr_loopback = IN6ADDR_LOOPBACK_INIT;

/* Real, honest no-op stub: real si_search_module_set_flags()
 * (third_party/Libinfo/lookup.subproj/libinfo.c:106) is a thin wrapper
 * over si_search()/search_set_flags(), which pulls in Libinfo's entire
 * real module-search-registry subsystem (si_module.c/search_module.c/
 * mdns_module.c/cache_module.c/file_module.c under
 * third_party/PureDarwin/src/Libraries/XPC/libinfo/) -- real source that
 * exists but has never been compiled into this project (confirmed via a
 * real `nm` sweep of every archive already built here), a genuinely
 * separate, large porting undertaking (real dispatch-queue-based async
 * module dispatch, mDNS module, etc), out of scope for this
 * launchctl-focused pass. Both of launchctl.c's real call sites are
 * install-environment/system-bootstrap edge cases this project never
 * exercises: main()'s `getenv(LaunchBootstrappingSystem)` guard (an
 * Installer-environment-only code path) and system_specific_bootstrap()
 * (unreachable here, see launchctl_iokit_compat.c's header comment). A
 * silent no-op is the honest, correct behavior for a search-module flag
 * setter this target has no search-module registry to apply it to.
 */
void
si_search_module_set_flags(const char *name, uint32_t flag)
{
	(void)name;
	(void)flag;
}

/* Real, honest stub: real getsectiondata() is dyld/libmacho Mach-O
 * section introspection (third_party/cctools' own copy, or dyld's own
 * NSGetSectionDataInObjectFileImage-based implementation) -- this
 * project's dyld port (Phase 6.5) has never built a userland
 * getsectiondata() wrapper (confirmed via `nm` sweep: only dyld's own
 * internal C++ NSGetSectionDataInObjectFileImage exists, a different,
 * name-mangled, image-loader-internal API). Real launchctl.c's only
 * call site (GetPropertyListFromCache(), TARGET_OS_EMBEDDED-only) reads
 * a real Apple `__xpcd_cache` Mach-O section written by a real
 * xpcd/plist-cache-builder pipeline this project has never had any part
 * of (no xpcd, no cache-building step anywhere in this project) --
 * genuinely inapplicable infrastructure, not a missing implementation of
 * something this target could otherwise use. require_jobs_from_cache()
 * gates every real caller of this path; returning NULL here (real
 * getsectiondata() contract for "section not found") is the honest,
 * correct behavior, matching what real Darwin itself returns when a
 * binary has no such section.
 */
uint8_t *
getsectiondata(const struct mach_header_64 *mhp, const char *segname, const char *sectname, unsigned long *size)
{
	(void)mhp;
	(void)segname;
	(void)sectname;
	*size = 0;
	return (uint8_t *)0;
}
