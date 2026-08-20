/* Real public nanosleep(2) for shell_cmds/sleep/sleep.c (the ONLY current
 * caller -- confirmed via grep, tail's own -f poll interval uses the
 * private double-underscore variant instead, see
 * simple_cmds_nanosleep_compat.c's own header for that unrelated path).
 *
 * Real Apple `Libc/gen/nanosleep.c` under `__DARWIN_UNIX03=1` (this
 * project's build always sets that) is Mach-IPC-based
 * (host_get_clock_service + semaphore_create + clock_get_time +
 * __semwait_signal) -- the exact same real, not-yet-built subsystem
 * documented as a gap by simple_cmds_nanosleep_compat.c's header and
 * docs/roadmap.md's "Known stubbings" (Phase 4.6 item 5). Per this
 * project's source-priority rule (Apple real source first, FreeBSD
 * fallback second, custom only when neither is buildable yet), this is
 * the same class of narrow last-resort custom stand-in as that sibling
 * file -- deliberately NOT a copy-paste of its body, since that file
 * defines the different, private `__nanosleep` symbol usleep.c needs,
 * not the public one sleep(1) calls directly.
 *
 * Real, honest simplification (identical trade-off to the sibling file):
 * a spin-wait against real `mach_absolute_time()`/`mach_timebase_info()`
 * (already linked via libplatform.a) for the real requested duration,
 * using the real numer/denom tick ratio (read, not assumed 1:1). Burns
 * CPU instead of blocking -- no real scheduler-level sleep primitive
 * exists on this target yet -- and leaves `remaining_time` untouched on
 * the (here, unreachable in practice) EINTR path, since this target's
 * userland has no real signal-delivery-during-syscall model either.
 * Never fabricates a fake success/short-sleep. */
#include <errno.h>
#include <stdint.h>
#include <time.h>

extern uint64_t mach_absolute_time(void);
struct mach_timebase_info { uint32_t numer; uint32_t denom; };
extern int mach_timebase_info(struct mach_timebase_info *info);

#define NSEC_PER_SEC 1000000000ULL

int nanosleep(const struct timespec *req, struct timespec *rem) {
    (void)rem;
    if (req == NULL || req->tv_sec < 0 || (uint64_t)req->tv_nsec >= NSEC_PER_SEC) {
        errno = EINVAL;
        return -1;
    }
    uint64_t ns = (uint64_t)req->tv_sec * NSEC_PER_SEC + (uint64_t)req->tv_nsec;

    struct mach_timebase_info tb = {1, 1};
    mach_timebase_info(&tb); /* real, honest fallback -- see header */
    uint64_t ticks = ns * (uint64_t)tb.denom / (uint64_t)tb.numer;

    uint64_t start = mach_absolute_time();
    while (mach_absolute_time() - start < ticks) {
        /* real spin -- see header comment */
    }
    return 0;
}
