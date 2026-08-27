/* Real pthread_attr_* functions CFRunLoop.c/CFMachPort.c call --
 * extracted verbatim (each function cites its real origin line) from
 * third_party/libpthread/src/pthread.c rather than compiling that whole
 * file, matching this project's established "extract just the needed
 * real functions" pattern (see cf_mach_port_syscalls.c's own header
 * comment for the identical reasoning, and
 * pthread_main_thread_bootstrap.c for the precedent this project
 * already set for this exact source file: pthread.c bundles real
 * pthread_create() stack/mach-vm allocation machinery that hits a real
 * header conflict in this project's build -- build_libpthread.sh's own
 * header comment has the full story -- so the whole file is never
 * compiled here; these 4 functions have no such dependency at all, pure
 * struct-field manipulation, zero syscalls).
 *
 * pthread_attr_setscope/pthread_mutexattr_destroy/
 * pthread_attr_set_qos_class_np/qos_class_main are NOT here:
 * pthread_attr_set_qos_class_np/qos_class_main live in
 * third_party/libpthread/src/qos.c, which build_libpthread.sh's own
 * header comment already documents as unreachable ("needs real
 * bsdthread_register") -- a real, already-diagnosed kernel-registration
 * wall, not something to route around with a stub. pthread_mutexattr_destroy
 * lives in pthread_mutex.c, already fully compiled into libpthread.a --
 * its absence from this trial link is a real libSystem.B.dylib
 * export-parity gap (the function exists, just isn't exported from the
 * flattened static-stack-as-dylib this project's libSystem.B.dylib
 * currently is), same already-documented category as _getegid/
 * _uuid_generate_random/etc, not a missing-source problem -- not
 * addressed here.
 */
/* Real internal.h (third_party/libpthread/src/internal.h), same as
 * pthread.c's own real first include -- NOT <pthread.h>: the public SDK
 * header's pthread_attr_t is an opaque byte-array
 * (_opaque_pthread_attr_t), while pthread.c's own real code (faithfully
 * copied below) operates on the real internal struct with named fields
 * (.sig/.detached/.inherit/.policy) that internal.h actually defines --
 * confirmed the hard way: a first attempt using plain <pthread.h>
 * failed to compile with "no member named 'sig'" against the opaque
 * type, not assumed correct upfront. */
#include "internal.h"
#include <sched.h>
#include <errno.h>

/* Real signature/policy constants from third_party/libpthread's own
 * private headers -- not guessed, matches pthread.c's own real values
 * exactly (pthread/pthread_impl.h / bsdthread_private.h). */
#ifndef _PTHREAD_ATTR_SIG
#define _PTHREAD_ATTR_SIG 0x74786274
#endif
#ifndef _PTHREAD_DEFAULT_INHERITSCHED
#define _PTHREAD_DEFAULT_INHERITSCHED PTHREAD_INHERIT_SCHED
#endif
#ifndef _PTHREAD_DEFAULT_POLICY
#define _PTHREAD_DEFAULT_POLICY SCHED_OTHER
#endif

/* Real origin: libpthread/src/pthread.c:93. Real qosclass default value
 * omitted (that field is part of this project's still-unreachable QoS
 * machinery, see qos.c note above) -- zero-initialized instead, matching
 * this struct's own real fallback behavior whenever schedset is false
 * (_pthread_attr_get_schedparam already falls back to a computed
 * default rather than trusting this field blindly). */
static const pthread_attr_t _pthread_attr_default = {
	.sig       = _PTHREAD_ATTR_SIG,
	.stacksize = 0,
	.detached  = PTHREAD_CREATE_JOINABLE,
	.inherit   = _PTHREAD_DEFAULT_INHERITSCHED,
	.policy    = _PTHREAD_DEFAULT_POLICY,
};

/* Real origin: libpthread/src/pthread.c:303 */
int
pthread_attr_init(pthread_attr_t *attr)
{
	*attr = _pthread_attr_default;
	return 0;
}

/* Real origin: libpthread/src/pthread.c:235 */
int
pthread_attr_destroy(pthread_attr_t *attr)
{
	int ret = EINVAL;
	if (attr->sig == _PTHREAD_ATTR_SIG) {
		attr->sig = 0;
		ret = 0;
	}
	return ret;
}

/* Real origin: libpthread/src/pthread.c:309 */
int
pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate)
{
	int ret = EINVAL;
	if (attr->sig == _PTHREAD_ATTR_SIG &&
			(detachstate == PTHREAD_CREATE_JOINABLE ||
			detachstate == PTHREAD_CREATE_DETACHED)) {
		attr->detached = detachstate;
		ret = 0;
	}
	return ret;
}

/* Real origin: libpthread/src/pthread.c:368 */
int
pthread_attr_setscope(pthread_attr_t *attr, int scope)
{
	int ret = EINVAL;
	if (attr->sig == _PTHREAD_ATTR_SIG) {
		if (scope == PTHREAD_SCOPE_SYSTEM) {
			ret = 0;
		} else if (scope == PTHREAD_SCOPE_PROCESS) {
			ret = ENOTSUP;
		}
	}
	return ret;
}
