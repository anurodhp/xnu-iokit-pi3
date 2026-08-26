/* Real, functionally-exercising trial main for build_libdispatch_real.sh's
 * own trial link -- NOT a trivial empty main(), which this project
 * already learned (Phase 6.8's libxpc trial) proves nothing: static
 * linking only pulls in archive members actually referenced
 * transitively from real code. Exercises dispatch_once/dispatch_queue_create/
 * dispatch_async/dispatch_source_create/dispatch_semaphore_create --
 * the real, most commonly used real libdispatch entry points launchd/
 * libxpc's own actual source already depends on (see
 * docs/daemon-roadmap.md's Phase 6.8 sections).
 */
#include <dispatch/dispatch.h>

static dispatch_once_t once_pred;

int
main(void)
{
	dispatch_once(&once_pred, ^{
	});

	dispatch_queue_t q = dispatch_queue_create("com.iokit.trial", NULL);
	dispatch_async(q, ^{
	});

	dispatch_semaphore_t sem = dispatch_semaphore_create(0);
	dispatch_semaphore_signal(sem);
	dispatch_semaphore_wait(sem, DISPATCH_TIME_NOW);

	dispatch_source_t src = dispatch_source_create(
	    DISPATCH_SOURCE_TYPE_PROC, 0, 0, dispatch_get_main_queue());
	dispatch_resume(src);

	return 0;
}
