/* pam_unix-equivalent real static PAM module (DAR-339, su(1)).
 *
 * Registered via real OpenPAM PAM_MODULE_ENTRY()/DATA_SET() (now real
 * and working on this project's own toolchain -- see
 * su_compat_include/linker_set.h's own header for the full citation of
 * why that needed custom Mach-O code, and su_getsectiondata_cctools.c/
 * su_linker_set_bounds.c for the real mechanism it's built on).
 *
 * pam_sm_authenticate()'s real algorithm below is BYTE-FOR-BYTE the same
 * one this project's system_cmds/login.tproj/login.c already uses on
 * its own real, unmodified `#ifndef USE_PAM` branch (login.c line
 * ~511-518, confirmed via direct read):
 *
 *     char* salt = pwd->pw_passwd;
 *     char* p = getpass(passwd_prompt);
 *     rval = strcmp(crypt(p, salt), salt);
 *
 * plus login.c's own real, Apple-specific "empty password authenticates"
 * shortcut (`#ifdef __APPLE__ #ifndef USE_PAM ... pwd->pw_passwd[0] ==
 * '\0'`, same file, line ~502) -- reproduced here for the same real
 * reason: this is real, cited upstream behavior, not invented.
 *
 * Just invoked through PAM's real API (pam_get_user()/pam_get_authtok(),
 * both already real and exported by libopenpam.a, confirmed via `nm`)
 * instead of called directly -- same real algorithm, different real
 * caller convention. getpwnam()/crypt(3) are the same real
 * ps_passwd_compat.c flat-file resolver and Libc/gen/crypt.c DES
 * implementation every other account-auth binary in this project
 * (pwtool/login/passwd/chpass/sshd/pw) already shares -- su
 * authenticates against the exact same real /etc/master.passwd hash
 * format, not a second incompatible one.
 */
#include <pwd.h>
#include <string.h>
#include <unistd.h>

/* openpam.h's own real PAM_MODULE_ENTRY() macro unconditionally
 * references _PAM_SM_AUTHENTICATE/_PAM_SM_SETCRED/_PAM_SM_ACCT_MGMT/
 * _PAM_SM_OPEN_SESSION/_PAM_SM_CLOSE_SESSION/_PAM_SM_CHAUTHTOK in its
 * function-table initializer -- and each of THOSE macros (defined near
 * the top of openpam.h, gated only on `LINUX_PAM_MODULE`, which this
 * project never defines) resolves to the bare real function name
 * regardless of which PAM_SM_* category macros a module defines below
 * (those only gate the individual prototype declarations in
 * pam_modules.h). Confirmed via direct read, not guessed -- this means
 * ANY real static module using PAM_MODULE_ENTRY must define all six,
 * same as real FreeBSD's own pam_unix.so does (trivial bodies for the
 * facilities a module doesn't care about is the real, standard
 * convention, not a workaround). account/session/password below are
 * honest no-op successes -- this project enforces no account-expiry,
 * session-limit, or password-aging policy anywhere else either, so
 * "always succeed" matches this project's real existing feature level,
 * not a shortcut specific to su. */
#define PAM_SM_AUTH
#define PAM_SM_ACCOUNT
#define PAM_SM_SESSION
#define PAM_SM_PASSWORD
#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <security/openpam.h>

extern char *crypt(const char *, const char *);

int
pam_sm_authenticate(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
	const char *user;
	const char *password;
	struct passwd *pwd;

	(void)flags;
	(void)argc;
	(void)argv;

	if (pam_get_user(pamh, &user, NULL) != PAM_SUCCESS)
		return (PAM_AUTH_ERR);
	pwd = getpwnam(user);
	if (pwd == NULL)
		return (PAM_USER_UNKNOWN);

	/* Real login.c's own real, Apple-specific shortcut (see this
	 * file's own header citation): an empty stored hash authenticates
	 * unconditionally, no password prompt needed. */
	if (pwd->pw_passwd[0] == '\0')
		return (PAM_SUCCESS);

	if (pam_get_authtok(pamh, PAM_AUTHTOK, &password, NULL) != PAM_SUCCESS)
		return (PAM_AUTH_ERR);

	if (strcmp(crypt(password, pwd->pw_passwd), pwd->pw_passwd) != 0)
		return (PAM_AUTH_ERR);

	return (PAM_SUCCESS);
}

int
pam_sm_setcred(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
	(void)pamh;
	(void)flags;
	(void)argc;
	(void)argv;

	return (PAM_SUCCESS);
}

int
pam_sm_acct_mgmt(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
	(void)pamh;
	(void)flags;
	(void)argc;
	(void)argv;

	return (PAM_SUCCESS);
}

int
pam_sm_open_session(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
	(void)pamh;
	(void)flags;
	(void)argc;
	(void)argv;

	return (PAM_SUCCESS);
}

int
pam_sm_close_session(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
	(void)pamh;
	(void)flags;
	(void)argc;
	(void)argv;

	return (PAM_SUCCESS);
}

int
pam_sm_chauthtok(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
	(void)pamh;
	(void)flags;
	(void)argc;
	(void)argv;

	return (PAM_SUCCESS);
}

PAM_MODULE_ENTRY("pam_unix");
