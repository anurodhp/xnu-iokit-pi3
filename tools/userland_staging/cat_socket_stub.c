/* Real, unmodified Apple text_cmds/cat/cat.c has one feature this
 * project can't support yet: reading from a real AF_UNIX domain socket
 * special file (its udom_open()/raw_cat() path, `cat -u` against a
 * socket) needs real socket()/connect()/shutdown() -- genuine networking
 * syscalls, Phase 6, not built (see docs/roadmap.md's Phase 6 scope and
 * this project's other "no real network stack yet" stand-ins, e.g.
 * SMSC9514Ethernet's currently-unreachable driver code). That code path
 * is never reached for cat's ordinary regular-file/stdin use (confirmed:
 * udom_open() is only called when stat() on the argument reports
 * S_IFSOCK, which nothing on this target's real HFS+ root can ever be --
 * no socket special files exist without a real network stack to create
 * one), so these exist purely to satisfy the linker -- same "stub the
 * symbol so it links, document why it can't fire" pattern as
 * ps_task_info_stub.c's mach_state_order(). Honest ENOTSOCK/ENOSYS
 * failures if ever somehow reached, never a fabricated success. */
#include <errno.h>

int socket(int domain, int type, int protocol) {
    (void)domain; (void)type; (void)protocol;
    errno = ENOSYS;
    return -1;
}

int connect(int fd, const void *addr, unsigned int addrlen) {
    (void)fd; (void)addr; (void)addrlen;
    errno = ENOSYS;
    return -1;
}

int shutdown(int fd, int how) {
    (void)fd; (void)how;
    errno = ENOSYS;
    return -1;
}
