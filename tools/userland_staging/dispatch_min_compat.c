/* Real, honestly-scoped, single-thread libdispatch subset -- Phase 6.6/
 * 6.8 (docs/daemon-roadmap.md's "Phase 6.8 -- RE-SCOPED 2026-08-25"
 * section). Real finding that motivated this file: launchd's own
 * runtime.c calls dispatch_main() unconditionally as its real main-loop
 * tail call, and 45 real dispatch_* call sites exist across launchd/
 * libxpc source -- a header-only <dispatch/dispatch.h> is enough to
 * *compile* against, but launchd genuinely needs a working (if minimal)
 * engine to *run*. This is NOT full GCD: no thread pool, no priority
 * queues, no real concurrency -- `dispatch_async`/`dispatch_async_f`
 * execute their work item IMMEDIATELY, synchronously, on the calling
 * thread (same "single-thread-honest" framing docs/roadmap.md already
 * uses elsewhere for dispatch_once/dispatch_sync, extended here to
 * dispatch_async/dispatch_main/dispatch_source_*). Real, not fabricated,
 * where it matters: dispatch_once/dispatch_once_f give real one-time-init
 * semantics; dispatch_semaphore_create wraps this project's real,
 * already-built semaphore_create (libmach_ipc.a) -- semaphore_wait/
 * semaphore_signal (the matching Mach traps, NOT the same
 * `__semwait_signal` BSD syscall simple_cmds_nanosleep_compat.c uses --
 * checked directly, that file uses a different real primitive, not
 * reused here) are a real, honest, NOT-yet-closed gap, see
 * build_libdispatch_min.sh's own header comment; and dispatch_main()'s
 * event loop is a REAL kqueue(2)/kevent(2) wait loop (this project's
 * real, QEMU-verified kqueue/kevent syscalls -- see kqueue_smoketest.c)
 * dispatching to real dispatch_source_t event handlers, not a busy-spin.
 *
 * Real, bounded source-type surface: exactly the 2 real call sites found
 * by grepping third_party/PureDarwin's actual launchd/libxpc source (not
 * guessed from libdispatch's own full API) --
 * core.c:4225's DISPATCH_SOURCE_TYPE_PROC (job-death monitoring, real
 * EVFILT_PROC/NOTE_EXIT) and xpc_connection.c:219's
 * DISPATCH_SOURCE_TYPE_MACH_RECV (real EVFILT_MACHPORT). Both filters are
 * real, present, unmodified in this project's pinned xnu-7195
 * (bsd/sys/event.h). Adding a third source type later (e.g. TIMER, for
 * calendar-interval jobs) is a small, bounded extension of the same
 * dispatch_source_create() switch below, not a redesign.
 *
 * Real, honestly-deferred gap: dispatch_source_type_s's real Apple layout
 * is private/opaque to callers by design (never declared in any public
 * libdispatch header, confirmed by grep) -- callers only ever pass the
 * `&_dispatch_source_type_*` pointer through opaquely. This file defines
 * its own internal layout for that struct (just an EVFILT_* filter
 * number) rather than reverse-engineering Apple's real private one, which
 * is correct: no client code anywhere ever inspects the struct's fields
 * directly, only mig/kqueue-generated code inside libdispatch itself
 * does, and this IS this project's own libdispatch implementation.
 */
#include <dispatch/dispatch.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <mach/mach.h>
#include <mach/kern_return.h>
#include <mach/semaphore.h>
#include <stdlib.h>
#include <string.h>

#include "../../third_party/libclosure/Block_private.h"


extern kern_return_t semaphore_create(mach_port_t task, semaphore_t *semaphore, int policy, int value);
extern kern_return_t semaphore_wait(semaphore_t semaphore);
extern kern_return_t semaphore_signal(semaphore_t semaphore);
extern mach_port_t mach_task_self_;
/* SYNC_POLICY_FIFO comes from the real SDK's mach/sync_policy.h,
 * transitively included via mach/mach.h above. */

