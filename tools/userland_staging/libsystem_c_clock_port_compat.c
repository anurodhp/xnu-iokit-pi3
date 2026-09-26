/* _init_clock_port() and its two globals, verbatim from
 * third_party/Libc/gen/nanosleep.c:40-60. Needed because the real
 * _libc_fork_child() (third_party/Libc/sys/_libc_fork_child.c:47) calls it.
 * The rest of nanosleep.c is not compiled here: this project's nanosleep()
 * comes from libsystem_kernel.dylib. */
#include <stdlib.h>
#include <mach/mach.h>
#include <mach/clock.h>

/* build_libsystem_c.sh's compat_include shadows <mach/mach_init.h>, so
 * declare the two it would have provided (SDK mach/mach_init.h). */
extern mach_port_t mach_host_self(void);
extern mach_port_t mach_task_self_;
#ifndef mach_task_self
#define mach_task_self() mach_task_self_
#endif

semaphore_t	clock_sem = MACH_PORT_NULL;
mach_port_t	clock_port = MACH_PORT_NULL;

void _init_clock_port(void);

void _init_clock_port(void) {
	kern_return_t kr;
	mach_port_t host = mach_host_self();
	
	/* Get the clock service port for nanosleep */
	kr = host_get_clock_service(host, SYSTEM_CLOCK, &clock_port);
	if (kr != KERN_SUCCESS) {
		abort();
	}
	
	kr = semaphore_create(mach_task_self(), &clock_sem, SYNC_POLICY_FIFO, 0);
	if (kr != KERN_SUCCESS) {
		abort();
	}
	mach_port_deallocate(mach_task_self(), host);
}
