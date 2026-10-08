/* libc entry points libghostty-vt (built by zig against Apple's libSystem)
 * binds that this port's libsystem does not export. */
#include <errno.h>
#include <stddef.h>
#include <sys/types.h>
#include <unistd.h>

struct iovec_s { void *iov_base; size_t iov_len; };

int shim_close_nocancel(int fd) __asm__("_close$NOCANCEL");
int shim_close_nocancel(int fd) { return close(fd); }

ssize_t shim_preadv(int fd, const struct iovec_s *iov, int cnt, off_t off) __asm__("_preadv");
ssize_t shim_preadv(int fd, const struct iovec_s *iov, int cnt, off_t off)
{
	ssize_t total = 0;
	for (int i = 0; i < cnt; i++) {
		ssize_t n = pread(fd, iov[i].iov_base, iov[i].iov_len, off + total);
		if (n < 0)
			return total > 0 ? total : -1;
		total += n;
		if ((size_t)n < iov[i].iov_len)
			break;
	}
	return total;
}

int shim_shm_unlink(const char *name) __asm__("_shm_unlink");
int shim_shm_unlink(const char *name) { (void)name; errno = ENOSYS; return -1; }

/* zig's compiler_rt (platform-version checks, __clear_cache). Nothing in the
 * terminal library executes generated code, and every API it asks about is
 * the one this libSystem provides. */
#include <stdint.h>
int shim_availability_version_check(uint32_t count, void *versions) __asm__("__availability_version_check");
int shim_availability_version_check(uint32_t count, void *versions) { (void)count; (void)versions; return 1; }

void shim_icache(void *start, size_t len) __asm__("_sys_icache_invalidate");
void shim_icache(void *start, size_t len) { (void)start; (void)len; }
