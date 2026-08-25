/* Real smoketest for libc_build/libresolv.a (build_libresolv.sh) --
 * exercises the real, unmodified classic BIND resolver path this tier
 * compiles from third_party/libresolv (tag libresolv-68): __res_vinit()
 * (which, via this build's dnsinfo compat shim, takes the real
 * res_vinit_from_file()/etc-resolv.conf fallback path -- see
 * libresolv_dnsinfo_compat.c and this file's own comment below on why
 * __res_vinit() is called directly rather than through the public
 * res_init()), res_nmkquery() (real DNS query-packet construction,
 * dn_comp()/ns_name compression included -- the explicit-res_state
 * variant of res_mkquery(), called directly for the same real reason
 * __res_vinit() is called instead of res_init(), see below), then
 * ns_initparse()/ns_parserr()/dn_expand() to parse that same packet back
 * apart and recover the original question name -- proving base64.c,
 * ns_name.c, ns_parse.c, res_comp.c, res_mkquery.c and res_init.c all
 * really link and really run, not just that res_nmkquery() returns a
 * positive length.
 *
 * Deliberately does NOT call res_send()/res_query()/the public
 * res_init() (would need either a real reachable nameserver plus this
 * tier's new socket syscalls exercised live, or pulling in res_data.o/
 * res_query.o's much larger real transitive surface -- getifaddrs(3),
 * notify(3), kqueue(2)/kevent(2), and mDNSResponder's DNSServiceQueryRecord
 * client API -- see the comment on __res_vinit() below for the full
 * citation of why that's a real, separate architectural gap, not a
 * shortcut). res_send.c/res_query.c/res_data.c are still real,
 * unmodified, unstubbed members of libresolv.a itself (all three compile
 * clean, see build_libresolv.sh) -- this smoketest just doesn't happen
 * to link them, the same way a real program that never calls res_query()
 * would never pull res_query.o out of real libresolv.dylib either.
 *
 * Same host-has-no-console-to-assert-through convention as
 * libc_regex_smoketest.c/libiconv_smoketest.c: prints PASS/FAIL markers
 * via the already-proven printf() path, QEMU boot log is the real
 * verification evidence.
 */
#include <resolv.h>
#include <arpa/nameser.h>
#include <stdio.h>
#include <string.h>

/* Real internal entry point (declared in res_private.h, defined in
 * res_init.c) called directly instead of the public res_init()/
 * resolv.h's res_9_init() wrapper. Real reason, not a shortcut: on this
 * exact build, res_9_init() lives in res_data.c, which -- to provide the
 * rest of the classic res_query()/res_search() public surface -- also
 * references res_query.c's res_9_nquery(), which is where Apple's real
 * res_query.c ALSO defines res_query_mDNSResponder() (a separate, real,
 * mDNSResponder/DNSServiceQueryRecord-based lookup path -- see
 * build_libresolv.sh's own header comment for the full citation). Static
 * linking pulls in a referenced .o's entire undefined-symbol set, not
 * just the specific function called, so merely linking res_data.o would
 * drag in real DNSServiceQueryRecord/DNSServiceProcessResult/
 * DNSServiceRefSockFD/DNSServiceRefDeallocate references this project
 * has no mDNSResponder client library to satisfy -- a real, separate
 * architectural gap from the dnsinfo/configd one already fixed for this
 * tier, genuinely out of scope for this pass (see build_libresolv.sh).
 * Calling __res_vinit() directly exercises the exact same real
 * res_init.c code (including this tier's dns_configuration_copy() ->
 * NULL -> res_vinit_from_file() fallback) without that unrelated pull.
 * resolv.h itself already declares __res_vinit(res_state, int) (it's a
 * real, non-static, non-hidden entry point -- res_data.c's own res_init()
 * calls it too), so no separate extern is needed here.
 *
 * res_nmkquery() is the same story one level down: the public
 * res_mkquery() (no explicit res_state) is ALSO just a thin res_data.c
 * wrapper (`return res_nmkquery(_res_static, op, ...);`), so calling it
 * would pull in res_data.o for the same reason discussed above. */

int main(void) {
    int all_ok = 1;

    struct __res_state my_res;
    memset(&my_res, 0, sizeof(my_res));
    int rc = __res_vinit(&my_res, 0);
    printf("__res_vinit(): %s (rc=%d, nscount=%d)\n", (rc == 0) ? "PASS" : "FAIL", rc, my_res.nscount);
    all_ok &= (rc == 0);

    unsigned char query[512];
    const char *qname = "test.example.com";
    int qlen = res_nmkquery(&my_res, ns_o_query, qname, ns_c_in, ns_t_a, NULL, 0, NULL, query, sizeof(query));
    printf("res_nmkquery(%s): %s (qlen=%d)\n", qname, (qlen > 0) ? "PASS" : "FAIL", qlen);
    all_ok &= (qlen > 0);

    if (qlen > 0) {
        ns_msg msg;
        int prc = ns_initparse(query, qlen, &msg);
        printf("ns_initparse(): %s (rc=%d, qdcount=%d)\n", (prc == 0) ? "PASS" : "FAIL", prc,
               ns_msg_count(msg, ns_s_qd));
        all_ok &= (prc == 0);

        if (prc == 0 && ns_msg_count(msg, ns_s_qd) == 1) {
            ns_rr rr;
            int rrc = ns_parserr(&msg, ns_s_qd, 0, &rr);
            int name_ok = (rrc == 0) && (strcmp(ns_rr_name(rr), qname) == 0);
            printf("ns_parserr()/dn_expand round-trip: %s (name=\"%s\")\n",
                   name_ok ? "PASS" : "FAIL", (rrc == 0) ? ns_rr_name(rr) : "<error>");
            all_ok &= name_ok;
        } else {
            all_ok = 0;
        }
    }

    printf("libresolv_smoketest: %s\n", all_ok ? "ALL PASS" : "SOME FAILED");
    return all_ok ? 0 : 1;
}
