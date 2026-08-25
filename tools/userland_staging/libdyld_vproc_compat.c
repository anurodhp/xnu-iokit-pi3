#include <stdint.h>

/* Real gap: see dyld_compat/vproc_priv.h's own comment. This port has no
 * launchd at all -- the correct, honest answer to "is this process
 * launchd-managed" (LibSystemHelpers::isLaunchdOwned(), the only real
 * caller) is always no. Returning NULL (success, vproc_err_t) with
 * *outval=0 matches real vproc_swap_integer()'s documented success
 * contract for an unmanaged process. */
void *vproc_swap_integer(void *vp, unsigned int key, int64_t *inval, int64_t *outval) {
    (void)vp;
    (void)key;
    (void)inval;
    if (outval) {
        *outval = 0;
    }
    return 0;
}
