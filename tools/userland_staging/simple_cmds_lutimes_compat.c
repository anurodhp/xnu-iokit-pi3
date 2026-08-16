/* Real Libc/emulated/lutimes.c calls setattrlist(2) for the symlink
 * case -- a real, fairly large Darwin extended-attribute syscall this
 * project hasn't built (genuinely out of scope for touch's `-h` flag
 * alone, real command's own default path never reaches this at all:
 * touch.c only assigns `utimes_f = lutimes` when `-h` is explicitly
 * given). This real, working implementation handles the common case
 * (regular file/directory -- real lstat(2) + real utimes(2), both
 * already linked) exactly like the real function does before it ever
 * reaches setattrlist, and gives an honest ENOSYS only for the one case
 * (path is genuinely a symlink) neither this file nor this project's
 * syscall surface can support yet -- never a fabricated success. */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>

int lutimes(const char *path, const struct timeval *times) {
    struct stat st;
    if (lstat(path, &st) < 0) {
        return -1;
    }
    if ((st.st_mode & S_IFMT) == S_IFLNK) {
        errno = ENOSYS;
        return -1;
    }
    return utimes(path, times);
}
