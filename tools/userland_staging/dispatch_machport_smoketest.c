/* Real standalone dispatch Mach-receive-source smoketest (DAR-266/DAR-268).
 *
 * ---- OUTCOME, so nobody re-runs this investigation ----
 *
 * Every mode below PASSES on this port (real QEMU boot, 2026-09-08,
 * qemu/dar266_dmp_boot1.log): the Mach-receive dispatch source really
 * receives a real Mach message, and the real notify_register_dispatch()
 * really registers, posts and delivers. DAR-268's hypothesis -- that
 * notify(3)'s client-side dispatch bring-up was broken and was causing
 * DAR-266's crash -- is FALSIFIED, and DAR-268 is closed. DAR-266's
 * actual cause was a stale build artifact in the gitignored
 * libc_build/ (configd.macho was shipped against a libconfigd_plugins.a
 * / libSystemConfiguration.dylib that had not been rebuilt after their
 * sources were reverted); rebuilding all three from the unchanged tree
 * made the crash disappear. This file stays as a REGRESSION TEST for
 * the machinery it proves works -- run it whenever anything under
 * libdispatch's event backend, libsystem_notify, or the kernel's
 * EVFILT_MACHPORT filter changes.
 *
 * ---- Original motivation (kept: it is why the modes are what they are) ----
 *
 * DAR-266 reported configd (pid 3) crash-looping with `pc = lr = fp = x0
 * = x1 = 0`, `esr 0x82000005` (EC 0x20, instruction abort from EL0 --
 * a jump through a NULL function pointer), on a freshly-spawned
 * non-main worker thread. DAR-266's own comment thread localized the
 * crash to the span
 *
 *   load_IPMonitor()            configd/Plugins/IPMonitor/ip_plugin.c:9208
 *    -> prefs_changed_callback_init()                      ip_plugin.c:9227
 *     -> IPMonitorControlPrefsInit()   Plugins/common/IPMonitorControlPrefs.c:153
 *      -> enable_prefs_observer()               IPMonitorControlPrefs.c:102
 *       -> _scprefs_observer_watch()  SystemConfiguration.fproj/scprefs_observer.c:283
 *        -> _prefs_observer_init()                     scprefs_observer.c:235
 *         -> notify_register_dispatch()      Libnotify/notify_client.c:2087
 *
 * -- but could not pin the faulting instruction, because the crashing
 * thread has no usable frame.
 *
 * ---- What this test isolates, and why it is not "just call notify" ----
 *
 * notify_register_dispatch() is the FIRST caller in this whole project's
 * userland that reaches _notify_lib_init_locked()'s NOTIFY_OPT_DISPATCH
 * branch (real, unmodified third_party/Libnotify/notify_client.c:879-1012).
 * That branch is gated on _dispatch_is_multithreaded(), so the existing
 * notifyd_roundtrip_smoketest -- a straight-line, single-threaded
 * program using notify_register_check() only -- never executed a single
 * line of it. Everything in it is therefore first-run code here:
 *
 *   dispatch_source_create(DISPATCH_SOURCE_TYPE_PROC, pid, PROC_EXIT, q)
 *   dispatch_source_set_event_handler_f(...) / dispatch_resume(...)
 *   _notify_generate_common_port()                      (MIG round trip)
 *   dispatch_source_create(DISPATCH_SOURCE_TYPE_MACH_RECV, port, 0, q)
 *   dispatch_set_context / dispatch_source_set_event_handler_f
 *   dispatch_source_set_cancel_handler(^{ ... })        (a real Block)
 *   dispatch_resume(...)
 *
 * DISPATCH_SOURCE_TYPE_MACH_RECV in particular appears exactly once in
 * all of tools/userland_staging/ before this file, and there only inside
 * a comment (dispatch_min_compat.c) -- no test has ever created an
 * EVFILT_MACHPORT dispatch source on this port.
 *
 * So the modes below walk that list one primitive at a time, each in its
 * own process (argv-driven, exactly like mach_port_construct_smoketest),
 * so a mode that dies takes nothing else with it and the last line
 * printed names the primitive that failed:
 *
 *   dispatch_machport_smoketest mt          # dispatch_async_f + _dispatch_is_multithreaded()
 *   dispatch_machport_smoketest proc        # DISPATCH_SOURCE_TYPE_PROC create+resume
 *   dispatch_machport_smoketest recv-create # MACH_RECV source create only (no resume)
 *   dispatch_machport_smoketest recv-resume # ... + handler_f + dispatch_resume
 *   dispatch_machport_smoketest recv-block  # ... + a real Block cancel handler
 *   dispatch_machport_smoketest recv-deliver# ... + send one real Mach message to it
 *   dispatch_machport_smoketest notify      # the real notify_register_dispatch() call
 *   dispatch_machport_smoketest all         # mt, proc, recv-create, recv-resume (safe order)
 *
 * `recv-deliver` is the end-to-end one: it allocates a receive right,
 * makes a send right on it, hands the receive right to dispatch, sends
 * itself one message, and waits for the C event handler to run. If that
 * handler never runs, the EVFILT_MACHPORT plumbing is broken but
 * silent -- which is the failure mode a "clean nm, clean link, daemon
 * just hangs" bug on this port always takes.
 *
 * `notify` is the real call configd makes, in the smallest possible
 * program: it separates a fault in Libnotify's own bring-up (the
 * _notify_generate_common_port() MIG round trip, or the regen/proc
 * source) from a fault in the dispatch source machinery underneath it.
 * It needs a real, running notifyd -- a failure here with every other
 * mode passing means notifyd, not dispatch.
 *
 * Output is raw write(2), never printf() -- see kqueue_smoketest.c's own
 * header comment for the real, separate, still-undiagnosed printf()
 * crash in this minimal link recipe.
 */
