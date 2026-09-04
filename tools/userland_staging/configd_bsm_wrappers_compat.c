/* Real, narrowly-extracted copy of exactly the two real functions
 * configd.tproj/session.c actually calls out of third_party/BSM's real
 * bsm/lib/lib_wrappers.c (audit_token_to_au32() directly; its own real
 * audit_token_to_au32() body unconditionally references
 * audit_set_terminal_host() too, though every real configd.tproj call
 * site passes tidp == NULL so it's never actually invoked at runtime) --
 * NOT the whole file, same "narrowly-extracted forwarding bodies copied
 * from the pinned source" precedent this ticket's own prior session
 * (2026-09-04, libSystemConfiguration.dylib pass) already established for
 * mach_port_get_refs/mach_port_names out of xnu-7195/libsyscall/mach/
 * mach_port.c, for the same reason: compiling the WHOLE real
 * lib_wrappers.c pulls in audit_write()/audit_write_success()/
 * audit_write_failure(), which need the real au_open/au_to_me/
 * au_to_subject32/au_to_return32/au_to_text/au_write/au_free_token/
 * au_close BSM token-writing API -- a real, much larger, genuinely
 * separate part of libbsm this project has never built and configd.tproj
 * itself never calls (confirmed: `grep -c audit_write configd.tproj/*.c`
 * is zero). Body copied verbatim, zero fabrication -- both functions'
 * real source is third_party/BSM/bsm/lib/lib_wrappers.c lines 101-115
 * (audit_set_terminal_host) and 322-353 (audit_token_to_au32).
 */
#include <sys/types.h>
#include <sys/sysctl.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <bsm/libbsm.h>

int
audit_set_terminal_host(u_int32_t *m)
{
	int name[2] = { CTL_KERN, KERN_HOSTID };
	size_t len;

	if (m == NULL)
		return kAUBadParamErr;
	*m = 0;
	len = sizeof(*m);
	if (sysctl(name, 2, m, &len, NULL, 0) != 0) {
		syslog(LOG_ERR, "sysctl() failed (%s)", strerror(errno));
		return kAUSysctlErr;
	}
	return kAUNoErr;
}

void
audit_token_to_au32(
	audit_token_t	atoken,
	uid_t		*auidp,
	uid_t		*euidp,
	gid_t		*egidp,
	uid_t		*ruidp,
	gid_t		*rgidp,
	pid_t		*pidp,
	au_asid_t	*asidp,
	au_tid_t	*tidp)
{
	if (auidp != NULL)
		*auidp = (uid_t)atoken.val[0];
	if (euidp != NULL)
		*euidp = (uid_t)atoken.val[1];
	if (egidp != NULL)
		*egidp = (gid_t)atoken.val[2];
	if (ruidp != NULL)
		*ruidp = (uid_t)atoken.val[3];
	if (rgidp != NULL)
		*rgidp = (gid_t)atoken.val[4];
	if (pidp != NULL)
		*pidp = (pid_t)atoken.val[5];
	if (asidp != NULL)
		*asidp = (au_asid_t)atoken.val[6];
	if (tidp != NULL) {
		audit_set_terminal_host(&tidp->machine);
		tidp->port = (dev_t)atoken.val[7];
	}
}
