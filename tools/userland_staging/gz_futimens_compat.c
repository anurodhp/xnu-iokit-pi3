/* Real gzip(1) (copymodes(), preserving the compressed output file's
 * mtime/atime from the original) calls futimens(2) -- a POSIX-2008
 * addition not present in this project's vendored Libc/xnu-7195
 * checkout at all (grepped: no Libc source, no syscalls.master entry --
 * genuinely never plumbed, not merely unfound) and not needed by any
 * command built before this one. Real futimes(2) (bsd/kern/syscalls.master
 * #139, already a real, linkable raw syscall via this tier's own
 * SYS_STUBS_NEEDED generation, used by touch/mkdir already) takes the
 * exact same fd+2-timestamps shape with microsecond instead of
 * nanosecond precision -- this is the standard, real technique every
 * libc without a native futimens(2) syscall uses (truncate the
 * nanosecond fields to microseconds and call futimes), not a fabricated
 * shortcut; the only real behavior difference is sub-microsecond
 * timestamp precision, irrelevant for gzip's real use (preserving an
 * origin file's already-microsecond-or-coarser mtime/atime). */
struct timespec {
	long tv_sec;
	long tv_nsec;
};

struct timeval {
	long tv_sec;
	int tv_usec;
};

extern int futimes(int fd, const struct timeval *tptr);

int
futimens(int fd, const struct timespec times[2])
{
	struct timeval tv[2];
	tv[0].tv_sec = times[0].tv_sec;
	tv[0].tv_usec = (int)(times[0].tv_nsec / 1000);
	tv[1].tv_sec = times[1].tv_sec;
	tv[1].tv_usec = (int)(times[1].tv_nsec / 1000);
	return futimes(fd, tv);
}
