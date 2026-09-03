/* Real os_transaction_t (os/transaction_private.h) has no source
 * anywhere in this project's pinned checkouts -- it's part of Apple's
 * closed-source libxpc/libdispatch private SPI used by launchd-
 * supervised daemons to defer idle-exit while work is pending
 * ("com.apple.syslogd" / "com.apple.syslogd.message" transactions in
 * daemon.c). This project's PID-1 has no idle-exit lifecycle for
 * spawned daemons at all -- syslogd runs until killed, never
 * idle-exits -- so there is nothing for a real implementation to hook
 * into regardless. Honest no-op stand-in: create/end are real,
 * callable, always-succeed no-ops.
 *
 * DAR-222: the returned handle used to be a bare `(void *)1` sentinel,
 * with a comment claiming it was "never dereferenced". That was wrong,
 * and it was a real crash, not a cosmetic inaccuracy. Real
 * os_transaction_t is an os_object subtype, so daemon.c genuinely
 * releases it -- `os_release(transaction)` at the tail of
 * process_message()'s work-queue block (daemon.c:983) and
 * `os_release(main_transaction)` in the client-count path
 * (daemon.c:342). Real libdispatch's release path starts by reading
 * the object's refcount fields (`_os_object_s`: isa at +0,
 * os_obj_ref_cnt at +8, os_obj_xref_cnt at +12,
 * third_party/libdispatch/src/object_internal.h:437), so releasing
 * `(void *)1` read address 0x9 and took a real EXC_BAD_ACCESS. Found
 * from a real faulting-address kernel probe once the crash before it
 * (pthread_create() leaving pthread_get_stackaddr_np() NULL) was
 * fixed and syslogd got far enough to actually process a message.
 *
 * The fix is to hand back something real libdispatch can genuinely
 * retain and release: an `_os_object_s`-shaped singleton whose two
 * refcounts are both _OS_OBJECT_GLOBAL_REFCNT (INT_MAX, defined in
 * third_party/libdispatch/os/object_private.h:69). That is real
 * libdispatch's own encoding for "global object, never disposed" --
 * _os_atomic_refcnt_perform2o (object_internal.h:597) short-circuits
 * on it, so every retain/release becomes a genuine no-op that never
 * touches the isa pointer and never reaches any dispose path. Not a
 * fabricated success: it is exactly the representation real
 * libdispatch uses for its own process-lifetime objects, which is
 * precisely what a never-idle-exiting daemon's transaction is here.
 */
#ifndef _OS_TRANSACTION_PRIVATE_COMPAT_H_
#define _OS_TRANSACTION_PRIVATE_COMPAT_H_

#include <limits.h>

/* Real _os_object_s layout (libdispatch src/object_internal.h:437 ->
 * os/object_private.h's _OS_OBJECT_HEADER): isa pointer, then two int
 * refcounts. Reproduced here rather than included because those are
 * libdispatch-internal headers that are not on syslogd's include path
 * and pull in the whole dispatch internal world. */
struct os_transaction_s {
	const void *os_obj_isa;
	int os_obj_ref_cnt;
	int os_obj_xref_cnt;
};
typedef struct os_transaction_s *os_transaction_t;

/* _OS_OBJECT_GLOBAL_REFCNT, os/object_private.h:69. */
static struct os_transaction_s _os_transaction_compat_singleton = {
	(const void *)0, INT_MAX, INT_MAX
};

static inline os_transaction_t
os_transaction_create(const char *name)
{
	(void)name;
	return &_os_transaction_compat_singleton;
}

static inline void
os_transaction_end(os_transaction_t transaction)
{
	(void)transaction;
}

#endif /* _OS_TRANSACTION_PRIVATE_COMPAT_H_ */