#include <mach/mach_types.h>
#include <mach/kern_return.h>
#include <mach/port.h>
#include <mach/message.h>
#include <unistd.h>
#include <string.h>

/* Real declarations rather than <mach/mach.h> / <dispatch/dispatch.h>:
 * this binary is built -ffreestanding against the same minimal recipe as
 * every other smoketest here, and both umbrella headers drag in surface
 * (and, for dispatch, an OS_OBJECT_USE_OBJC decision) this test has no
 * use for. Signatures copied from the real pinned sources:
 * third_party/xnu-7195/libsyscall/mach/, third_party/libdispatch/dispatch/
 * source.h+queue.h, third_party/Libnotify/notify.h. */
extern mach_port_t mach_task_self_;

extern kern_return_t mach_port_allocate(ipc_space_t, mach_port_right_t,
    mach_port_name_t *);
extern kern_return_t mach_port_insert_right(ipc_space_t, mach_port_name_t,
    mach_port_t, mach_msg_type_name_t);
extern kern_return_t mach_port_deallocate(ipc_space_t, mach_port_name_t);
extern mach_msg_return_t mach_msg(mach_msg_header_t *, mach_msg_option_t,
    mach_msg_size_t, mach_msg_size_t, mach_port_name_t, mach_msg_timeout_t,
    mach_port_name_t);

/* dispatch_source_type_s stays incomplete on purpose -- only its address
 * is ever used, and the real struct layout is libdispatch-internal. */
struct dispatch_source_type_s;
extern const struct dispatch_source_type_s _dispatch_source_type_mach_recv;
extern const struct dispatch_source_type_s _dispatch_source_type_proc;

extern void *dispatch_get_global_queue(long identifier, unsigned long flags);
extern void *dispatch_source_create(const struct dispatch_source_type_s *type,
    unsigned long handle, unsigned long mask, void *queue);
extern void dispatch_source_set_event_handler_f(void *source,
    void (*handler)(void *));
extern void dispatch_source_set_cancel_handler_f(void *source,
    void (*handler)(void *));
extern void dispatch_source_set_cancel_handler(void *source, void (^handler)(void));
extern void dispatch_set_context(void *object, void *context);
extern void dispatch_resume(void *object);
extern void dispatch_source_cancel(void *source);
extern void dispatch_release(void *object);
extern void dispatch_async_f(void *queue, void *context, void (*work)(void *));
extern _Bool _dispatch_is_multithreaded(void);

extern int notify_register_dispatch(const char *name, int *out_token,
    void *queue, void (^handler)(int token));
extern int notify_post(const char *name);
extern int notify_cancel(int token);

extern int getpid(void);
extern int usleep(unsigned int);

