/* No-op replacement for third_party/sudo/src/utmp.c (DAR-340).
 *
 * Why not compile the real src/utmp.c: with HAVE_GETUTXID it needs
 * setutxent/getutxid/getutxline/endutxent, none of which any shipped dylib
 * exports (nm -gU of libsystem_c.dylib), and the only Libc source that
 * provides them (third_party/Libc/gen/NetBSD/utmpx.c) drags in
 * utmpx-darwin.c's ASL client and notify_post -- see build_login.sh's
 * EXTRA_FILES -- far more than a first sudo build should take on. Without
 * HAVE_UTMPX_H, src/utmp.c:36 falls to `#include <utmp.h>`, which the
 * iPhoneOS SDK does not ship.
 *
 * Why a no-op is behaviour-safe: both callers ignore the return value
 * (src/exec_pty.c:161 utmp_login, :128 and :885 utmp_logout), and the only
 * effect lost is the `who`-visible utmpx record for a sudo-allocated pty,
 * which plugins/sudoers/defaults.c:610 (def_set_utmp = true) would
 * otherwise request. Prototypes: src/sudo_exec.h:111-113.
 *
 * Follow-up (not done here): link the real Libc utmpx pair the way
 * build_login.sh does and compile the real src/utmp.c instead.
 */
#include <stdbool.h>

bool
utmp_login(const char *from_line, const char *to_line, int ttyfd,
    const char *user)
{
    (void)from_line; (void)to_line; (void)ttyfd; (void)user;
    return false;
}

bool
utmp_logout(const char *line, int status)
{
    (void)line; (void)status;
    return false;
}
