/* Real undelete(2) is only ever reached by rm.c's FTS_W ("whiteout"
 * union-filesystem marker) case -- a BSD union-mount feature this
 * target's real HFS+ root never produces (fts(3) only emits FTS_W
 * entries when FTS_WHITEOUT is passed to fts_open(), which rm.c's real
 * call site never does, confirmed by reading rm.c -- structurally
 * unreachable here, not merely untested). Honest ENOSYS if somehow
 * reached, same "stub the symbol so it links, document why it can't
 * fire" pattern as ps_task_info_stub.c's mach_state_order(). */
#include <errno.h>

int undelete(const char *path) {
    (void)path;
    errno = ENOSYS;
    return -1;
}