/* Real values, third_party/libdispatch/dispatch/queue.h + source.h. */
#define DISPATCH_QUEUE_PRIORITY_HIGH  2
#define DISPATCH_PROC_EXIT            0x80000000u
/* Real sentinel dispatch_source_create() returns when dux_create() fails
 * (third_party/libdispatch/src/internal.h:546 --
 * `#define DISPATCH_BAD_INPUT ((void *_Nonnull)0)`, i.e. a plain NULL,
 * checked here under its real name rather than assumed). */
#define DISPATCH_BAD_INPUT            ((void *)0)
/* Real values, osfmk/mach/message.h / osfmk/mach/port.h. */
#define MACH_SEND_MSG_REAL            0x00000001
#define MACH_MSG_TYPE_MAKE_SEND_REAL  20
#define MACH_MSG_TYPE_COPY_SEND_REAL  19
#define MACH_PORT_RIGHT_RECEIVE_REAL  1

static int g_out = 2;
static int g_fail;

static void diag(const char *s)
{
	write(g_out, s, strlen(s));
}

static void diag_num(const char *label, long v)
{
	char buf[32];
	int i = (int)sizeof(buf);
	unsigned long u;
	int neg = 0;

	write(g_out, label, strlen(label));
	if (v < 0) { neg = 1; u = (unsigned long)(-v); } else { u = (unsigned long)v; }
	buf[--i] = '\n';
	if (u == 0) buf[--i] = '0';
	while (u) { buf[--i] = (char)('0' + (u % 10)); u /= 10; }
	if (neg) buf[--i] = '-';
	write(g_out, buf + i, (size_t)(sizeof(buf) - (size_t)i));
}

static void diag_hex(const char *label, unsigned long v)
{
	static const char hexd[] = "0123456789abcdef";
	char buf[32];
	int i = (int)sizeof(buf);

	write(g_out, label, strlen(label));
	buf[--i] = '\n';
	if (v == 0) buf[--i] = '0';
	while (v) { buf[--i] = hexd[v & 0xf]; v >>= 4; }
	buf[--i] = 'x';
	buf[--i] = '0';
	write(g_out, buf + i, (size_t)(sizeof(buf) - (size_t)i));
}

static volatile int g_async_ran;
static volatile int g_event_ran;
static volatile int g_cancel_ran;

static void async_marker(void *ctxt)
{
	(void)ctxt;
	g_async_ran = 1;
}

static void event_marker(void *ctxt)
{
	(void)ctxt;
	g_event_ran = 1;
}

static void cancel_marker(void *ctxt)
{
	(void)ctxt;
	g_cancel_ran = 1;
}

/* Real reason this exists as its own step: _notify_lib_init_locked()'s
 * NOTIFY_OPT_DISPATCH branch -- the one DAR-266's whole trace ends in --
 * is gated on exactly this predicate, and it only becomes true once
 * libdispatch has decided the process is multithreaded
 * (_dispatch_fork_becomes_unsafe(), reached from the first real root
 * queue use). A single dispatch_async_f() is the smallest real thing
 * that flips it. */
static int mode_multithreaded(void)
{
	void *q = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0);
	int spins;

	diag_hex("global queue: ", (unsigned long)q);
	diag_num("_dispatch_is_multithreaded() before any async: ",
	    _dispatch_is_multithreaded() ? 1 : 0);

	dispatch_async_f(q, 0, async_marker);
	for (spins = 0; spins < 200 && !g_async_ran; spins++) {
		usleep(10000);
	}
	diag_num("dispatch_async_f block ran: ", g_async_ran);
	if (!g_async_ran) {
		diag("  ^ FAIL: a plain dispatch_async_f never ran\n");
		g_fail++;
	}
	diag_num("_dispatch_is_multithreaded() after the async: ",
	    _dispatch_is_multithreaded() ? 1 : 0);
	if (!_dispatch_is_multithreaded()) {
		diag("  ^ NOTE: notify(3) would NOT take its NOTIFY_OPT_DISPATCH"
		     " branch in a process in this state\n");
	}
	return g_fail;
}

/* First of the two sources _notify_lib_init_locked() creates, and the
 * one it creates FIRST (notify_client.c:974, the NOTIFY_OPT_REGEN
 * "notifyd restarted" watchdog). EVFILT_PROC, not EVFILT_MACHPORT. */
