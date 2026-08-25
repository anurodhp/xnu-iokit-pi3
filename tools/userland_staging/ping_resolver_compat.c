/* Real ping(8) has exactly two call sites for hostname resolution
 * (main()'s `-S source` and target-address parsing, lines ~646/676 of
 * third_party/network_cmds/ping.tproj/ping.c) -- both already try
 * inet_aton() FIRST and only fall through to gethostbyname2() if the
 * argument doesn't parse as a numeric dotted-quad. Real gethostbyname2()
 * on real Apple/Darwin resolves through Libinfo's si_module dispatcher
 * (si_getaddrinfo.c) into a real, separate mDNSResponder/lookupd-
 * equivalent daemon process this project has never built -- the exact
 * same real, already-documented architectural gap build_libresolv.sh's
 * own header comment describes for dns.c/dns_async.c ("a real,
 * mach-IPC-based, much larger subsystem ... genuinely out of scope
 * here"). No source file implementing gethostbyname2() exists anywhere
 * in this project's third_party/Libinfo or third_party/libresolv checkouts
 * (confirmed by grep across both trees, not guessed) -- it is not a
 * missing single function, but the entry point to that whole subsystem.
 *
 * Real, honest "no resolver available" degradation here, same class of
 * gap as ifconfig_resolver_compat.c's own gethostbyname() stub (which
 * this file's shape directly mirrors) and ps's real Mach IPC stand-in
 * (ps_task_info_stub.c): sets h_errno = HOST_NOT_FOUND and returns NULL,
 * which is exactly the real, already-correct error path ping.c's own
 * two call sites already handle (`if (!hp) errx(EX_NOHOST, "cannot
 * resolve %s: %s", ..., hstrerror(h_errno));`) -- not fabricated
 * success. This target's real numeric-IP-only usage (Phase 6b) never
 * exercises the hostname path in practice.
 *
 * h_errno/hstrerror/herror themselves are NOT stubbed here -- real,
 * unmodified Apple source (third_party/Libinfo/dns.subproj/herror.c) is
 * compiled directly into ping.macho instead (see build_ping.sh), so
 * this file only needs to set the real h_errno global that source
 * already exports.
 */
#include <netdb.h>

struct hostent *gethostbyname2(const char *name, int af) {
    (void)name;
    (void)af;
    h_errno = HOST_NOT_FOUND;
    return (struct hostent *)0;
}

/* gethostbyaddr(): pr_addr()'s reverse-DNS hostname lookup for reply
 * source addresses (only reached without -n). Same architectural gap as
 * gethostbyname2() above -- real reverse lookup needs the identical
 * Libinfo si_module dispatcher, never built here. Honest "no resolver"
 * degradation, matching pr_addr()'s own already-correct fallback (prints
 * the numeric address when hp is NULL). */
struct hostent *gethostbyaddr(const void *addr, unsigned int len, int af) {
    (void)addr;
    (void)len;
    (void)af;
    h_errno = HOST_NOT_FOUND;
    return (struct hostent *)0;
}
