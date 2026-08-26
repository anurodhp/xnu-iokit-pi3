/* Real, curated __darwin_check_fd_set_overflow() -- bounds-check logic
 * copied verbatim from
 * third_party/xnu-7195/libsyscall/wrappers/terminate_with_reason.c (real
 * Apple source; this is the function the SDK's own <sys/_types/_fd_def.h>
 * FD_SET/FD_CLR/FD_ISSET/FD_ZERO macros call when `n` might exceed
 * __DARWIN_FD_SETSIZE). Curated rather than compiling that file whole:
 * the real body's out-of-bounds path reports via
 * os_fault_with_payload()/pthread_current_stack_contains_np() (a real
 * crash-reporter payload + stack-bounds probe this target has no
 * exception-port pipeline for -- same reasoning as chk_fail_compat.c's
 * os_crash() swap). The in-bounds fast path (the one every correct caller
 * actually takes) is untouched; the two overflow/underflow paths call
 * plain abort() instead.
 */
#include <sys/types.h>
#include <stdlib.h>

#define __DARWIN_FD_SETSIZE 1024

int
__darwin_check_fd_set_overflow(int n, const void *fd_set, int unlimited_select)
{
	(void)fd_set;

	if (n < 0) {
		abort();
	}

	if (n >= __DARWIN_FD_SETSIZE) {
		if (!unlimited_select) {
			abort();
		}
		return 1;
	}

	return 1;
}