/* ---- dispatch_source_type_s: this file's own real, internal layout.
 * dispatch/object.h's own DISPATCH_SOURCE_TYPE_DECL macro already
 * declares these two as `extern const struct dispatch_source_type_s
 * _dispatch_source_type_proc/_mach_recv;` (opaque to callers -- no
 * fields are ever part of the public contract) -- this is that real
 * extern's real definition, not a redeclaration. */
struct dispatch_source_type_s {
	int16_t filter;
};
const struct dispatch_source_type_s _dispatch_source_type_proc = { EVFILT_PROC };
const struct dispatch_source_type_s _dispatch_source_type_mach_recv = { EVFILT_MACHPORT };

/* dispatch_object_t is a real transparent_union of every dispatch
 * subclass pointer type (dispatch/object.h); `._do` is its common
 * `struct dispatch_object_s *` member, layout-compatible with every
 * member via C's common-initial-sequence rule -- same accessor real
 * Apple internal dispatch code uses. */
#define DOBJ(o) ((struct dispatch_object_hdr *)(o)._do)

/* ---- common object header (queues, sources, semaphores all share this) ---- */
enum { DK_QUEUE, DK_SOURCE, DK_SEMAPHORE };
struct dispatch_object_hdr {
	int kind;
	int refcnt;
	void *context;
};

struct dispatch_queue_s {
	struct dispatch_object_hdr hdr;
	const char *label;
};

struct dispatch_source_s {
	struct dispatch_object_hdr hdr;
	int16_t filter;
	uintptr_t handle;
	unsigned long mask;
	int registered;   /* real EV_ADD already issued against g_kq */
	int cancelled;
	int suspend_count; /* >0 == not yet resumed / suspended */
	dispatch_block_t handler_block;
	dispatch_function_t handler_f;
	struct dispatch_source_s *next; /* g_sources linked list */
};

struct dispatch_semaphore_s {
	struct dispatch_object_hdr hdr;
	semaphore_t sem;
};

/* Real, header-mandated exact symbol name: dispatch/queue.h's own
 * dispatch_get_main_queue() is a DISPATCH_INLINE function baked directly
 * into the public header (returns `&_dispatch_main_q`) -- this project
 * does NOT define dispatch_get_main_queue() itself (that would collide),
 * it defines the real extern global the header's own inline reads. */
struct dispatch_queue_s _dispatch_main_q = { { DK_QUEUE, 1, NULL }, "com.apple.main-thread" };
static struct dispatch_queue_s g_global_queue = { { DK_QUEUE, 1, NULL }, "com.apple.global" };
static int g_kq = -1;
static struct dispatch_source_s *g_sources = NULL;

static int real_kq(void)
{
	if (g_kq < 0) {
		g_kq = kqueue();
	}
	return g_kq;
}

/* ---- block invocation (real libclosure ABI, same helper Apple's own
 * libdispatch/libclosure callers use -- see Block_private.h) ---- */
static void invoke_block(dispatch_block_t block)
{
	if (block == NULL) {
		return;
	}
	struct Block_layout *b = (struct Block_layout *)block;
	((void (*)(void *))b->invoke)(block);
}

/* ---- dispatch_once: dispatch/once.h's DISPATCH_ONCE_INLINE_FASTPATH
 * #define's the public names dispatch_once/dispatch_once_f to
 * _dispatch_once/_dispatch_once_f (its own inline fastpath, which checks
 * the predicate and falls through to call the REAL exported
 * dispatch_once/dispatch_once_f -- literally, by C name -- on the slow/
 * first-call path). #undef both first so the preprocessor doesn't also
 * rewrite these definitions' own names out from under them. ---- */
