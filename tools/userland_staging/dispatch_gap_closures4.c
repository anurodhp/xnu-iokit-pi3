/* sleep() REMOVED from this file 2026-08-31 (static-linking cleanup,
 * "needs a new home" batch): it had no real single canonical owner
 * before this (this file's own copy vs libncurses.dylib's independent
 * one), and neither was real Apple's actual home for it. Moved to
 * libsystem_c.dylib instead (libsystem_c_sleep_compat.c, same real
 * algorithm, reused verbatim not re-derived -- see that file's own
 * header for the full citation, including why real
 * third_party/Libc/gen/FreeBSD/sleep.c still can't be used verbatim
 * here). This dylib already reexports libsystem_c.dylib wholesale
 * (see this script's own final link command), so nothing downstream
 * lost the symbol -- it just resolves through the reexport now instead
 * of a private copy compiled into this file.
 *
 * mach_continuous_time/mach_get_times: real origin
 * third_party/xnu-7195/libsyscall/wrappers/mach_continuous_time.c and
 * mach_get_times.c, narrowed in 2 real, documented ways rather than
 * ported verbatim:
 *   1. This kernel port never writes _COMM_PAGE_CONT_HWCLOCK/
 *      _COMM_PAGE_CONT_TIMEBASE (confirmed absent from every
 *      any osfmk/arm source in this project's xnu-7195 fork via grep before
 *      writing this, not assumed) -- same real, already-accepted gap
 *      class as tools/init_binary/init.c's own clock_gettime_nsec_np
 *      comment documents for _COMM_PAGE_BOOTTIME_USEC. Reading a
 *      zeroed comm page here is safe, not garbage: _mach_continuous_
 *      hwclock's `cont_hwclock` byte reads 0, so it correctly returns
 *      KERN_NOT_SUPPORTED (its own real fallback branch, not a
 *      fabrication) and _mach_continuous_time's own retry loop reads a
 *      constant 0 base, converging on read1==read2 immediately (no
 *      infinite loop) and yielding cont_time == absolute_time. Real,
 *      honest degraded behavior for a kernel that has no real
 *      sleep/wake distinction to report yet, not a crash risk.
 *   2. mach_get_times' real tp!=NULL branch needs
 *      __commpage_gettimeofday_internal (a comm-page fast-path this
 *      project never ported either) with fallback to
 *      __gettimeofday_with_mach -- narrowed to call
 *      __gettimeofday_with_mach directly every time (that symbol is
 *      already real and linked, confirmed via nm before writing this),
 *      skipping only the comm-page fast-path optimization, not any
 *      correctness. Same "one path only, not two are both traps"
 *      narrowing this project's own mach_port_extract_right precedent
 *      already established. */
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <stdint.h>
#include <sys/time.h>
#include <mach/mach_time.h>
#include <arm/cpu_capabilities.h>

static uint64_t
_mach_continuous_time_base(void)
{
	return *(volatile uint64_t *)_COMM_PAGE_CONT_TIMEBASE;
}

static kern_return_t
_mach_continuous_hwclock(uint64_t *cont_time)
{
	uint8_t cont_hwclock = *((volatile uint8_t *)_COMM_PAGE_CONT_HWCLOCK);
	if (cont_hwclock) {
#if defined(__arm64__)
		volatile uint64_t *base_ptr = (volatile uint64_t *)_COMM_PAGE_CONT_HW_TIMEBASE;
		__builtin_arm_isb(0xf);
		*cont_time = __builtin_arm_rsr64("CNTVCT_EL0") + *base_ptr;
		return KERN_SUCCESS;
#endif
	}
	return KERN_NOT_SUPPORTED;
}

static kern_return_t
_mach_continuous_time_impl(uint64_t *absolute_time, uint64_t *cont_time)
{
	volatile uint64_t *base_ptr = (volatile uint64_t *)_COMM_PAGE_CONT_TIMEBASE;
	volatile uint64_t read1, read2;
	volatile uint64_t absolute;

	do {
		read1 = *base_ptr;
		absolute = mach_absolute_time();
		read2 = *base_ptr;
	} while (__builtin_expect((read1 != read2), 0));

	if (absolute_time) {
		*absolute_time = absolute;
	}
	if (cont_time) {
		*cont_time = absolute + read1;
	}

	return KERN_SUCCESS;
}

uint64_t
mach_continuous_time(void)
{
	uint64_t cont_time;
	if (_mach_continuous_hwclock(&cont_time) != KERN_SUCCESS) {
		_mach_continuous_time_impl(NULL, &cont_time);
	}
	return cont_time;
}

/* mach_continuous_approximate_time() deliberately NOT provided here:
 * it needs mach_approximate_time(), which isn't a real symbol anywhere
 * in this project (confirmed via nm before writing this) -- and nothing
 * in build_libdispatch_dylib.sh's own real remaining-gap list actually
 * calls mach_continuous_approximate_time(), only mach_continuous_time()/
 * mach_get_times(). Adding it would trade zero real gaps for one new
 * one; left out on purpose, not an oversight. */

extern int __gettimeofday_with_mach(struct timeval *, struct timezone *, uint64_t *);

kern_return_t
mach_get_times(uint64_t *absolute_time, uint64_t *cont_time, struct timespec *tp)
{
	if (tp == NULL) {
		return _mach_continuous_time_impl(absolute_time, cont_time);
	}

	uint64_t continuous_time_base_prior, continuous_time_base_post;
	uint64_t tbr = 0;
	struct timeval tv;

	do {
		continuous_time_base_prior = _mach_continuous_time_base();

		if (__gettimeofday_with_mach(&tv, NULL, &tbr) < 0) {
			return KERN_FAILURE;
		}

		continuous_time_base_post = _mach_continuous_time_base();
	} while (__builtin_expect((continuous_time_base_prior != continuous_time_base_post), 0));

	if (absolute_time) {
		*absolute_time = tbr;
	}
	if (cont_time) {
		*cont_time = continuous_time_base_prior + tbr;
	}
	tp->tv_sec = tv.tv_sec;
	tp->tv_nsec = tv.tv_usec * 1000;

	return KERN_SUCCESS;
}
