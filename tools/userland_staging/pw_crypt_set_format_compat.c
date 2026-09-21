/* crypt_set_format(3) -- real, narrow, cited compat for pw(8) (DAR-341).
 *
 * pw_user.c calls login_setcryptfmt(lc, "sha512", NULL) at 3 real call
 * sites (real, unmodified lib/libutil/login_crypt.c, pulled via
 * setup_third_party.sh's FREEBSD_DEST block -- see that script's own
 * comment for the citation), which in turn calls real
 * crypt_set_format(cipher). This project's real crypt(3)
 * (third_party/Libc/gen/crypt.c, Apple's own unmodified DES-only
 * implementation, already used project-wide by login/passwd/chpass/
 * sshd -- see build_login.sh's own header) has no crypt_set_format() at
 * all: it is a single fixed algorithm, not real FreeBSD's own
 * lib/libcrypt/crypt.c multi-format dispatcher.
 *
 * Real FreeBSD lib/libcrypt/crypt.c's own crypt_set_format() (checked
 * directly against this project's pinned third_party/freebsd-src @
 * release/13.2.0) is exactly this:
 *
 *     int
 *     crypt_set_format(const char *format)
 *     {
 *         const struct crypt_format *cf;
 *         for (cf = crypt_formats; cf->name != NULL; ++cf) {
 *             if (strcasecmp(cf->name, format) == 0) {
 *                 crypt_format = cf;
 *                 return (1);
 *             }
 *         }
 *         return (0);
 *     }
 *
 * i.e. a case-insensitive linear search of a registered {name, hash-fn}
 * table, returning 1 only on an exact name match and 0 otherwise -- real
 * FreeBSD callers (this file's own real caller included) are written to
 * treat a 0 return as an ordinary, non-fatal "requested format
 * unavailable" outcome, not an error condition. Below implements that
 * exact real contract, restricted to the one format this project's
 * crypt(3) actually has ("des") -- there is no {name,func} table to
 * search because there is only the one, real, already-shipped algorithm.
 *
 * This is NOT a behavior downgrade: real upstream pw_user.c already
 * treats login_setcryptfmt()'s own NULL return (which follows directly
 * from crypt_set_format() returning 0) as non-fatal -- `warn("setting
 * crypt(3) format")`, not exit -- and falls through to a plain
 * `crypt(password, salt)` call regardless of whether the format request
 * succeeded (see pw_pwcrypt(), pw_user.c). So pw(8)-created accounts get
 * exactly the same DES-crypt hash format every other account-management
 * tool in this project already reads and writes to /etc/master.passwd
 * (chpass/passwd/login) -- keeping cross-tool password verification
 * intact, which a real sha512-crypt hash from a naively-multi-format
 * pw(8) would have silently broken (this project's crypt(3) can't verify
 * a format it can't produce).
 */
#include <stddef.h>
#include <strings.h>

int
crypt_set_format(const char *format)
{
	if (format != NULL && strcasecmp(format, "des") == 0)
		return (1);
	return (0);
}
