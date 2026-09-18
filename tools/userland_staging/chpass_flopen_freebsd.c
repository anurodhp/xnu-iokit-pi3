/* Real, essentially-unmodified FreeBSD lib/libutil/flopen.c (DAR-174,
 * chpass(1) -- see chpass_pw_util_freebsd.c's own header for the full
 * writeup of why this project needed to pull real FreeBSD source for
 * chpass's pw_util functions). pw_lock() (chpass_pw_util_freebsd.c)
 * calls real flopen(path, O_RDONLY|O_NONBLOCK|O_CLOEXEC, 0) -- FreeBSD's
 * own reliable open+flock helper, not reimplemented by hand.
 *
 * Pulled from this project's own pinned third_party/freebsd-src @
 * release/13.2.0 (lib/libutil/flopen.c), sparse-checked-out via
 * setup_third_party.sh (see that script's own FREEBSD_DEST block).
 *
 * ONE real, documented deviation from the upstream file: it adds
 * `#include <fcntl.h>` (upstream only includes <sys/file.h> and
 * <sys/stat.h>). Real FreeBSD's own <sys/file.h> defines LOCK_EX/
 * LOCK_SH/LOCK_NB itself; this project's Apple/XNU-derived headers do
 * NOT -- they live in third_party/xnu-7195/bsd/sys/fcntl.h (confirmed
 * via grep; third_party/xnu-7195/bsd/sys/file.h has no LOCK_* macros at
 * all) -- so without this include, LOCK_EX/LOCK_NB below are simply
 * undefined identifiers on this project's real header set. AT_FDCWD is
 * reached the same real way (fcntl.h), also absent from this project's
 * sys/file.h.
 *
 * Everything else below -- every real function body -- is byte-for-byte
 * the real upstream file.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>	/* LOCK_EX/LOCK_NB/AT_FDCWD -- see header above */

#include <errno.h>
#include <stdarg.h>
#include <unistd.h>

#include <libutil.h>

/*
 * Reliably open and lock a file.
 *
 * Please do not modify this code without first reading the revision history
 * and discussing your changes with <des@freebsd.org>.  Don't be fooled by the
 * code's apparent simplicity; there would be no need for this function if it
 * was easy to get right.
 */
static int
vflopenat(int dirfd, const char *path, int flags, va_list ap)
{
	int fd, operation, serrno, trunc;
	struct stat sb, fsb;
	mode_t mode;

#ifdef O_EXLOCK
	flags &= ~O_EXLOCK;
#endif

	mode = 0;
	if (flags & O_CREAT) {
		mode = (mode_t)va_arg(ap, int); /* mode_t promoted to int */
	}

        operation = LOCK_EX;
        if (flags & O_NONBLOCK)
                operation |= LOCK_NB;

	trunc = (flags & O_TRUNC);
	flags &= ~O_TRUNC;

	for (;;) {
		if ((fd = openat(dirfd, path, flags, mode)) == -1)
			/* non-existent or no access */
			return (-1);
		if (flock(fd, operation) == -1) {
			/* unsupported or interrupted */
			serrno = errno;
			(void)close(fd);
			errno = serrno;
			return (-1);
		}
		if (fstatat(dirfd, path, &sb, 0) == -1) {
			/* disappeared from under our feet */
			(void)close(fd);
			continue;
		}
		if (fstat(fd, &fsb) == -1) {
			/* can't happen [tm] */
			serrno = errno;
			(void)close(fd);
			errno = serrno;
			return (-1);
		}
		if (sb.st_dev != fsb.st_dev ||
		    sb.st_ino != fsb.st_ino) {
			/* changed under our feet */
			(void)close(fd);
			continue;
		}
		if (trunc && ftruncate(fd, 0) != 0) {
			/* can't happen [tm] */
			serrno = errno;
			(void)close(fd);
			errno = serrno;
			return (-1);
		}
		/*
		 * The following change is provided as a specific example to
		 * avoid.
		 */
#if 0
		if (fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) {
			serrno = errno;
			(void)close(fd);
			errno = serrno;
			return (-1);
		}
#endif
		return (fd);
	}
}

int
flopen(const char *path, int flags, ...)
{
	va_list ap;
	int ret;

	va_start(ap, flags);
	ret = vflopenat(AT_FDCWD, path, flags, ap);
	va_end(ap);
	return (ret);
}

int
flopenat(int dirfd, const char *path, int flags, ...)
{
	va_list ap;
	int ret;

	va_start(ap, flags);
	ret = vflopenat(dirfd, path, flags, ap);
	va_end(ap);
	return (ret);
}
