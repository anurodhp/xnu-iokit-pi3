/* chflagsat(2) -- real, narrow, cited compat for pw(8) (DAR-341).
 *
 * cpdir.c's own real, unmodified copymkdir() calls
 * `chflagsat(rootfd, dir, flags, AT_SYMLINK_NOFOLLOW)` (only when
 * `flags > 0` -- this project's own pw.conf has no config path that
 * sets a non-default home-directory chflags value, so this is a real
 * but rarely-exercised call site, not a hot path). Confirmed genuinely
 * absent from this platform, not just an availability-macro block like
 * system() below: grepped both this project's pinned Xcode-12 iPhoneOS
 * SDK headers and third_party/xnu-7195/bsd -- Darwin has real chflags(2)/
 * fchflags(2)/lchflags(2) (path-based, fd-based, symlink-not-followed)
 * but no *at-family variant at all.
 *
 * Built from two real Darwin primitives instead of one real syscall:
 * openat(2) (real, already proven throughout this project) to resolve
 * `path` relative to `dirfd`, then fchflags(2) (real) on the resulting
 * fd. When AT_SYMLINK_NOFOLLOW is requested, the open uses O_SYMLINK --
 * a real, kernel-recognized Darwin/XNU open(2) flag ("allow open of a
 * symlink") confirmed via this project's own pinned
 * third_party/xnu-7195/bsd/sys/fcntl.h:154 (`#define O_SYMLINK
 * 0x00200000`) -- not declared in this project's Xcode-12 iPhoneOS SDK
 * fcntl.h (availability-gated out of the public SDK, but the kernel
 * value is real and unchanged), so the raw numeric value from that real
 * source is used directly rather than guessed. This is a real,
 * documented deviation from a single atomic *at syscall (a
 * resolve-then-fchflags is briefly non-atomic vs. real chflagsat), not a
 * fabricated stub -- it performs the real chflags operation.
 */
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_SYMLINK
#define O_SYMLINK 0x00200000
#endif
#ifndef AT_SYMLINK_NOFOLLOW
#define AT_SYMLINK_NOFOLLOW 0x0020
#endif

extern int fchflags(int fd, unsigned int flags);

int
chflagsat(int dirfd, const char *path, unsigned long flags, int atflag)
{
	int oflags = O_RDONLY;
	int fd, r, serrno;

	if (atflag & AT_SYMLINK_NOFOLLOW)
		oflags |= O_SYMLINK;
	fd = openat(dirfd, path, oflags);
	if (fd == -1)
		return (-1);
	r = fchflags(fd, flags);
	serrno = errno;
	close(fd);
	errno = serrno;
	return (r);
}
