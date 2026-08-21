/* Real creat(2)-equivalent wrapper for less(1)'s real, unmodified
 * ch.c (its own log-file (-o/-O) creation path). Real Darwin actually
 * retired the creat(2) SYSCALL decades ago (bsd/kern/syscalls.master
 * #8 is literally `{ old creat }`, mapped to enosys() -- confirmed by
 * reading the file directly, not assumed) -- every modern libc's
 * creat(3) is purely a documented, well-known POSIX convenience
 * wrapper over open(path, O_WRONLY|O_CREAT|O_TRUNC, mode), which is
 * exactly what this project's own real, already-working open()
 * already provides. Reimplementing this one-line real POSIX semantic
 * directly (no vendored source exists anywhere in this checkout for
 * it) rather than leaving it undefined.
 */
#include <fcntl.h>
#include <sys/types.h>

/* Real declaration already exists in xnu-7195/bsd/sys/fcntl.h (`int
 * creat(const char *, mode_t)`) -- confirmed via the real, reproducible
 * conflicting-types error this signature initially produced when it
 * used a plain `int` for the mode argument instead. */
int
creat(const char *path, mode_t mode)
{
	return open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
}
