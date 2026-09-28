/* DAR-238 (2026-09-28): the parts of libdispatch's src/workgroup.c that
 * libdispatch needs without the os_workgroup API itself.
 *
 * With USE_OBJC (DAR-237), object.m's class bodies for OS_os_workgroup,
 * OS_os_workgroup_interval and OS_os_workgroup_parallel call
 * _os_workgroup_xref_dispose, _os_workgroup_dispose,
 * _os_workgroup_interval_xref_dispose, _os_workgroup_interval_dispose and
 * _os_workgroup_debug unconditionally, and queue.c's libdispatch_init()
 * registers _os_workgroup_tsd_cleanup as a TSD destructor. The bodies
 * below are copied verbatim from third_party/libdispatch/src/workgroup.c:
 * lines 68-143 for these six functions, with the static
 * _os_workgroup_is_configurable() helper at 91-95.
 *
 * The rest of workgroup.c is not built. It needs nine
 * work_interval_instance_* functions (alloc/free/clear/set_start/
 * set_deadline/start/update/set_finish/finish). Apple's libsystem_kernel
 * exports them (pinned iPhoneOS 14.4 SDK usr/lib/system/libsystem_kernel.tbd),
 * but neither xnu-7195 nor xnu-latest has source for them:
 * bsd/sys/work_interval.h declares only the work_interval_instance_t type,
 * and libsyscall/wrappers/work_interval.c doesn't define them. Nothing in
 * this tree calls os_workgroup_create*() or os_workgroup_interval_*().
 *
 * work_interval_destroy() is real. build_libsystem_kernel.sh builds
 * libsyscall/wrappers/work_interval.c into libsystem_kernel, which
 * exports it.
 *
 * work_interval_instance_free() is the one symbol with no source anywhere.
 * Its only caller is _os_workgroup_interval_dispose() below, which passes
 * wgi->wii. That field is set only by os_workgroup_interval_create*()
 * (workgroup.c, via work_interval_instance_alloc), and that isn't built.
 * So wii is always NULL here. The stand-in is file-static rather than an
 * exported fake libsystem_kernel symbol. It crashes loudly if it ever sees
 * a non-NULL instance, rather than leaking or pretending to free one.
 * -Wl,-U is not an option: an undefined symbol with no definer in the
 * process aborts every libdispatch load under eager chained fixups
 * (DAR-238 description).
 */
#include "internal.h"

#include <os/assumes.h>
#include <mach/mach_port.h>

static void
work_interval_instance_free(work_interval_instance_t wii)
{
	if (wii != NULL) {
		os_crash("DAR-238: work_interval_instance_free() has no "
				"implementation on this port (no source in xnu-7195)");
	}
}

void
_os_workgroup_xref_dispose(os_workgroup_t wg)
{
	os_workgroup_arena_t arena = wg->wg_arena;

	if (arena == NULL) {
		return;
	}

	arena->destructor(arena->client_arena);
	free(arena);
}

void
_os_workgroup_interval_xref_dispose(os_workgroup_interval_t wgi)
{
	uint64_t wg_state = wgi->wg_state;
	if (wg_state & OS_WORKGROUP_INTERVAL_STARTED) {
		os_crash("BUG IN CLIENT: Releasing last reference to workgroup interval "
			"while an interval has been started");
	}
}

static inline bool
_os_workgroup_is_configurable(uint64_t wg_state)
{
	return (wg_state & OS_WORKGROUP_OWNER) == OS_WORKGROUP_OWNER;
}

void
_os_workgroup_dispose(os_workgroup_t wg)
{
	dispatch_assert(wg->joined_cnt == 0);

	kern_return_t kr;
	uint64_t wg_state = os_atomic_load(&wg->wg_state, relaxed);
	if (_os_workgroup_is_configurable(wg_state)) {
		kr = work_interval_destroy(wg->wi);
	} else {
		kr = mach_port_mod_refs(mach_task_self(), wg->port, MACH_PORT_RIGHT_SEND, -1);
	}
	os_assumes(kr == KERN_SUCCESS);
	if (wg_state & OS_WORKGROUP_LABEL_NEEDS_FREE) {
		free((void *)wg->name);
	}
}

void
_os_workgroup_debug(os_workgroup_t wg, char *buf, size_t size)
{
	snprintf(buf, size, "wg[%p] = {xref = %d, ref = %d, name = %s}",
			(void *) wg, wg->do_xref_cnt + 1, wg->do_ref_cnt + 1, wg->name);
}

void
_os_workgroup_interval_dispose(os_workgroup_interval_t wgi)
{
	work_interval_instance_free(wgi->wii);
}

void
_os_workgroup_tsd_cleanup(void *ctxt) /* Destructor for the tsd key */
{
	os_workgroup_t wg = (os_workgroup_t) ctxt;
	if (wg != NULL) {
		char buf[512];
		snprintf(buf, sizeof(buf), "BUG IN CLIENT: Thread exiting without leaving workgroup '%s'", wg->name);

		os_crash(buf);
	}
}
