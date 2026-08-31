/* Real public sleep(2)/_sleep -- moved here 2026-08-31 (static-linking
 * cleanup, "needs a new home" batch) as the one real canonical copy.
 *
 * NOT third_party/Libc/gen/FreeBSD/sleep.c verbatim: that real file's
 * __sleep() calls the cancelable-variant symbol `_nanosleep` (linker
 * name `__nanosleep`, real Apple's namespace.h convention for the
 * translation unit that itself implements the public name) -- this
 * project's own real nanosleep port (simple_cmds_nanosleep_pub_compat.c,
 * canonical home libsystem_kernel.dylib as of this same batch) never
 * replicated that internal double-underscore split; it only ever
 * exports the plain public `nanosleep` (confirmed via nm before writing
 * this, not guessed), so linking real sleep.c's `_nanosleep` call fails.
 *
 * This is the exact same wall dispatch_gap_closures4.c's own header
 * comment already documented and worked around on 2026-08-27, when
 * libdispatch.dylib needed a `sleep()` and nothing else in the project
 * had one yet: reproduce the real FreeBSD algorithm (same overflow-
 * avoidance for huge `seconds`, same EINTR-remaining-time rounding)
 * narrowed to call the symbol that actually exists (plain `nanosleep`)
 * instead of the cancelable-internal one. Not re-derived here -- this
 * is that same real, already-QEMU-verified body, relocated.
 *
 * Real canonical owner is libsystem_c.dylib (real Apple ships sleep()
 * as part of Libc's "gen" tier, distinct from nanosleep() itself, which
 * is a Libsyscall/libsystem_kernel.dylib-tier custom wrapper -- see
 * build_libsystem_kernel.sh's own nanosleep_compat.o comment for that
 * half of this same investigation). Before this batch, `_sleep` had two
 * independent owners (libdispatch.dylib's dispatch_gap_closures4.c and
 * libncurses.dylib's ncurses_extra_syscalls.c's own `_mal_semwait_sleep`-
 * based version) and no real Apple-shaped canonical home -- both are
 * dropped in favor of this one (libdispatch.dylib already reexports
 * this dylib wholesale; libncurses.dylib already links it directly as a
 * sibling), see build_libdispatch_dylib.sh's and build_ncurses_dylib.sh's
 * own updated comments.
 */
#include <errno.h>
#include <limits.h>
#include <time.h>

unsigned int
sleep(unsigned int seconds)
{
	struct timespec time_to_sleep;
	struct timespec time_remaining;

	if (seconds > INT_MAX) {
		return (seconds - INT_MAX + sleep(INT_MAX));
	}

	time_to_sleep.tv_sec = seconds;
	time_to_sleep.tv_nsec = 0;
	time_remaining.tv_sec = 0;
	time_remaining.tv_nsec = 0;
	if (nanosleep(&time_to_sleep, &time_remaining) != -1) {
		return 0;
	}
	if (errno != EINTR) {
		return seconds;
	}
	return (unsigned int)(time_remaining.tv_sec + (time_remaining.tv_nsec != 0));
}
