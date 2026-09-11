/* DAR-285: real, end-to-end verification that the STOCK libresolv path
 * sees the real, published DNS configuration.
 *
 * WHAT THIS PROVES, AND WHY IT NEEDED ITS OWN BINARY
 * ==================================================
 * libresolv.dylib used to ship libresolv_dnsinfo_compat.c, whose
 * dns_configuration_copy() unconditionally returned NULL, so
 * res_init.c's __res_vinit() always fell through to parsing
 * /etc/resolv.conf -- a file that legitimately does not exist on this
 * TARGET_OS_IPHONE target. DAR-285 replaced that stand-in with the real
 * Apple dnsinfo XPC client (configd/dnsinfo/dnsinfo_copy.c).
 *
 * Nothing else on this image exercises that path in an observable way:
 *   - `scutil --dns` calls dns_configuration_copy() but NOT res_init(),
 *     and until DAR-285 it carried its own private copy of the client
 *     rather than using libresolv's at all.
 *   - a full `nm -m` sweep of every shipped dylib and .macho found
 *     exactly one image referencing __res_vinit: libresolv.dylib itself.
 * So "did the fix work" had no existing observer. This binary is that
 * observer, in the same shape as notifyd_roundtrip_smoketest.macho and
 * dispatch_machport_smoketest.macho.
 *
 * It checks BOTH halves, because they can fail independently:
 *   1. dns_configuration_copy() itself -- does the real XPC round trip
 *      to configd's dnsinfo server return a real configuration?
 *   2. res_init() -- does __res_vinit() then take the dnsinfo branch and
 *      really populate the resolver state, rather than silently falling
 *      through to the (absent) /etc/resolv.conf?
 * Half 2 is the one that actually matters for future getaddrinfo/ping
 * work, and it is possible for half 1 to succeed while half 2 reports
 * nscount == 0 -- so a pass is only claimed when both are real.
 *
 * NOT a hang-free assumption: before DAR-284 an unreachable XPC service
 * made libSC_send_message_with_reply_sync() retry forever, which would
 * have turned res_init() into an unconditional hang. This binary
 * deliberately prints a marker BEFORE each of the two calls, so a hang
 * is distinguishable from a NULL result in a boot log rather than
 * looking identical to it.
 */
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <dnsinfo.h>
#include <resolv.h>

/* The real exported global is `_res_9` (res_data.c defines
 * `struct __res_9_state _res_9;` under -DUSE__RES_9, which is how this
 * project builds libresolv -- see build_libresolv.sh). <resolv.h>'s own
 * `extern struct __res_state _res;` declaration names a symbol that is
 * NOT exported by libresolv.dylib, so declare the real one. The struct
 * type is the same: resolv.h does `#define __res_state __res_9_state`. */
extern struct __res_state _res_9;

static void
print_sockaddr(const char *label, const struct sockaddr *sa)
{
	char buf[INET6_ADDRSTRLEN];

	if (sa == NULL) {
		printf("%s <null>\n", label);
		return;
	}
	memset(buf, 0, sizeof(buf));
	if (sa->sa_family == AF_INET) {
		const struct sockaddr_in *s4 = (const struct sockaddr_in *)(const void *)sa;
		inet_ntop(AF_INET, &s4->sin_addr, buf, sizeof(buf));
		printf("%s %s\n", label, buf);
	} else if (sa->sa_family == AF_INET6) {
		const struct sockaddr_in6 *s6 = (const struct sockaddr_in6 *)(const void *)sa;
		inet_ntop(AF_INET6, &s6->sin6_addr, buf, sizeof(buf));
		printf("%s %s\n", label, buf);
	} else {
		printf("%s <sa_family %d>\n", label, sa->sa_family);
	}
}

int
main(void)
{
	dns_config_t *cfg;
	int i, j;
	int copy_ok = 0;
	int res_ok = 0;
	int n_expect = 0;
	struct in_addr expect[MAXNS];

	printf("res_init_dnsinfo_smoketest: start\n");

	/* --- half 1: the raw dnsinfo XPC client ------------------- */
	printf("res_init_dnsinfo_smoketest: calling dns_configuration_copy()...\n");
	fflush(stdout);
	cfg = dns_configuration_copy();
	if (cfg == NULL) {
		printf("res_init_dnsinfo_smoketest: dns_configuration_copy() returned NULL\n");
	} else {
		printf("res_init_dnsinfo_smoketest: dns_configuration_copy() ok, n_resolver=%d\n",
		       cfg->n_resolver);
		for (i = 0; i < cfg->n_resolver; i++) {
			dns_resolver_t *r = cfg->resolver[i];

			if (r == NULL) continue;
			printf("  resolver[%d]: n_nameserver=%d domain=%s\n",
			       i, r->n_nameserver,
			       (r->domain != NULL) ? r->domain : "(none)");
			for (j = 0; j < r->n_nameserver; j++) {
				char lbl[64];
				snprintf(lbl, sizeof(lbl), "    nameserver[%d]:", j);
				print_sockaddr(lbl, r->nameserver[j]);
				/* Remember the IPv4 ones so half 2 can be
				 * cross-checked against them rather than just
				 * counted -- see the comment at the PASS test. */
				if (r->nameserver[j] != NULL &&
				    r->nameserver[j]->sa_family == AF_INET &&
				    n_expect < MAXNS) {
					const struct sockaddr_in *s4 =
						(const struct sockaddr_in *)(const void *)r->nameserver[j];
					expect[n_expect++] = s4->sin_addr;
				}
			}
			if (r->n_nameserver > 0) copy_ok = 1;
		}
		dns_configuration_free(cfg);
	}

	/* --- half 2: the real res_init() consumer ------------------ */
	printf("res_init_dnsinfo_smoketest: calling res_init()...\n");
	fflush(stdout);
	(void)res_init();
	printf("res_init_dnsinfo_smoketest: res_init() returned, nscount=%d ndots=%u\n",
	       _res_9.nscount, (unsigned)_res_9.ndots);
	for (i = 0; i < _res_9.nscount && i < MAXNS; i++) {
		char lbl[64];
		snprintf(lbl, sizeof(lbl), "  _res.nsaddr_list[%d]:", i);
		print_sockaddr(lbl, (const struct sockaddr *)(const void *)&_res_9.nsaddr_list[i]);
		/* nscount > 0 alone is NOT evidence of anything: the
		 * /etc/resolv.conf fallback path this ticket exists to stop
		 * taking also yields nscount == 1 (with nsaddr_list[0] ==
		 * 0.0.0.0 -- observed for real on this target, since the file
		 * does not exist). The only meaningful check is that the
		 * resolver state actually matches what dnsinfo returned. */
		for (j = 0; j < n_expect; j++) {
			if (_res_9.nsaddr_list[i].sin_addr.s_addr == expect[j].s_addr) {
				res_ok = 1;
				break;
			}
		}
	}

	if (copy_ok && res_ok) {
		printf("res_init_dnsinfo_smoketest: PASS -- stock libresolv sees the real DNS configuration\n");
		return 0;
	}
	printf("res_init_dnsinfo_smoketest: FAIL -- copy_ok=%d res_ok=%d\n", copy_ok, res_ok);
	return 1;
}
