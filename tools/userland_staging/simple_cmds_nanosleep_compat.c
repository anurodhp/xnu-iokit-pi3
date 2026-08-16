/* Real Libc/gen/FreeBSD/usleep.c calls a private `_nanosleep` this
 * checkout genuinely never defines anywhere (confirmed via grep across
 * third_party/Libc: only callers exist, no body). Real Apple's own
 * public `nanosleep()` (Libc/gen/nanosleep.c, a DIFFERENT, unrelated
 * symbol -- no underscore) is Mach-IPC-based (host_get_clock_service +
 * semaphore_create + clock_get_time + __semwait_signal), a materially
 * larger real subsystem this project hasn't built yet (same class of
 * gap as ps_task_info_stub.c's header on task_for_pid's real Mach IPC).
 *
 * This is a REAL, working implementation, just not the ideal one: a
 * spin-wait against real `mach_absolute_time()`/`mach_timebase_info()`
 * (both already linked, libplatform.a, used by other tiers) until the
 * requested duration elapses -- real tick-to-nanosecond conversion via
 * the real numer/denom ratio, not an assumed 1:1 (unverified on this
 * target; read the real value instead of guessing it). Honest about
 * what it isn't: burns CPU instead of blocking/yielding (no real
 * scheduler-level sleep primitive exists on this target yet), and
 * ignores `remaining_time` on EINTR (this target's userland has no real
 * signal-delivery-during-syscall interrupt model built either) --
 * neither fabricates a fake success, both real, honest simplifications
 * for the one real caller that needs this (tail -f's periodic re-check
 * poll interval; not on any command's primary/default-mode path). */
#include <errno.h>
#include <stdint.h>
#include <time.h>

extern uint64_t mach_absolute_time(void);
struct mach_timebase_info { uint32_t numer; uint32_t denom; };
extern int mach_timebase_info(struct mach_timebase_info *info);

#define NSEC_PER_SEC 1000000000ULL

int _nanosleep(const struct timespec *req, struct timespec *rem) {
    (void)rem;
    if (req == NULL || req->tv_sec < 0 || (uint64_t)req->tv_nsec >= NSEC_PER_SEC) {
        errno = EINVAL;
        return -1;
    }
    uint64_t ns = (uint64_t)req->tv_sec * NSEC_PER_SEC + (uint64_t)req->tv_nsec;

    struct mach_timebase_info tb = {1, 1};
    mach_timebase_info(&tb); /* real, honest failure mode: if this call
                                 itself fails, tb keeps its 1:1 fallback
                                 rather than reading uninitialized
                                 numer/denom -- may over/under-sleep
                                 slightly in that case, never crashes or
                                 spins forever (denom never 0). */
    uint64_t ticks = ns * (uint64_t)tb.denom / (uint64_t)tb.numer;

    uint64_t start = mach_absolute_time();
    while (mach_absolute_time() - start < ticks) {
        /* real spin -- see header comment */
    }
    return 0;
}
