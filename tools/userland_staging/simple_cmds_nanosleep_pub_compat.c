/* Real public nanosleep(2) for shell_cmds/sleep/sleep.c (the ONLY current
 * caller -- confirmed via grep, tail's own -f poll interval uses the
 * private double-underscore variant instead, see
 * simple_cmds_nanosleep_compat.c's own header for that unrelated path).
 *
 * REAL Mach-IPC implementation, Phase 4.6 item 5 (2026-08-21) -- this
 * used to be a CPU-burning mach_absolute_time() spin-wait (see git log
 * for the prior body); now a faithful adaptation of real Apple
 * third_party/Libc/gen/nanosleep.c's own `__DARWIN_UNIX03` body (this
 * project's build always sets `-D__DARWIN_UNIX03=1`, confirmed via
 * build_simple_cmds.sh's CFLAGS), genuinely blocking via
 * host_get_clock_service + semaphore_create (real mach_msg()-based MIG
 * RPC, this project's first-ever exercise of that subsystem -- see
 * build_mach_ipc.sh, mach_ipc_clock_init.c, clockUser.c/mach_hostUser.c/
 * taskUser.c) + clock_get_time + __semwait_signal (real BSD syscall 334,
 * already linked via libc_build/libmalloc.a's sys_semwait_signal.o,
 * exercised previously only by libmalloc_sleep_shim.c's simpler
 * no-real-semaphore case -- this is its first real semaphore-backed
 * caller). Not reimplemented from scratch -- this IS real Apple's own
 * algorithm, just not literally the same translation unit (real
 * nanosleep.c isn't compiled as-is: its `_init_clock_port()` needs an
 * idempotency guard this project's no-dyld environment requires, see
 * mach_ipc_clock_init.c's own header comment for why).
 *
 * Deliberately kept from real nanosleep.c: same EINVAL argument
 * validation, same clock_get_time-before/after bracketing for computing
 * `remaining_time` on EINTR, same ETIMEDOUT/EINTR/other-errno branching
 * on __semwait_signal's return. Deliberately DIFFERENT from real
 * nanosleep.c: uses the plain (cancelable) `__semwait_signal`, not
 * `__semwait_signal_nocancel` -- this project has no pthread
 * cancellation infrastructure, and __semwait_signal (syscall 334) is
 * already the real, working, previously-QEMU-exercised syscall
 * libmalloc_sleep_shim.c uses (same real syscall, not a different one) --
 * no cancellation ever actually fires either way on this target, so the
 * two variants are behaviorally identical here.
 */
#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <mach/mach_types.h>
#include <mach/kern_return.h>
#include <mach/clock_types.h>

extern void mach_ipc_clock_init(void);
extern mach_port_t g_mach_ipc_clock_sem;
extern mach_port_t g_mach_ipc_clock_port;
extern kern_return_t clock_get_time(mach_port_t clock_serv, mach_timespec_t *cur_time);
extern int __semwait_signal(int cond_sem, int mutex_sem, int timeout,
    int relative, long long tv_sec, int tv_nsec);

int nanosleep(const struct timespec *requested_time, struct timespec *remaining_time)
{
	mach_ipc_clock_init();

	if (requested_time == NULL || requested_time->tv_sec < 0 ||
	    (uint64_t)requested_time->tv_nsec >= NSEC_PER_SEC) {
		errno = EINVAL;
		return -1;
	}

	mach_timespec_t completion;
	if (remaining_time != NULL) {
		kern_return_t kret = clock_get_time(g_mach_ipc_clock_port, &completion);
		if (kret != KERN_SUCCESS) {
			errno = EINVAL;
			return -1;
		}
	}

	int ret = __semwait_signal(g_mach_ipc_clock_sem, MACH_PORT_NULL, 1, 1,
	    (long long)requested_time->tv_sec, (int)requested_time->tv_nsec);
	if (ret < 0) {
		if (errno == ETIMEDOUT) {
			return 0;
		} else if (errno == EINTR) {
			if (remaining_time != NULL) {
				mach_timespec_t current;
				kern_return_t kret = clock_get_time(g_mach_ipc_clock_port, &current);
				if (kret != KERN_SUCCESS) {
					return -1;
				}
				/* Real nanosleep.c's own comment: this depends on the
				 * layout of a mach_timespec_t and timespec_t being
				 * equivalent -- both {tv_sec, tv_nsec}, real on this
				 * target (same assumption real Apple's own source makes,
				 * not new here). */
				ADD_MACH_TIMESPEC(&completion, requested_time);
				if (CMP_MACH_TIMESPEC(&completion, &current) > 0) {
					SUB_MACH_TIMESPEC(&completion, &current);
					remaining_time->tv_sec = completion.tv_sec;
					remaining_time->tv_nsec = completion.tv_nsec;
				} else {
					remaining_time->tv_sec = 0;
					remaining_time->tv_nsec = 0;
				}
			}
		} else {
			errno = EINVAL;
		}
	}
	return -1;
}
