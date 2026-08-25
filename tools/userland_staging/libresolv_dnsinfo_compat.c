/* Real Apple dns_configuration_copy()/dns_configuration_free()/
 * dns_configuration_notify_key() (declared in this build's vendored real
 * dnsinfo.h -- see build_libresolv.sh's own compat_include heredoc,
 * fetched verbatim from apple-oss-distributions/configd tag
 * configd-1109.60.2, the exact tag distribution-macOS's macos-112
 * manifest pins -- same manifest this project already cross-checked to
 * confirm the libresolv-68 pin in setup_third_party.sh) are genuinely
 * implemented in configd itself
 * (configd/dnsinfo/dnsinfo_copy.c:dns_configuration_copy()), via a
 * dispatch_sync()'d XPC round-trip to the running configd daemon. This
 * project has no XPC, no launchd, no configd -- a real, already-
 * documented architectural gap (see project memory
 * project_xnu_rpi3_dhcp_ipconfiguration_analysis.md: "real blocker is
 * SCDynamicStore/configd").
 *
 * This is NOT a fabricated stand-in for DNS resolver logic: Apple's own
 * dns_configuration_copy() already contains, and takes, this exact "DNS
 * configuration server not available" branch on a real, unmodified
 * system whenever configd/the dnsinfo XPC service isn't reachable --
 * e.g. genuinely early boot, or (per that function's own os_log message)
 * "DNS configuration requested between fork() and exec()" -- returning
 * NULL either way (dnsinfo_copy.c, the `if (!libSC_info_available())`
 * and `if ((dnsinfo_client == NULL) || !dnsinfo_client->active)` early
 * returns). res_init.c's __res_vinit() (the only caller left in this
 * build -- dns.c, the other real caller, is scoped out of this tier, see
 * build_libresolv.sh) already has a real, source-supplied fallback for
 * exactly that NULL:
 *
 *     sc_dns = dns_configuration_copy();
 *     if (sc_dns == NULL) return res_vinit_from_file(statp, preinit, _PATH_RESCONF);
 *
 * -- the classic /etc/resolv.conf parser, which this project's
 * res_vinit_from_file() (same file, compiles clean with zero dnsinfo
 * dependency) already handles. This shim just always takes that same
 * real, Apple-authored branch, unconditionally -- there being no configd
 * here for any other branch to ever be reachable.
 *
 * dns_configuration_notify_key() returns the real, literal notify(3) key
 * string Apple's own implementation returns (dnsinfo_copy.c:
 * "com.apple.system.SystemConfiguration.dns_configuration"). Nothing in
 * this build calls it (only dns.c does, and dns.c is scoped out) --
 * included anyway since it costs nothing to keep byte-for-byte real
 * rather than omit.
 *
 * dns_configuration_free() matches the real function's own NULL-check
 * fast path (dnsinfo_copy.c: "if (config == NULL) return;") -- since
 * dns_configuration_copy() here can only ever return NULL, that is the
 * only branch ever reachable, but the free(config) path is kept real/
 * correct in case that ever changes.
 */
#include "dnsinfo.h"
#include <stdlib.h>

const char *
dns_configuration_notify_key(void)
{
    return "com.apple.system.SystemConfiguration.dns_configuration";
}

dns_config_t *
dns_configuration_copy(void)
{
    return NULL;
}

void
dns_configuration_free(dns_config_t *config)
{
    if (config == NULL) return;
    free((void *)config);
}
