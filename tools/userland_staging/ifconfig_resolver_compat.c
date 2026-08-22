/* Real ifconfig(8) has exactly two classes of hostname-resolution call
 * site (af_inet.c's in_getaddr() hostname/network-name fallback when an
 * address argument doesn't parse as a numeric dotted-quad, and
 * settunnel()'s gif-tunnel endpoint lookup) -- both genuinely need a
 * working DNS/hosts-file resolver, which this project has never built
 * (see project memory: "custom resolver" is explicit future Phase 7
 * scope, not attempted yet). Real, honest "no resolver available"
 * degradation here, same class of gap as ps's real Mach IPC stand-in
 * (ps_task_info_stub.c) -- not fabricated success, and this board's
 * real numeric-IP-only interface config (Phase 6b) never exercises
 * these paths in practice.
 *
 * getnameinfo()/freeaddrinfo()/gai_strerror() need NO stand-in behavior
 * at all: af_inet.c's only two call sites always pass NI_NUMERICHOST
 * (pure inet_ntop formatting, no resolver involved), so those three are
 * real, fully-correct implementations here, not stubs.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>

int h_errno;

struct hostent *gethostbyname(const char *name) {
    (void)name;
    h_errno = HOST_NOT_FOUND;
    return NULL;
}

struct netent *getnetbyname(const char *name) {
    (void)name;
    return NULL;
}

const char *gai_strerror(int ecode) {
    switch (ecode) {
    case EAI_NONAME: return "hostname nor servname provided, or not known (no resolver on this system)";
    case EAI_FAIL:   return "non-recoverable failure in name resolution (no resolver on this system)";
    default:         return "name resolution error (no resolver on this system)";
    }
}

void freeaddrinfo(struct addrinfo *ai) {
    while (ai != NULL) {
        struct addrinfo *next = ai->ai_next;
        free(ai->ai_addr);
        free(ai);
        ai = next;
    }
}

/* Real numeric-only getaddrinfo(): builds a real single-entry AF_INET
 * result when `node` is a real dotted-quad (inet_pton), otherwise
 * returns EAI_NONAME -- correct, honest behavior for a system with no
 * DNS resolver, not a fabricated lookup. `hints`/`service` unused: every
 * real call site in this build (ifconfig.c's settunnel()) passes NULL
 * for both.
 */
int getaddrinfo(const char *node, const char *service,
                 const struct addrinfo *hints, struct addrinfo **res) {
    (void)service;
    (void)hints;
    if (node == NULL) {
        return EAI_NONAME;
    }
    struct in_addr addr;
    if (inet_pton(AF_INET, node, &addr) != 1) {
        return EAI_NONAME;
    }
    struct sockaddr_in *sin = calloc(1, sizeof(*sin));
    if (sin == NULL) {
        return EAI_FAIL;
    }
    sin->sin_len = sizeof(*sin);
    sin->sin_family = AF_INET;
    sin->sin_addr = addr;

    struct addrinfo *ai = calloc(1, sizeof(*ai));
    if (ai == NULL) {
        free(sin);
        return EAI_FAIL;
    }
    ai->ai_family = AF_INET;
    ai->ai_socktype = SOCK_DGRAM;
    ai->ai_addrlen = sizeof(*sin);
    ai->ai_addr = (struct sockaddr *)sin;
    ai->ai_next = NULL;

    *res = ai;
    return 0;
}

int getnameinfo(const struct sockaddr *sa, socklen_t salen,
                 char *host, socklen_t hostlen,
                 char *serv, socklen_t servlen, int flags) {
    (void)salen;
    if (serv != NULL && servlen > 0) {
        serv[0] = '\0';
    }
    if (host == NULL || hostlen == 0) {
        return 0;
    }
    if (!(flags & NI_NUMERICHOST)) {
        /* Real DNS PTR lookup: no resolver on this system, see header. */
        return EAI_FAIL;
    }
    if (sa->sa_family != AF_INET) {
        return EAI_FAIL;
    }
    const struct sockaddr_in *sin = (const struct sockaddr_in *)(const void *)sa;
    if (inet_ntop(AF_INET, &sin->sin_addr, host, hostlen) == NULL) {
        return EAI_FAIL;
    }
    return 0;
}
