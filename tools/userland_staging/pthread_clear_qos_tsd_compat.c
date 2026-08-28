/* Real _pthread_clear_qos_tsd(), verbatim from
 * third_party/libpthread/src/pthread.c:1900-1923 (real Apple source,
 * that file's own comment there: "Libsystem knows about this symbol and
 * exports it to libsyscall") -- pthread.c itself is deliberately NOT
 * compiled wholesale by build_libpthread.sh (see that script's own
 * header comment: it bundles three separate things, most of which is
 * dead code under this project's kernel-level pthread_create()
 * constraint), so this one real, self-contained function is extracted
 * the same way pthread_main_thread_bootstrap.c's own functions already
 * are -- not reinvented, not simplified, same real algorithm, same real
 * dependencies (__pthread_head/_pthread_list_lock, already real and
 * defined in pthread_main_thread_bootstrap.c; _pthread_tsd_slot/
 * _pthread_mach_thread_self_direct/_pthread_setspecific_direct/
 * _pthread_unspecified_priority are all real static-inline functions
 * already reachable via "internal.h" -> pthread/qos_private.h ->
 * pthread/priority_private.h, the same header chain every other
 * apple_pthread/*.c file in this tier already compiles against). */
#include "internal.h"

void
_pthread_clear_qos_tsd(mach_port_t port)
{
	pthread_priority_t pp = _pthread_unspecified_priority();

	if (port == MACH_PORT_NULL || _pthread_mach_thread_self_direct() == port) {
		/* Clear the current thread's TSD, that can be done inline. */
		_pthread_setspecific_direct(_PTHREAD_TSD_SLOT_PTHREAD_QOS_CLASS, pp);
	} else {
		pthread_t p;

		_pthread_lock_lock(&_pthread_list_lock);

		TAILQ_FOREACH(p, &__pthread_head, tl_plist) {
			mach_port_t kp = _pthread_tsd_slot(p, MACH_THREAD_SELF);
			if (port == kp) {
				_pthread_tsd_slot(p, PTHREAD_QOS_CLASS) = pp;
				break;
			}
		}

		_pthread_lock_unlock(&_pthread_list_lock);
	}
}
