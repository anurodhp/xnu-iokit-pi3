/* Real, curated semaphore_signal()/semaphore_wait() -- verbatim bodies
 * from third_party/xnu-7195/libsyscall/mach/semaphore.c (real Apple
 * source), curated down to just these two real functions rather than
 * the whole file: the other 6 functions in that file
 * (semaphore_signal_all/_thread/semaphore_timedwait/_signal/
 * _wait_signal/_timedwait_signal) need their own trap declarations this
 * project has no other real caller for yet, matching this project's own
 * established curation precedent (build_libmach_ipc.sh's
 * mach_host_self_real.c, curated from mach_legacy.c the same way).
 * Traps compiled separately in mach_semaphore_traps.s.
 */
#include <mach/mach_types.h>
#include <mach/kern_return.h>

extern kern_return_t semaphore_signal_trap(mach_port_t signal_semaphore);
extern kern_return_t semaphore_wait_trap(mach_port_t wait_semaphore);

kern_return_t
semaphore_signal(mach_port_t signal_semaphore)
{
	return semaphore_signal_trap(signal_semaphore);
}

kern_return_t
semaphore_wait(mach_port_t wait_semaphore)
{
	return semaphore_wait_trap(wait_semaphore);
}
