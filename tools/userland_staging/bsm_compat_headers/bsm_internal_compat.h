#ifndef BSM_INTERNAL_COMPAT_H
#define BSM_INTERNAL_COMPAT_H
/* Real, force-included compat shim for building apple-oss-distributions/BSM
 * (tag bsm-13) against this project's pinned xnu-7195. See
 * build_bsm.sh's own header comment for the full writeup -- every mapping
 * here was found by real trial compile (not guessed), and every real
 * numeric/semantic pairing was cross-checked against xnu-7195's own
 * bsd/bsm/audit_record.h before being added.
 *
 * bsm_audit.c uses struct au_token/au_record (TAILQ_ENTRY-based) without
 * including their real declaring header itself (bsm/audit_internal.h,
 * present in this project's xnu-7195 pin) -- on real Apple builds this
 * apparently arrives transitively via a private SDK header-search path
 * this project doesn't replicate. sys/types.h/sys/queue.h are pulled in
 * first since audit_internal.h uses TAILQ_ENTRY/TAILQ_HEAD without
 * including sys/queue.h itself.
 */
#include <sys/types.h>
#include <sys/queue.h>
#include <bsm/audit_internal.h>

/* libbsm.h's own au_groups_t typedef uses a bare MAX_GROUPS that is
 * never defined anywhere in this repo at all (grepped the whole BSM
 * checkout, zero hits) -- a real, confirmed bug/omission in BSM-13's own
 * published libbsm.h, not a project-specific gap. bsm/audit_record.h
 * (included further down libbsm.h's own chain, before this macro is
 * actually used) defines the real, semantically-equivalent
 * AUDIT_MAX_GROUPS -- this is a macro-to-macro alias, so it's fine that
 * AUDIT_MAX_GROUPS itself isn't defined yet at this exact point in the
 * translation unit. */
#define MAX_GROUPS AUDIT_MAX_GROUPS

/* Real, confirmed naming drift between BSM-13's userland lib source and
 * this project's paired xnu-7195 bsd/bsm/audit_internal.h: the latter
 * renamed HEADER_SIZE/TRAILER_SIZE to AUDIT_HEADER_SIZE/AUDIT_TRAILER_SIZE
 * at some point after BSM-13's own bsm_audit.c was last synced -- same
 * real value, just a different macro name. */
#ifndef HEADER_SIZE
#define HEADER_SIZE AUDIT_HEADER_SIZE
#endif
#ifndef TRAILER_SIZE
#define TRAILER_SIZE AUDIT_TRAILER_SIZE
#endif

/* Real, confirmed naming drift between BSM-13's bsm_token.c/bsm_io.c/
 * bsm_event.c (AU_*_TOKEN / AU_NULL / AUR_LONG naming convention) and
 * this project's paired xnu-7195 bsd/bsm/audit_record.h (AUT_* / AUR_INT32
 * convention) -- confirmed 1:1 by cross-referencing each constant's real
 * numeric value and call-site semantics against audit_record.h, not
 * guessed. AU_HEADER_VERSION/TRAILER_PAD_MAGIC are the same real drift.
 */
#define AU_NULL                 AUT_INVALID
#define AU_ARG32_TOKEN          AUT_ARG32
#define AU_ARG64_TOKEN          AUT_ARG64
#define AU_ATTR32_TOKEN         AUT_ATTR32
#define AU_EXEC_ARG_TOKEN       AUT_EXEC_ARGS
#define AU_EXEC_ENV_TOKEN       AUT_EXEC_ENV
#define AU_EXIT_TOKEN           AUT_EXIT
#define AU_FILE_TOKEN           AUT_OTHER_FILE32
#define AU_HEADER_32_TOKEN      AUT_HEADER32
#define AU_IN_ADDR_EX_TOKEN     AUT_IN_ADDR_EX
#define AU_IN_ADDR_TOKEN        AUT_IN_ADDR
#define AU_IP_TOKEN              AUT_IP
#define AU_IPC_TOKEN             AUT_IPC
#define AU_IPCPERM_TOKEN         AUT_IPC_PERM
#define AU_IPORT_TOKEN           AUT_IPORT
#define AU_NEWGROUPS_TOKEN       AUT_NEWGROUPS
#define AU_OPAQUE_TOKEN          AUT_OPAQUE
#define AU_PATH_TOKEN            AUT_PATH
#define AU_PROCESS_32_EX_TOKEN   AUT_PROCESS32_EX
#define AU_PROCESS_32_TOKEN      AUT_PROCESS32
#define AU_RETURN_32_TOKEN       AUT_RETURN32
#define AU_RETURN_64_TOKEN       AUT_RETURN64
#define AU_SEQ_TOKEN             AUT_SEQ
#define AU_SOCK_EX32_TOKEN       AUT_SOCKET_EX
#define AU_SOCK_INET_128_TOKEN   AUT_SOCKINET128
#define AU_SOCK_INET_32_TOKEN    AUT_SOCKINET32
#define AU_SOCK_TOKEN            AUT_SOCKET
#define AU_SOCK_UNIX_TOKEN       AUT_SOCKUNIX
#define AU_SUBJECT_32_EX_TOKEN   AUT_SUBJECT32_EX
#define AU_SUBJECT_32_TOKEN      AUT_SUBJECT32
#define AU_TEXT_TOKEN            AUT_TEXT
#define AU_TRAILER_TOKEN         AUT_TRAILER
#define AU_ARB_TOKEN             AUT_DATA
#define AUR_LONG                 AUR_INT32
#define AUR_LONG_SIZE            AUR_INT32_SIZE
#define HEADER_VERSION           AUDIT_HEADER_VERSION_OPENBSM
#define TRAILER_PAD_MAGIC        AUT_TRAILER_MAGIC

/* bsm_token.c uses struct in_addr/in6_addr/sockaddr_in/sockaddr_in6/
 * struct ip and AF_INET6 without including their declaring headers
 * itself -- same "relies on a transitive real-SDK include order this
 * project doesn't replicate" pattern as audit_internal.h above. */
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>

/* be16enc/be32enc/be64enc: real, standard BSD <sys/endian.h> API
 * (big-endian byte-order encode into a buffer), called by xnu-7195's own
 * real bsd/bsm/audit_internal.h ADD_U_INT16/32/64 macros. Not present
 * anywhere in this project's vendored Libc (grepped, zero hits in any
 * pinned repo under this name). Per source-priority, a real, standard,
 * unambiguous algorithm -- there is exactly one correct big-endian
 * encoding, no platform variation -- rather than pinning a whole extra
 * repo for 3 one-line functions.
 */
static inline void be16enc(void *pp, uint16_t u) {
    unsigned char *p = (unsigned char *)pp;
    p[0] = (unsigned char)(u >> 8);
    p[1] = (unsigned char)u;
}
static inline void be32enc(void *pp, uint32_t u) {
    unsigned char *p = (unsigned char *)pp;
    p[0] = (unsigned char)(u >> 24);
    p[1] = (unsigned char)(u >> 16);
    p[2] = (unsigned char)(u >> 8);
    p[3] = (unsigned char)u;
}
static inline void be64enc(void *pp, uint64_t u) {
    unsigned char *p = (unsigned char *)pp;
    be32enc(p, (uint32_t)(u >> 32));
    be32enc(p + 4, (uint32_t)u);
}

#endif /* BSM_INTERNAL_COMPAT_H */