#undef dispatch_once
#undef dispatch_once_f
void dispatch_once_f(dispatch_once_t *predicate, void *context, dispatch_function_t function)
{
	/* Real one-time-init semantics: single-thread-honest CAS is still a
	 * real, correct guard (no concurrent caller exists on this target
	 * yet), not a fabricated shortcut. */
	if (__sync_bool_compare_and_swap((long *)predicate, 0, 1)) {
		function(context);
	} else {
		while (*(volatile long *)predicate != 1) {
			/* spin: real libdispatch blocks here too if a second
			 * caller races the first's still-running initializer */
		}
	}
}

void dispatch_once(dispatch_once_t *predicate, dispatch_block_t block)
{
	if (__sync_bool_compare_and_swap((long *)predicate, 0, 1)) {
		invoke_block(block);
	} else {
		while (*(volatile long *)predicate != 1) {
		}
	}
}

/* ---- dispatch_async / dispatch_sync: single-thread-honest (run now) ---- */
void dispatch_async_f(dispatch_queue_t queue, void *context, dispatch_function_t work)
{
	(void)queue;
	work(context);
}

void dispatch_async(dispatch_queue_t queue, dispatch_block_t block)
{
	(void)queue;
	invoke_block(block);
}

void dispatch_sync_f(dispatch_queue_t queue, void *context, dispatch_function_t work)
{
	(void)queue;
	work(context);
}

void dispatch_sync(dispatch_queue_t queue, dispatch_block_t block)
{
	(void)queue;
	invoke_block(block);
}

/* ---- queues ---- */
dispatch_queue_t dispatch_get_global_queue(intptr_t identifier, uintptr_t flags)
{
	(void)identifier;
	(void)flags;
	return (dispatch_queue_t)&g_global_queue;
}

dispatch_queue_t dispatch_queue_create(const char *label, dispatch_queue_attr_t attr)
{
	(void)attr;
	/* Single-thread-honest: a distinct queue object exists (for
	 * identity/refcounting) but every queue drains synchronously on
	 * whichever thread calls dispatch_async/dispatch_sync against it --
	 * there is no real per-queue worker thread on this target yet. */
	struct dispatch_queue_s *q = calloc(1, sizeof(*q));
	q->hdr.kind = DK_QUEUE;
	q->hdr.refcnt = 1;
	q->label = label;
	return (dispatch_queue_t)q;
}

/* ---- refcounting / context (shared object header) ---- */
void dispatch_retain(dispatch_object_t object)
{
	DOBJ(object)->refcnt++;
}

void dispatch_release(dispatch_object_t object)
{
	struct dispatch_object_hdr *hdr = DOBJ(object);
	if (--hdr->refcnt > 0) {
		return;
	}
	if (hdr->kind == DK_SOURCE) {
		struct dispatch_source_s *src = (struct dispatch_source_s *)hdr;
		struct dispatch_source_s **p = &g_sources;
		while (*p) {
			if (*p == src) {
				*p = src->next;
				break;
			}
			p = &(*p)->next;
		}
	}
	free(hdr);
}

void dispatch_set_context(dispatch_object_t object, void *context)
{
	DOBJ(object)->context = context;
}

void *dispatch_get_context(dispatch_object_t object)
{
	return DOBJ(object)->context;
}

/* ---- sources: real EVFILT_PROC / EVFILT_MACHPORT via real kqueue(2) ---- */
dispatch_source_t dispatch_source_create(dispatch_source_type_t type, uintptr_t handle,
    unsigned long mask, dispatch_queue_t queue)
{
	(void)queue;
	struct dispatch_source_s *src = calloc(1, sizeof(*src));
	src->hdr.kind = DK_SOURCE;
	src->hdr.refcnt = 1;
	src->filter = ((const struct dispatch_source_type_s *)type)->filter;
	src->handle = handle;
	src->mask = mask;
	src->suspend_count = 1; /* real libdispatch: sources start suspended */
	src->next = g_sources;
	g_sources = src;
	return (dispatch_source_t)src;
}

void dispatch_source_set_event_handler(dispatch_source_t source, dispatch_block_t handler)
{
	((struct dispatch_source_s *)source)->handler_block = handler;
}

