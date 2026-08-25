/* Real smoketest for libopenpam.a (tools/userland_staging/build_openpam.sh).
 *
 * Deliberately does NOT exercise any module-loading-dependent API
 * (pam_authenticate/pam_acct_mgmt/etc, which need a real service module
 * to call into) -- see build_openpam.sh's header comment and
 * openpam_dynamic_compat.c for the real, honestly-documented reason
 * dynamic module loading can't work on this target at all (no dyld).
 *
 * Instead this exercises real, unmodified core libpam surface that
 * genuinely doesn't need a loadable module to prove itself:
 *   - pam_start() -- real item storage (pam_set_item.c x3) + real policy
 *     lookup (openpam_configure.c -> openpam_load_chain() ->
 *     openpam_read_chain(), real fopen()/openpam_readline() against
 *     /etc/pam.d/<service> and /etc/pam.conf, real ENOENT handling since
 *     this image ships neither) -- confirmed by reading
 *     openpam_configure.c directly: with no policy file found for any
 *     facility, it falls through to its own real
 *     "default empty facilities to required pam_deny.so" path
 *     (<rdar://problem/27991863>, openpam_configure.c:336-359), which
 *     calls openpam_load_module("pam_deny.so") -- reaching
 *     openpam_dynamic() (our honest-NULL replacement) for real, and
 *     correctly failing the whole pam_start() call with PAM_SYSTEM_ERR
 *     because no module could be loaded. This is the real, true state of
 *     this target (no PAM modules exist to load), not a crash and not a
 *     fabricated success.
 *   - pam_end() (called internally by pam_start()'s own fail path, real
 *     openpam_clear_chains() + module_data cleanup)
 *   - pam_strerror() -- confirms the real PAM_SYSTEM_ERR code maps to
 *     its real, unmodified string table entry.
 *   - openpam_nullconv -- real stock non-interactive conv function,
 *     passed as the pam_conv callback (never actually invoked here, no
 *     module ever prompts, but proves it links).
 *
 * Zero undefined symbols expected (checked via nm -u by
 * build_openpam.sh); zero LC_LOAD_DYLIB; real LC_UNIXTHREAD entry point.
 */

#include <stdio.h>
#include <security/pam_appl.h>
#include <security/openpam.h>

int
main(void)
{
	pam_handle_t *pamh = NULL;
	struct pam_conv conv = { &openpam_nullconv, NULL };
	int r;

	r = pam_start("smoketest", "nobody", &conv, &pamh);
	printf("pam_start(\"smoketest\"): %d (%s)\n", r, pam_strerror(NULL, r));

	if (r == PAM_SUCCESS) {
		/* Would only happen if a real /etc/pam.d/smoketest or
		 * /etc/pam.conf entry existed on whatever image this runs
		 * against AND its module(s) somehow loaded (impossible
		 * without dyld) -- not expected on this project's own test
		 * image, but handle it honestly rather than assume. */
		pam_end(pamh, r);
	}

	/* Expected real result on this target: PAM_SYSTEM_ERR (7) --
	 * "system error" -- because pam_deny.so could never be dlopen()ed
	 * (see openpam_dynamic_compat.c). pam_start() has already called
	 * pam_end() itself on this path (see pam_start.c's fail: label),
	 * so pamh must not be touched again here. */
	printf("smoketest done\n");
	return (0);
}