static int mode_proc_source(void)
{
	void *q = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0);
	void *src;

	diag("creating DISPATCH_SOURCE_TYPE_PROC source on own pid...\n");
	src = dispatch_source_create(&_dispatch_source_type_proc,
	    (unsigned long)getpid(), DISPATCH_PROC_EXIT, q);
	diag_hex("  source = ", (unsigned long)src);
	if (src == DISPATCH_BAD_INPUT) {
		diag("  ^ FAIL: dispatch_source_create(PROC) returned"
		     " DISPATCH_BAD_INPUT (NULL)\n");
		g_fail++;
		return g_fail;
	}
	dispatch_source_set_event_handler_f(src, event_marker);
	diag("  set_event_handler_f ok, resuming...\n");
	dispatch_resume(src);
	usleep(300000);
	diag("  resumed and still alive\n");
	dispatch_source_cancel(src);
	usleep(100000);
	dispatch_release(src);
	diag("PROC source: OK\n");
	return g_fail;
}

/* Allocates the same shape of port notify(3) hands to dispatch: a real
 * receive right, with a send right made on it (notifyd does the
 * MAKE_SEND half server-side via __notify_generate_common_port; done
 * locally here so this test needs no daemon at all). */
static kern_return_t make_port(mach_port_name_t *out)
{
	kern_return_t kr = mach_port_allocate(mach_task_self_,
	    MACH_PORT_RIGHT_RECEIVE_REAL, out);

	diag_num("mach_port_allocate(RECEIVE) kr = ", kr);
	if (kr != 0) return kr;
	diag_hex("  port name = ", (unsigned long)*out);
	kr = mach_port_insert_right(mach_task_self_, *out, *out,
	    MACH_MSG_TYPE_MAKE_SEND_REAL);
	diag_num("mach_port_insert_right(MAKE_SEND) kr = ", kr);
	return kr;
}

static void *recv_source_common(int with_handler, int with_block_cancel,
    mach_port_name_t *out_port)
{
	void *q = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0);
	mach_port_name_t port = 0;
	void *src;

	if (make_port(&port) != 0) {
		diag("  ^ FAIL: could not build the receive/send right pair\n");
		g_fail++;
		return 0;
	}
	*out_port = port;

	diag("creating DISPATCH_SOURCE_TYPE_MACH_RECV source...\n");
	src = dispatch_source_create(&_dispatch_source_type_mach_recv,
	    (unsigned long)port, 0, q);
	diag_hex("  source = ", (unsigned long)src);
	if (src == DISPATCH_BAD_INPUT) {
		diag("  ^ FAIL: dispatch_source_create(MACH_RECV) returned"
		     " DISPATCH_BAD_INPUT (NULL)\n");
		g_fail++;
		return 0;
	}
	dispatch_set_context(src, (void *)src);
	if (with_handler) {
		dispatch_source_set_event_handler_f(src, event_marker);
		diag("  set_event_handler_f ok\n");
	}
	if (with_block_cancel) {
		/* The real shape notify_client.c:1007 uses -- a Block, not a
		 * plain function pointer, so this leg also exercises
		 * _Block_copy()/libsystem_blocks on a dispatch object. */
		dispatch_source_set_cancel_handler(src, ^{
			g_cancel_ran = 1;
		});
		diag("  set_cancel_handler(block) ok\n");
	} else if (with_handler) {
		dispatch_source_set_cancel_handler_f(src, cancel_marker);
		diag("  set_cancel_handler_f ok\n");
	}
	return src;
}

