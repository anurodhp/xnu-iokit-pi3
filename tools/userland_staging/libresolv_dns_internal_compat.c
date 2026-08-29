/* Real, honest stand-ins for 4 dns.c-internal helpers (_check_cache,
 * _sdns_search, _pdns_search, dns_search_list_count) that dns_util.c
 * (one of the 24 real files this tier DOES compile) declares `extern`
 * and calls -- real bodies only exist in dns.c itself, which
 * build_libresolv.sh's own header already documents as a deliberate
 * exclusion (the newer system-configuration/notify(3)-aware DNS query
 * front end, needs both dnsinfo and real notify(3), genuinely out of
 * scope; qclassic res_send()/res_query() never call into it). Real
 * prototypes copied verbatim from dns_util.c's own `extern` forward
 * declarations (not guessed) -- `sdns_handle_t`/`pdns_handle_t` are
 * real opaque pointer types (dns_private.h), declared here as `void *`
 * since only the pointer value crosses this ABI boundary, never
 * dereferenced by these honest stand-ins.
 *
 * Deferred via `-Wl,-U` before, which doesn't work: real dyld's eager
 * chained-fixups binding fails at real process load time regardless of
 * whether the call path executes (confirmed via a real QEMU boot of
 * launchd: "symbol not found in flat namespace '__check_cache'", pid 1
 * exited before main() ran). Real, honest "no system-configuration DNS
 * front end" behavior instead, same shape as this project's own
 * dnsinfo_compat.c (dns_configuration_copy() returning NULL) --
 * _sdns_search/_pdns_search return -1 (real dns.c convention: negative
 * means lookup failure, confirmed by dns_util.c's own `if (len < 0)
 * continue;`-style real caller checks around these calls). */
typedef unsigned int uint32_t;
struct sockaddr;

void
_check_cache(void *sdns)
{
	(void)sdns;
}

int
_sdns_search(void *sdns, const char *name, uint32_t qclass, uint32_t type,
    uint32_t fqdn, uint32_t recurse, char *buf, uint32_t len,
    struct sockaddr *from, uint32_t *fromlen, int *min)
{
	(void)sdns;
	(void)name;
	(void)qclass;
	(void)type;
	(void)fqdn;
	(void)recurse;
	(void)buf;
	(void)len;
	(void)from;
	(void)fromlen;
	(void)min;
	return -1;
}

int
_pdns_search(void *sdns, void *pdns, const char *name, uint32_t qclass,
    uint32_t type, char *buf, uint32_t len, struct sockaddr *from,
    uint32_t *fromlen)
{
	(void)sdns;
	(void)pdns;
	(void)name;
	(void)qclass;
	(void)type;
	(void)buf;
	(void)len;
	(void)from;
	(void)fromlen;
	return -1;
}

uint32_t
dns_search_list_count(const void *dns)
{
	(void)dns;
	return 0;
}
