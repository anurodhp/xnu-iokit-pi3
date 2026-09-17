/* DAR-182: real, end-to-end verification that a real DNS query over a
 * real network socket resolves a real hostname, through the exact real
 * functions ping.macho/ifconfig.macho were just wired to use.
 *
 * WHAT THIS PROVES, AND WHY NOTHING ELSE ON THIS IMAGE ALREADY DID
 * =================================================================
 * DAR-306 (2026-09-14) built real dns_module.c into libinfo.dylib's
 * default search order (cache -> dns -> file) and real gethostbyname/
 * gethostbyname2/gethostbyaddr/getaddrinfo/getnameinfo exports. This
 * project's own build_ping.sh/build_ifconfig.sh were fixed the same day
 * as this file (DAR-182) to link libinfo.dylib + libresolv.dylib instead
 * of their old honest "no resolver" stand-ins - but nothing had actually
 * run a real DNS query end-to-end since dns_module.c landed.
 * res_init_dnsinfo_smoketest.macho (DAR-285) is the closest existing
 * check, but it only proves resolver *configuration* propagates from
 * configd over a real DHCP lease (itself blocked on DAR-316's SD-card
 * write failure) - it never sends a single DNS packet.
 *
 * This binary sidesteps the DHCP/configd dependency entirely: with no
 * DHCP lease and no reachable configd dnsinfo service, __res_vinit()
 * (real third_party/libresolv/res_init.c:1163) falls through to
 * res_vinit_from_file(), which reads a real, static /etc/resolv.conf -
 * a real, unmodified, always-compiled code path, not a special test
 * mode. tools/init_binary/inject_into_sd_image.sh writes
 * "nameserver 10.0.2.3" there (QEMU usermode networking's built-in DNS
 * proxy, reachable with zero extra QEMU flags - see runvm.sh's
 * `-netdev user` line).
 *
 * Calls, in order, printing a marker before and after each (same
 * hang-vs-NULL distinguishing convention as res_init_dnsinfo_smoketest.c
 * - a real reachable-but-slow nameserver could hang inside res_send()
 * for the full resolver timeout, which should show as a real delay
 * between BEFORE/AFTER markers in the boot log, not silence):
 *   1. getaddrinfo(argv[1] ?: "example.com", ...) - the real function
 *      ifconfig.macho now links from libinfo.dylib.
 *   2. gethostbyname2(name, AF_INET) - the real function ping.macho now
 *      links from libinfo.dylib (its two real call sites, ping.c's
 *      target-address and -S source parsing, both go through this).
 * Prints the resolved IPv4 address (inet_ntop) from each on success, or
 * h_errno/hstrerror()/gai_strerror() on failure - real libresolv.dylib
 * exports for both (confirmed via nm before this file was written).
 */
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

int main(int argc, char **argv) {
    const char *name = (argc > 1) ? argv[1] : "example.com";
    char addrbuf[INET_ADDRSTRLEN];

    printf("dns_getaddrinfo_smoketest: resolving \"%s\"\n", name);

    printf("BEFORE getaddrinfo\n");
    fflush(stdout);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo *res = NULL;
    int gai_rc = getaddrinfo(name, NULL, &hints, &res);
    printf("AFTER getaddrinfo rc=%d\n", gai_rc);
    if (gai_rc != 0) {
        printf("getaddrinfo FAIL: %s\n", gai_strerror(gai_rc));
    } else {
        int shown = 0;
        for (struct addrinfo *ai = res; ai != NULL; ai = ai->ai_next) {
            if (ai->ai_family != AF_INET) {
                continue;
            }
            struct sockaddr_in *sin = (struct sockaddr_in *)(void *)ai->ai_addr;
            if (inet_ntop(AF_INET, &sin->sin_addr, addrbuf, sizeof(addrbuf)) != NULL) {
                printf("getaddrinfo PASS: %s -> %s\n", name, addrbuf);
                shown = 1;
            }
        }
        if (!shown) {
            printf("getaddrinfo FAIL: rc=0 but no AF_INET result found\n");
        }
        freeaddrinfo(res);
    }

    printf("BEFORE gethostbyname2\n");
    fflush(stdout);
    struct hostent *hp = gethostbyname2(name, AF_INET);
    printf("AFTER gethostbyname2\n");
    if (hp == NULL) {
        printf("gethostbyname2 FAIL: h_errno=%d (%s)\n", h_errno, hstrerror(h_errno));
    } else if (hp->h_addr_list[0] == NULL) {
        printf("gethostbyname2 FAIL: hostent with no addresses\n");
    } else if (inet_ntop(AF_INET, hp->h_addr_list[0], addrbuf, sizeof(addrbuf)) == NULL) {
        printf("gethostbyname2 FAIL: inet_ntop failed on resolved address\n");
    } else {
        printf("gethostbyname2 PASS: %s -> %s\n", name, addrbuf);
    }

    printf("dns_getaddrinfo_smoketest: done\n");
    return 0;
}
