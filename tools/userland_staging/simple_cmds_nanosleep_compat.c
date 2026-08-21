/* Real Libc/gen/FreeBSD/usleep.c calls a private `_nanosleep` this
 * checkout genuinely never defines anywhere (confirmed via grep across
 * third_party/Libc: only callers exist, no body). Real Apple's own
 * public `nanosleep()` (Libc/gen/nanosleep.c, a DIFFERENT, unrelated
 * symbol -- no underscore) is the same real algorithm this private
 * `_nanosleep` needs -- see simple_cmds_nanosleep_pub_compat.c's own
 * header comment for the full real Mach-IPC chain (host_get_clock_service
 * + semaphore_create + clock_get_time + __semwait_signal, Phase 4.6 item
 * 5, 2026-08-21) and exactly why/how it differs from literally compiling
 * real nanosleep.c as-is.
 *
 * REAL implementation now (previously a CPU-burning mach_absolute_time()
 * spin-wait -- see git log for the prior body): this file and
 * simple_cmds_nanosleep_pub_compat.c are deliberately near-identical
 * (both real Apple nanosleep() bodies, just under the two different
 * symbol names each of their real callers needs) rather than one calling
 * the other, matching this file's own prior header's explicit note that
 * these are "a DIFFERENT, unrelated symbol" pair, not aliases -- usleep.c
 * calls `_nanosleep` specifically, sleep.c calls `nanosleep` specifically,
 * and this project has no linker-alias mechanism cleaner than just
 * defining both real bodies once each (same "one real function per real
 * symbol name" approach every other tier in this project's build already
 * uses, e.g. libmalloc_dispatch.c's separate malloc/calloc/valloc bodies
 * that all just call through get_zone()).
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

int _nanosleep(const struct timespec *req, struct timespec *rem)
{
	mach_ipc_clock_init();

	if (req == NULL || req->tv_sec < 0 || (uint64_t)req->tv_nsec >= NSEC_PER_SEC) {
		errno = EINVAL;
		return -1;
	}

	mach_timespec_t completion;
	if (rem != NULL) {
		kern_return_t kret = clock_get_time(g_mach_ipc_clock_port, &completion);
		if (kret != KERN_SUCCESS) {
			errno = EINVAL;
			return -1;
		}
	}

	int ret = __semwait_signal(g_mach_ipc_clock_sem, MACH_PORT_NULL, 1, 1,
	    (long long)req->tv_sec, (int)req->tv_nsec);
	if (ret < 0) {
		if (errno == ETIMEDOUT) {
			return 0;
		} else if (errno == EINTR) {
			if (rem != NULL) {
				mach_timespec_t current;
				kern_return_t kret = clock_get_time(g_mach_ipc_clock_port, &current);
				if (kret != KERN_SUCCESS) {
					return -1;
				}
				ADD_MACH_TIMESPEC(&completion, req);
				if (CMP_MACH_TIMESPEC(&completion, &current) > 0) {
					SUB_MACH_TIMESPEC(&completion, &current);
					rem->tv_sec = completion.tv_sec;
					rem->tv_nsec = completion.tv_nsec;
				} else {
					rem->tv_sec = 0;
					rem->tv_nsec = 0;
				}
			}
		} else {
			errno = EINVAL;
		}
	}
	return -1;
}
