/*
 * ca_rt.h: put the calling thread into the Mach real-time (time-constraint) class, as Apple's audio
 * stack does for its IO threads. Why it matters here: timer deadlines of normal threads are coalesced
 * with large leeway (nanosleep(4 ms) was measured at about 91 ms and usleep(1500 us) at about 39 ms on
 * this kernel), while real-time threads get no timer leeway. Interface: osfmk/mach/thread_policy.h
 * (THREAD_TIME_CONSTRAINT_POLICY: period, computation, constraint, preemptible, all in mach absolute
 * time units); the kernel validates it in thread_set_mach_policy / thread_policy_set_internal
 * (osfmk/kern/thread_policy.c).
 */
#ifndef CA_RT_H
#define CA_RT_H
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <stdint.h>

/* period/computation/constraint in nanoseconds. Returns the kern_return_t of thread_policy_set. */
static inline kern_return_t ca_make_realtime(uint64_t period_ns, uint64_t computation_ns, uint64_t constraint_ns)
{
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    thread_time_constraint_policy_data_t p;
    p.period      = (uint32_t)(period_ns * tb.denom / tb.numer);
    p.computation = (uint32_t)(computation_ns * tb.denom / tb.numer);
    p.constraint  = (uint32_t)(constraint_ns * tb.denom / tb.numer);
    p.preemptible = 1;
    mach_port_t th = mach_thread_self();
    kern_return_t kr = thread_policy_set(th, THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&p, THREAD_TIME_CONSTRAINT_POLICY_COUNT);
    mach_port_deallocate(mach_task_self(), th);
    return kr;
}
#endif
