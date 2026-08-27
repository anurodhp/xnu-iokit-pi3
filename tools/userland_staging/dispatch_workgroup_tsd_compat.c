/* Real gap found 2026-08-27 bringing up syslogd (2nd crash after fixing
 * shims/yield.c): libdispatch's own real _libdispatch_init() (src/
 * queue.c:7920) unconditionally calls
 * `pthread_key_create(&_os_workgroup_key, _os_workgroup_tsd_cleanup)` --
 * a real, always-run reference (not gated behind actual os_workgroup API
 * usage), so the symbol must resolve even though nothing in this
 * project ever creates a real os_workgroup_t. The real definition
 * (src/workgroup.c:135) is short, but that whole 1076-line file has its
 * own real <mach/work_interval.h>-based dependency chain
 * (work_interval_instance_free, etc) this project doesn't need just for
 * this one TSD destructor -- same "don't chase an unrelated 30-for-3
 * trade" reasoning build_corefoundation.sh's own CFBundle.c revert note
 * already established. Real, exact behavior reproduced instead of
 * pulling in the whole file: the real function's only job is a
 * defensive check ("did a thread exit while still joined to a
 * workgroup?"); since this project never creates a real os_workgroup_t
 * anywhere, `ctxt` is always NULL in practice and this is provably a
 * real no-op here, not a functional gap -- if it were ever non-NULL
 * (workgroup API actually used), the real behavior is a fatal
 * diagnostic crash, reproduced below without needing the full
 * os_workgroup_t struct layout. */
#include <stdlib.h>

void _os_workgroup_tsd_cleanup(void *ctxt) {
    if (ctxt != NULL) {
        abort();
    }
}
