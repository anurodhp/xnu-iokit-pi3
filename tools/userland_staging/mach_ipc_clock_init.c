/* Real, adapted `_init_clock_port()` -- Phase 4.6 item 5's shared
 * one-time Mach-IPC bootstrap for real blocking `nanosleep(2)`/
 * `_nanosleep`, used by both simple_cmds_nanosleep_pub_compat.c (public
 * `nanosleep()`, `sleep(1)`'s real underlying call) and
 * simple_cmds_nanosleep_compat.c (private `_nanosleep`, `tail -f`'s poll
 * interval). Adapted (not verbatim) from real Apple
 * third_party/Libc/gen/nanosleep.c's own `_init_clock_port()`
 * (`__DARWIN_UNIX03` branch, this project's build always sets that --
 * see build_mach_ipc.sh) in exactly one way: real Apple's version is
 * meant to run exactly once via a dyld/libSystem_initializer hook this
 * project doesn't have (same "must be called explicitly in a static
 * executable, no dyld" situation mach_init()/pthread_tsd_shim_bootstrap()
 * already document) -- an idempotency guard is added here so any number
 * of callers (both nanosleep entry points) can call it safely, same
 * pattern libmalloc_dispatch.c's get_zone() already established for
 * mach_init()/pthread_tsd_shim_bootstrap() themselves.
 *
 * clock_sem/clock_port globals, and the real host_get_clock_service +
 * semaphore_create call sequence, are otherwise byte-for-byte the real
 * algorithm -- this project's own real, unmodified `mig`-generated
 * client stubs (clockUser.c/mach_hostUser.c/taskUser.c, see
 * build_mach_ipc.sh) do the actual mach_msg() IPC underneath.
 *
 * On a real failure here (host_get_clock_service/semaphore_create
 * genuinely returning non-KERN_SUCCESS), real Apple's own
 * _init_clock_port() calls abort() -- kept identical: a real,
 * unrecoverable environment failure (this project's own kernel would
 * have to be broken for this to fire, not a normal-operation path), not
 * silently swallowed into a fake success.
 */
#include <mach/mach_types.h>
#include <mach/kern_return.h>
#include <mach/clock_types.h>
#include <mach/sync_policy.h>

extern void abort(void);
extern mach_port_t mach_host_self(void);
extern mach_port_t mach_task_self_;
extern int mach_init(void);
extern int pthread_tsd_shim_bootstrap(void);
extern kern_return_t mach_port_deallocate(ipc_space_t task, mach_port_name_t name);
extern kern_return_t host_get_clock_service(mach_port_t host, int clock_id, mach_port_t *clock_serv);
extern kern_return_t semaphore_create(mach_port_t task, mach_port_t *semaphore, int policy, int value);

mach_port_t g_mach_ipc_clock_sem = MACH_PORT_NULL;
mach_port_t g_mach_ipc_clock_port = MACH_PORT_NULL;

static int g_mach_ipc_clock_inited = 0;

void mach_ipc_clock_init(void)
{
	if (g_mach_ipc_clock_inited) {
		return;
	}
	/* Defensive, idempotent -- see this file's header comment. Normally
	 * already done by the first malloc() call anywhere in the process
	 * (libmalloc_dispatch.c's get_zone()), but a caller that reaches
	 * nanosleep()/tail -f's poll before ever allocating must not read
	 * an uninitialized TPIDRRO_EL0/mach_task_self_. */
	if (!pthread_tsd_shim_bootstrap()) {
		abort();
	}
	mach_init();

	kern_return_t kr;
	mach_port_t host = mach_host_self();

	kr = host_get_clock_service(host, SYSTEM_CLOCK, &g_mach_ipc_clock_port);
	if (kr != KERN_SUCCESS) {
		abort();
	}

	kr = semaphore_create(mach_task_self_, &g_mach_ipc_clock_sem, SYNC_POLICY_FIFO, 0);
	if (kr != KERN_SUCCESS) {
		abort();
	}
	mach_port_deallocate(mach_task_self_, host);

	g_mach_ipc_clock_inited = 1;
}
