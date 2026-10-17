/*
 * mach_wait_until() for libmpv (build_libmpv.sh). mpv's darwin timer
 * (osdep/timer-darwin.c, mp_sleep_ns) sleeps with it, and this system's
 * libsystem_kernel.dylib does not export it (Apple's is a stub for the
 * mach_wait_until_trap Mach trap). Until libsystem_kernel has it, libmpv
 * carries this private one: the same contract (sleep until the
 * mach_absolute_time() deadline), with nanosleep(), resumed if a signal
 * interrupts it. Hidden: libmpv exports only mpv_*.
 */
#include <errno.h>
#include <mach/mach_time.h>
#include <stdint.h>
#include <time.h>

__attribute__((visibility("hidden")))
kern_return_t mach_wait_until(uint64_t deadline)
{
    static mach_timebase_info_data_t tb;
    if (!tb.denom)
        mach_timebase_info(&tb);
    uint64_t now = mach_absolute_time();
    if (deadline <= now)
        return KERN_SUCCESS;
    uint64_t ns = (deadline - now) * tb.numer / tb.denom;
    struct timespec ts = { (time_t)(ns / 1000000000u), (long)(ns % 1000000000u) };
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
        ;
    return KERN_SUCCESS;
}