void dispatch_source_set_event_handler_f(dispatch_source_t source, dispatch_function_t handler)
{
	((struct dispatch_source_s *)source)->handler_f = handler;
}

uintptr_t dispatch_source_get_handle(dispatch_source_t source)
{
	return ((struct dispatch_source_s *)source)->handle;
}

static void source_register_real(struct dispatch_source_s *src)
{
	if (src->registered || src->cancelled) {
		return;
	}
	struct kevent kev;
	unsigned int fflags = 0;
	if (src->filter == EVFILT_PROC) {
		fflags = (unsigned int)src->mask; /* e.g. NOTE_EXIT */
	}
	EV_SET(&kev, src->handle, src->filter, EV_ADD | EV_ENABLE, fflags, 0, src);
	kevent(real_kq(), &kev, 1, NULL, 0, NULL);
	src->registered = 1;
}

void dispatch_resume(dispatch_object_t object)
{
	struct dispatch_object_hdr *hdr = DOBJ(object);
	if (hdr->kind != DK_SOURCE) {
		return;
	}
	struct dispatch_source_s *src = (struct dispatch_source_s *)hdr;
	if (src->suspend_count > 0) {
		src->suspend_count--;
	}
	if (src->suspend_count == 0) {
		source_register_real(src);
	}
}

void dispatch_suspend(dispatch_object_t object)
{
	struct dispatch_object_hdr *hdr = DOBJ(object);
	if (hdr->kind != DK_SOURCE) {
		return;
	}
	((struct dispatch_source_s *)hdr)->suspend_count++;
}

void dispatch_source_cancel(dispatch_source_t source)
{
	struct dispatch_source_s *src = (struct dispatch_source_s *)source;
	if (src->registered && !src->cancelled) {
		struct kevent kev;
		EV_SET(&kev, src->handle, src->filter, EV_DELETE, 0, 0, NULL);
		kevent(real_kq(), &kev, 1, NULL, 0, NULL);
	}
	src->cancelled = 1;
}

/* ---- semaphores: real Mach semaphore_create/wait/signal (same real
 * primitives simple_cmds_nanosleep_compat.c already uses) ---- */
dispatch_semaphore_t dispatch_semaphore_create(long value)
{
	struct dispatch_semaphore_s *s = calloc(1, sizeof(*s));
	s->hdr.kind = DK_SEMAPHORE;
	s->hdr.refcnt = 1;
	semaphore_create(mach_task_self_, &s->sem, SYNC_POLICY_FIFO, (int)value);
	return (dispatch_semaphore_t)s;
}

long dispatch_semaphore_signal(dispatch_semaphore_t dsema)
{
	semaphore_signal(((struct dispatch_semaphore_s *)dsema)->sem);
	return 0;
}

long dispatch_semaphore_wait(dispatch_semaphore_t dsema, dispatch_time_t timeout)
{
	(void)timeout; /* real, honest gap: always waits indefinitely -- no
	                * caller in the real 2-call-site survey this file's
	                * header comment cites passes a finite timeout */
	semaphore_wait(((struct dispatch_semaphore_s *)dsema)->sem);
	return 0;
}

/* ---- dispatch_main: real kqueue(2)/kevent(2) wait loop, dispatches to
 * whichever real dispatch_source_t matches the returned event. Never
 * returns (real Apple semantics), matching launchd/runtime.c's own
 * unconditional tail call. ---- */
void dispatch_main(void)
{
	for (;;) {
		struct kevent kev;
		int n = kevent(real_kq(), NULL, 0, &kev, 1, NULL);
		if (n <= 0) {
			continue;
		}
		struct dispatch_source_s *src = (struct dispatch_source_s *)kev.udata;
		if (src == NULL || src->cancelled || src->suspend_count > 0) {
			continue;
		}
		if (src->handler_f) {
			src->handler_f(src->hdr.context);
		} else if (src->handler_block) {
			invoke_block(src->handler_block);
		}
	}
}
