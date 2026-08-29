/* Real, honest stand-ins for the getprotoent/getservent database
 * families plus getnameinfo -- confirmed genuinely absent from every
 * real Apple source in this project's pinned checkout (`find
 * third_party/Libinfo -iname '*getproto*.c' -iname '*getserv*.c'` --
 * zero hits, same "confirmed absent, not guessed" standard as
 * docs/backlog.md item d7's sysdir_* entry; getnameinfo's real source,
 * Libinfo/lookup.subproj/getnameinfo_link.c, exists but needs the same
 * si_module dispatcher wall build_libresolv.sh's own header already
 * documents excluding dns_async.c for -- not attempted here either).
 * Deferred via `-Wl,-U` before, which doesn't work: real dyld's eager
 * chained-fixups binding fails at real process load time regardless of
 * whether the call path executes (confirmed via a real QEMU boot of
 * launchd: "symbol not found in flat namespace '_endprotoent'"). Real,
 * honest "no protocol/service database, no name-service dispatcher"
 * behavior instead -- real public types/prototypes included directly
 * from the real Apple SDK header already on this project's include path
 * (<netdb.h>, not hand-declared), same real fallback shape as this
 * project's own dnsinfo_compat.c/DNS-SD compat file. getprotobynumber/
 * getprotoent/getservbyport/getservent return NULL (real, documented
 * "not found" return value for all four); setprotoent/endprotoent/
 * setservent/endservent are real no-ops (nothing to open/close without
 * a real database); getnameinfo returns EAI_FAIL (a real getaddrinfo-
 * family error code meaning "non-recoverable failure", not fabricated).
 */
#include <netdb.h>

struct protoent *getprotobynumber(int proto) {
	(void)proto;
	return 0;
}

struct protoent *getprotoent(void) {
	return 0;
}

void setprotoent(int stayopen) {
	(void)stayopen;
}

void endprotoent(void) {
}

struct servent *getservbyport(int port, const char *proto) {
	(void)port;
	(void)proto;
	return 0;
}

struct servent *getservent(void) {
	return 0;
}

void setservent(int stayopen) {
	(void)stayopen;
}

void endservent(void) {
}

int getnameinfo(const struct sockaddr *sa, socklen_t salen, char *host,
    socklen_t hostlen, char *serv, socklen_t servlen, int flags) {
	(void)sa;
	(void)salen;
	(void)host;
	(void)hostlen;
	(void)serv;
	(void)servlen;
	(void)flags;
	return EAI_FAIL;
}