static int mode_recv(int with_handler, int with_block_cancel, int do_resume,
    int do_deliver)
{
	mach_port_name_t port = 0;
	void *src = recv_source_common(with_handler, with_block_cancel, &port);
	int spins;

	if (src == 0) return g_fail ? g_fail : 1;

	if (!do_resume) {
		diag("MACH_RECV source created (not resumed): OK\n");
		return g_fail;
	}

	diag("  resuming...\n");
	dispatch_resume(src);
	usleep(300000);
	diag("  resumed and still alive\n");

	if (do_deliver) {
		mach_msg_header_t hdr;
		mach_msg_return_t mr;

		memset(&hdr, 0, sizeof(hdr));
		hdr.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND_REAL, 0);
		hdr.msgh_size = sizeof(hdr);
		hdr.msgh_remote_port = port;
		hdr.msgh_local_port = MACH_PORT_NULL;
		hdr.msgh_id = 0x44415232; /* "DAR2" */

		diag("  sending one real Mach message to the source's port...\n");
		mr = mach_msg(&hdr, MACH_SEND_MSG_REAL, sizeof(hdr), 0,
		    MACH_PORT_NULL, 0, MACH_PORT_NULL);
		diag_hex("  mach_msg(SEND) mr = ", (unsigned long)mr);
		if (mr != 0) {
			diag("  ^ FAIL: the send itself failed\n");
			g_fail++;
		}
		for (spins = 0; spins < 200 && !g_event_ran; spins++) {
			usleep(10000);
		}
		diag_num("  event handler ran: ", g_event_ran);
		if (!g_event_ran) {
			diag("  ^ FAIL: EVFILT_MACHPORT never delivered to the"
			     " dispatch source's handler\n");
			g_fail++;
		}
	}

	dispatch_source_cancel(src);
	usleep(200000);
	diag_num("  cancel handler ran: ", g_cancel_ran);
	dispatch_release(src);
	diag("MACH_RECV source: done\n");
	return g_fail;
}

/* The real call configd/IPMonitor makes, with nothing else in the
 * process. Deliberately preceded by the same dispatch_async_f() as
 * `mt`, because notify(3) only reaches the DAR-266 code path once
 * _dispatch_is_multithreaded() is true. */
static int mode_notify(void)
{
	void *q = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0);
	const char *name = "com.iokit.dar266.registerdispatch";
	int token = -1;
	int status;
	int spins;

	dispatch_async_f(q, 0, async_marker);
	for (spins = 0; spins < 200 && !g_async_ran; spins++) {
		usleep(10000);
	}
	diag_num("_dispatch_is_multithreaded(): ",
	    _dispatch_is_multithreaded() ? 1 : 0);

	diag("calling notify_register_dispatch()...\n");
	status = notify_register_dispatch(name, &token, q, ^(int t) {
		(void)t;
		g_event_ran = 1;
	});
	diag_num("notify_register_dispatch status = ", status);
	diag_num("  token = ", token);
	if (status != 0) {
		diag("  ^ FAIL: registration failed\n");
		g_fail++;
		return g_fail;
	}

	diag("posting the name...\n");
	status = notify_post(name);
	diag_num("notify_post status = ", status);
	for (spins = 0; spins < 300 && !g_event_ran; spins++) {
		usleep(10000);
	}
	diag_num("dispatch handler ran: ", g_event_ran);
	if (!g_event_ran) {
		diag("  ^ FAIL: the posted notification never reached the"
		     " dispatch handler\n");
		g_fail++;
	}
	notify_cancel(token);
	return g_fail;
}

int main(int argc, char **argv)
{
	const char *mode = (argc > 1) ? argv[1] : "all";

	diag("dispatch_machport_smoketest: mode ");
	diag(mode);
	diag("\n");

	if (!strcmp(mode, "mt")) {
		mode_multithreaded();
	} else if (!strcmp(mode, "proc")) {
		mode_multithreaded();
		mode_proc_source();
	} else if (!strcmp(mode, "recv-create")) {
		mode_multithreaded();
		mode_recv(0, 0, 0, 0);
	} else if (!strcmp(mode, "recv-resume")) {
		mode_multithreaded();
		mode_recv(1, 0, 1, 0);
	} else if (!strcmp(mode, "recv-block")) {
		mode_multithreaded();
		mode_recv(1, 1, 1, 0);
	} else if (!strcmp(mode, "recv-deliver")) {
		mode_multithreaded();
		mode_recv(1, 1, 1, 1);
	} else if (!strcmp(mode, "notify")) {
		mode_notify();
	} else if (!strcmp(mode, "all")) {
		mode_multithreaded();
		mode_proc_source();
		mode_recv(1, 1, 1, 1);
	} else {
		diag("unknown mode; see this file's header comment\n");
		return 2;
	}

	if (g_fail == 0) {
		diag("dispatch_machport_smoketest: PASS\n");
		return 0;
	}
	diag_num("dispatch_machport_smoketest: FAIL, failures = ", g_fail);
	return 1;
}
