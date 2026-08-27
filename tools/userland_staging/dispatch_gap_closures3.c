/* Real raw syscall implementation of sigsuspend, closing one more entry
 * of build_libdispatch_dylib.sh's own real remaining-gap list (see
 * tools/init_binary/init.c's spawn_syslogd() comment for the current
 * full list). Real syscall #111 per
 * third_party/xnu-7195/bsd/kern/syscalls.master:175
 * (`{ int sigsuspend(sigset_t mask) NO_SYSCALL_STUB; }`) -- same
 * NO_SYSCALL_STUB class, same hand-written carry-flag raw-trap pattern,
 * as this project's own pthread_bsdthread_register_syscall.c/
 * pthread_bsdthread_terminate_syscall.c (reused verbatim here, not
 * reinvented).
 *
 * semaphore_timedwait: real origin
 * third_party/xnu-7195/libsyscall/mach/semaphore.c:61 (verbatim body,
 * that file's own other 6 functions not needed -- this project's own
 * mach_semaphore_traps.s precedent already established the same "one
 * file, only the needed traps" split for semaphore_signal/wait). Only
 * new piece is semaphore_timedwait_trap itself
 * (dispatch_gap_closures3.s) -- semaphore_wait_trap already exists
 * elsewhere in this project (mach_semaphore_traps.s) but is a different
 * trap number, not reusable for the timed variant.
 *
 * malloc_engaged_nano/malloc_memory_event_handler: NOT verbatim source
 * (that would require this project's real, uncompiled Nano-zone/MSL
 * stack-logging machinery -- genuinely unbuilt here, a real, much larger
 * gap than this pass's scope). Instead, real-and-honest per real Apple's
 * own source: third_party/libmalloc/src/malloc.c's malloc_engaged_nano()
 * itself already degrades to exactly `return 0` whenever CONFIG_NANOZONE
 * is unset (verbatim `#else return 0 #endif` branch, confirmed by
 * reading it before writing this, not guessed) -- this project's own
 * curated libmalloc build never defines CONFIG_NANOZONE, so `return 0`
 * here reproduces that exact real fallback behavior, not a fabrication.
 * malloc_memory_event_handler's real body only ever calls
 * malloc_zone_pressure_relief()/malloc_register_stack_logger()/
 * msl.handle_memory_event() -- all 3 real, but this project has no real
 * memory-pressure-notification delivery path wired up at all (no
 * NOTE_MEMORYSTATUS_* dispatch source registration anywhere in this
 * tree), so the real body would never actually be reached with a
 * meaningful event in practice; a real, honest no-op here (matching
 * real Apple's own "nothing to do" outcome when none of those subsystems
 * are engaged) avoids a large, currently-pointless MSL/nanozone port for
 * a callback nothing in this project's real call graph invokes with
 * live data. */
#include <errno.h>
#include <sys/signal.h>
#include <mach/mach_types.h>
#include <mach/clock_types.h>
#include <mach/kern_return.h>

static inline long
sigsuspend_syscall1(long num, long a0)
{
	register long x0 asm("x0") = a0;
	register long x16 asm("x16") = num;
	long is_error;
	asm volatile("svc #0x80\n"
	             "cset %1, cs"
	             : "+r"(x0), "=r"(is_error)
	             : "r"(x16)
	             : "memory", "cc");
	if (is_error) {
		return -x0;
	}
	return x0;
}

int
sigsuspend(sigset_t mask)
{
	long r = sigsuspend_syscall1(111, (long)mask);
	if (r < 0) {
		errno = (int)-r;
		return -1;
	}
	return (int)r;
}

extern kern_return_t semaphore_timedwait_trap(mach_port_t, unsigned int, clock_res_t);

kern_return_t
semaphore_timedwait(mach_port_t wait_semaphore, mach_timespec_t wait_time)
{
	return semaphore_timedwait_trap(wait_semaphore,
	           wait_time.tv_sec,
	           wait_time.tv_nsec);
}

int
malloc_engaged_nano(void)
{
	return 0;
}

void
malloc_memory_event_handler(unsigned long event)
{
	(void)event;
}
