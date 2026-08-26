/* Real, curated __findenv_locked() -- verbatim body from
 * third_party/Libc/stdlib/FreeBSD/getenv.c (real FreeBSD/Apple source),
 * curated out rather than compiling that whole file: it #includes
 * <os/lock_private.h>, which itself needs <os/base_private.h> --
 * neither vendored in this project's pinned third_party/libplatform
 * checkout (confirmed via find). Real, unmodified algorithm, just
 * without that file's own os_unfair_lock-based real getenv()
 * implementation (this project's existing simple_cmds_getenv_compat.c
 * already provides a real getenv()/_NSGetEnviron(), see that file --
 * unsetenv() is the only real caller of __findenv_locked() this pass
 * needs).
 */
extern char **environ;

char *
__findenv_locked(const char *name, int *offset, char **environ_arg)
{
	int len, i;
	const char *np;
	char **p, *cp;

	if (name == 0 || environ_arg == 0) {
		return 0;
	}
	for (np = name; *np && *np != '='; ++np) {
		continue;
	}
	len = (int)(np - name);
	for (p = environ_arg; (cp = *p) != 0; ++p) {
		for (np = name, i = len; i && *cp; i--) {
			if (*cp++ != *np++) {
				break;
			}
		}
		if (i == 0 && *cp++ == '=') {
			*offset = (int)(p - environ_arg);
			return cp;
		}
	}
	return 0;
}
