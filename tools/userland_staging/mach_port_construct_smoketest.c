/* Real standalone mach_port_construct() / abort() smoketest (DAR-261).
 *
 * DAR-261 reports that notifyd is resident but every client round trip
 * hangs forever, with exactly one line in /var/log/notifyd.log:
 *
 *   Assertion failed: (kstatus == KERN_SUCCESS), function
 *   __notify_generate_common_port, file .../notify/notify_proc.c.
 *
 * The ticket reads that as "mach_port_construct() fails". That is one
 * of TWO possibilities, not a settled fact: real
 * __notify_generate_common_port (third_party/PureDarwin/src/Libraries/
 * XPC/notify/notify_proc.c:1609 and :1613, real unmodified source) has
 * two back-to-back asserts with a byte-identical expression, in the
 * same function --
 *
 *   kstatus = mach_port_construct(mach_task_self(), &opts, 0, &port);
 *   assert(kstatus == KERN_SUCCESS);
 *   kstatus = mach_port_insert_right(mach_task_self(), port, port,
 *                                    MACH_MSG_TYPE_MAKE_SEND);
 *   assert(kstatus == KERN_SUCCESS);
 *
 * -- and __assert_rtn's message (tools/userland_staging/
 * libmalloc_dispatch.c) prints func/file/expr but NOT the line number,
 * so the log line cannot distinguish them. This test runs both calls
 * separately, so whichever one actually fails is named outright.
 *
 * It also decomposes the call rather than just repeating it, because
 * every layer between notifyd and the kernel is a real, separate
 * candidate on this port:
 *
 *   mach_port_construct()                    cf_mach_port_syscalls.c,
 *                                            in libsystem_kernel.dylib
 *     -> _kernelrpc_mach_port_construct_trap  mach_port_traps.s, trap -24
 *     -> (MACH_SEND_INVALID_DEST only)
 *        _kernelrpc_mach_port_construct       real mig-generated
 *                                             mach_portUser.o, needs a
 *                                             working mach_msg() RPC
 *
 * and kernel-side (osfmk/ipc/mach_kernelrpc.c:391 ->
 * osfmk/ipc/mach_port.c:2568, both real unmodified xnu-7195):
 *
 *   port_name_to_task(target)   -- fails => MACH_SEND_INVALID_DEST
 *   copyin(options)             -- fails => MACH_SEND_INVALID_DATA
 *   ipc_port_alloc()
 *   MPO_QLIMIT => mach_port_set_attributes(MACH_PORT_LIMITS_INFO)
 *
 * MPO_QLIMIT is the only non-default option notifyd passes, and it is
 * the one leg that makes the kernel do a second, separate operation
 * after the port already exists. So TEST 3 (flags = 0) vs TEST 4
 * (notifyd's exact MPO_QLIMIT/qlimit=16 options) isolates it in one
 * step, TEST 5 calls the raw trap directly to separate a trap failure
 * from a MIG-fallback failure, and TEST 7 does mach_port_allocate() +
 * mach_port_set_attributes(MACH_PORT_LIMITS_INFO) by hand -- exactly
 * what the kernel's own MPO_QLIMIT leg does internally, but reached
 * over the MIG path from userland instead, so a failure there localizes
 * the fault to port limits rather than to construct.
 *
 * ---- Second, separate question this test answers (also DAR-261) ----
 *
 * The assertion did not kill notifyd: PID 4 stayed up, logged the
 * assertion exactly once, and burned only 0.57s of CPU across ~61s wall
 * -- so it is neither dead nor spinning in abort()'s `for(;;)`
 * fail-safe. If abort() does not really terminate on this port, then
 * every failed assertion anywhere in this userland is a silent hang
 * instead of a visible crash, which is a far broader diagnostic hazard
 * than notifyd.
 *
 * That is checked here by argv rather than by fork(): none of the
 * dylibs this binary links export fork()/waitpid() (checked with `nm
 * -gU`, not assumed -- _fork/_wait4 currently live only in
 * libSystemConfiguration.dylib), and a self-terminating child is
 * observable anyway, because the shell reports the signal and the
 * kernel's own user_fault_debug line lands in the boot log. Run:
 *
 *   mach_port_construct_smoketest                 # the mach tests
 *   mach_port_construct_smoketest abort-main      # abort() on main thread
 *   mach_port_construct_smoketest abort-assert    # __assert_rtn(), notifyd's path
 *   mach_port_construct_smoketest abort-thread    # abort() on a pthread
 *   mach_port_construct_smoketest raw-kill        # kill(getpid(), SIGABRT) direct
 *
 * abort-thread matters on its own: notifyd's assertion fires on a MIG
 * server thread, not on the main thread, and libmalloc_dispatch.c's
 * abort() is kill(getpid(), SIGABRT) -- a process-directed signal
 * raised from a non-main thread, which is a genuinely different kernel
 * path from the main-thread case.
 *
 * Output is raw write(2), never printf() -- see kqueue_smoketest.c's own
 * header comment for the real, separate, still-undiagnosed printf()
 * crash in this minimal link recipe.
 */
#include <mach/mach_types.h>
#include <mach/kern_return.h>
#include <mach/port.h>
#include <mach/message.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

/* Real declarations rather than <mach/mach.h>: this binary is built
 * -ffreestanding against the same minimal recipe as every other
 * smoketest here, and mach.h drags in the full task/thread/vm surface
 * this test has no use for. Signatures copied from
 * third_party/xnu-7195/libsyscall/mach/mach_port.c and
 * osfmk/mach/mach_port.defs. */
extern mach_port_t mach_task_self_;          /* libsystem_kernel.dylib */
extern mach_port_t task_self_trap(void);     /* real trap -28 */

extern kern_return_t mach_port_allocate(ipc_space_t, mach_port_right_t,
    mach_port_name_t *);
extern kern_return_t mach_port_construct(ipc_space_t, mach_port_options_t *,
    mach_port_context_t, mach_port_name_t *);
extern kern_return_t mach_port_destruct(ipc_space_t, mach_port_name_t,
    mach_port_delta_t, mach_port_context_t);
extern kern_return_t mach_port_insert_right(ipc_space_t, mach_port_name_t,
    mach_port_t, mach_msg_type_name_t);
extern kern_return_t mach_port_get_attributes(ipc_space_t, mach_port_name_t,
    mach_port_flavor_t, mach_port_info_t, mach_msg_type_number_t *);
extern kern_return_t mach_port_set_attributes(ipc_space_t, mach_port_name_t,
    mach_port_flavor_t, mach_port_info_t, mach_msg_type_number_t);
extern kern_return_t mach_port_deallocate(ipc_space_t, mach_port_name_t);

/* The two halves of mach_port_construct()'s real trap-first/MIG-fallback
 * body, called directly by TEST 5 so a trap failure and a MIG-fallback
 * failure are told apart instead of collapsed into one return value. */
extern kern_return_t _kernelrpc_mach_port_construct_trap(ipc_space_t,
    mach_port_options_t *, unsigned long long, mach_port_name_t *);
extern kern_return_t _kernelrpc_mach_port_construct(ipc_space_t,
    mach_port_options_t *, unsigned long long, mach_port_name_t *);

extern void abort(void);                     /* libsystem_malloc.dylib */
extern void __assert_rtn(const char *, const char *, int, const char *);
extern int getpid(void);

#define SIGABRT_REAL 6   /* bsd/sys/signal.h */

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

/* Real kern_return_t / MACH_SEND_* values, from
 * osfmk/mach/kern_return.h and osfmk/mach/message.h. Decoded locally
 * rather than via mach_error_string(): that function's real body walks
 * the mach_error subsystem tables and is not worth pulling into a test
 * whose whole job is to be the simplest possible reproducer. */
static const char *kr_name(kern_return_t kr)
{
	switch (kr) {
	case 0:  return "KERN_SUCCESS";
	case 1:  return "KERN_INVALID_ADDRESS";
	case 2:  return "KERN_PROTECTION_FAILURE";
	case 3:  return "KERN_NO_SPACE";
	case 4:  return "KERN_INVALID_ARGUMENT";
	case 5:  return "KERN_FAILURE";
	case 6:  return "KERN_RESOURCE_SHORTAGE";
	case 8:  return "KERN_NO_ACCESS";
	case 15: return "KERN_INVALID_TASK";
	case 16: return "KERN_INVALID_NAME";
	case 17: return "KERN_INVALID_RIGHT";
	case 18: return "KERN_INVALID_VALUE";
	case 19: return "KERN_UREFS_OVERFLOW";
	case 20: return "KERN_INVALID_CAPABILITY";
	case 21: return "KERN_RIGHT_EXISTS";
	case 46: return "KERN_NOT_SUPPORTED";
	case 49: return "KERN_DENIED";
	case 0x10000003: return "MACH_SEND_INVALID_DEST";
	case 0x10000002: return "MACH_SEND_INVALID_DATA";
	case 0x10000004: return "MACH_SEND_TIMED_OUT";
	case 0x10004002: return "MACH_RCV_INVALID_NAME";
	default: return "(unrecognized)";
	}
}

static void report(const char *what, kern_return_t kr)
{
	diag(what);
	diag_hex("    kr   = ", (unsigned long)kr);
	diag("    name = ");
	diag(kr_name(kr));
	diag("\n");
	if (kr != KERN_SUCCESS) g_fail++;
}

/* ------------------------- the mach tests ------------------------- */

static void test_task_self(void)
{
	mach_port_t cached = mach_task_self_;
	mach_port_t fresh = task_self_trap();

	diag("\n=== TEST 1: task self port ===\n");
	diag_hex("[T1] mach_task_self_ (global, set by mach_init) = ", (unsigned long)cached);
	diag_hex("[T1] task_self_trap() (fresh trap -28)          = ", (unsigned long)fresh);
	if (cached == 0) {
		diag("[T1] FAIL: mach_task_self_ is MACH_PORT_NULL -- every mach_port_*\n"
		     "     call in this process targets a null task and the kernel's\n"
		     "     port_name_to_task() returns TASK_NULL (=> MACH_SEND_INVALID_DEST)\n");
		g_fail++;
		return;
	}
	if (cached != fresh) {
		diag("[T1] FAIL: cached mach_task_self_ does not match a fresh trap\n");
		g_fail++;
		return;
	}
	diag("[T1] PASS\n");
}

/* mach_port_mod_refs is a real, defined export of this stack's
 * libsystem_kernel.dylib -- used to drop the receive rights the tests
 * below allocate, so a run leaves the process's port space as it found
 * it. */
extern kern_return_t mach_port_mod_refs(ipc_space_t, mach_port_name_t,
    mach_port_right_t, mach_port_delta_t);

static void test_allocate(void)
{
	mach_port_name_t name = 0;
	kern_return_t kr;

	diag("\n=== TEST 2: mach_port_allocate(RECEIVE) baseline ===\n");
	kr = mach_port_allocate(mach_task_self_, MACH_PORT_RIGHT_RECEIVE, &name);
	report("[T2] mach_port_allocate\n", kr);
	if (kr == KERN_SUCCESS) {
		diag_hex("[T2] port name = ", (unsigned long)name);
		(void)mach_port_mod_refs(mach_task_self_, name,
		    MACH_PORT_RIGHT_RECEIVE, -1);
	}
}

static mach_port_name_t g_qlimit_port;
static int g_qlimit_port_has_send;   /* did TEST 6's insert_right succeed? */

static void test_construct_plain(void)
{
	mach_port_options_t opts;
	mach_port_name_t name = 0;
	kern_return_t kr;

	diag("\n=== TEST 3: mach_port_construct(flags = 0) ===\n");
	memset(&opts, 0, sizeof(opts));
	kr = mach_port_construct(mach_task_self_, &opts, 0, &name);
	report("[T3] mach_port_construct, no options\n", kr);
	if (kr == KERN_SUCCESS) {
		diag_hex("[T3] port name = ", (unsigned long)name);
		(void)mach_port_destruct(mach_task_self_, name, 0, 0);
	}
}

static void test_construct_qlimit(void)
{
	mach_port_options_t opts;
	mach_port_name_t name = 0;
	kern_return_t kr;

	diag("\n=== TEST 4: mach_port_construct(MPO_QLIMIT, qlimit = 16) "
	     "-- notifyd's exact call ===\n");
	memset(&opts, 0, sizeof(opts));
	opts.flags = MPO_QLIMIT;
	opts.mpl.mpl_qlimit = 16;
	kr = mach_port_construct(mach_task_self_, &opts, 0, &name);
	report("[T4] mach_port_construct, MPO_QLIMIT\n", kr);
	if (kr == KERN_SUCCESS) {
		diag_hex("[T4] port name = ", (unsigned long)name);
		g_qlimit_port = name;
	} else if (kr == KERN_NOT_SUPPORTED) {
		diag("[T4] NOTE: KERN_NOT_SUPPORTED is what the trap-first wrapper\n"
		     "     returns when the trap answered MACH_SEND_INVALID_DEST and\n"
		     "     the MIG fallback is still cf_mach_port_mig_fallback_stub.c.\n"
		     "     See TEST 5 for which half actually failed.\n");
	}
}

static void test_construct_raw_trap(void)
{
	mach_port_options_t opts;
	mach_port_name_t name = 0;
	kern_return_t kr;

	diag("\n=== TEST 5: raw trap vs MIG fallback, MPO_QLIMIT ===\n");

	memset(&opts, 0, sizeof(opts));
	opts.flags = MPO_QLIMIT;
	opts.mpl.mpl_qlimit = 16;
	kr = _kernelrpc_mach_port_construct_trap(mach_task_self_, &opts, 0, &name);
	report("[T5] _kernelrpc_mach_port_construct_trap (trap -24)\n", kr);
	if (kr == KERN_SUCCESS) {
		diag_hex("[T5] port name = ", (unsigned long)name);
		(void)mach_port_destruct(mach_task_self_, name, 0, 0);
	} else {
		/* Only reached if the trap itself failed -- the fallback is what
		 * mach_port_construct() would have tried next. Its own return
		 * value separates "the MIG client is an honest KERN_NOT_SUPPORTED
		 * stub" from "the RPC ran and the kernel refused". */
		name = 0;
		kr = _kernelrpc_mach_port_construct(mach_task_self_, &opts, 0, &name);
		report("[T5] _kernelrpc_mach_port_construct (MIG fallback)\n", kr);
		if (kr == KERN_SUCCESS) {
			diag_hex("[T5] port name = ", (unsigned long)name);
			(void)mach_port_destruct(mach_task_self_, name, 0, 0);
		}
	}
}

static void test_insert_right(void)
{
	kern_return_t kr;

	diag("\n=== TEST 6: mach_port_insert_right(MAKE_SEND) "
	     "-- notifyd's second call ===\n");
	if (g_qlimit_port == 0) {
		diag("[T6] SKIP: TEST 4 produced no port\n");
		return;
	}
	kr = mach_port_insert_right(mach_task_self_, g_qlimit_port, g_qlimit_port,
	    MACH_MSG_TYPE_MAKE_SEND);
	report("[T6] mach_port_insert_right\n", kr);
	/* Recorded so the cleanup at the end passes the matching srdelta.
	 * This is exactly the case the test exists to catch, so it must not
	 * assume the send right was created. */
	if (kr == KERN_SUCCESS) g_qlimit_port_has_send = 1;
}

static void test_port_limits(void)
{
	mach_port_limits_t limits;
	mach_msg_type_number_t cnt;
	kern_return_t kr;
	mach_port_name_t name = 0;

	diag("\n=== TEST 7: MACH_PORT_LIMITS_INFO over MIG "
	     "(what MPO_QLIMIT does kernel-side) ===\n");

	/* Read back the limit the kernel should have set on TEST 4's port. */
	if (g_qlimit_port != 0) {
		cnt = MACH_PORT_LIMITS_INFO_COUNT;
		memset(&limits, 0, sizeof(limits));
		kr = mach_port_get_attributes(mach_task_self_, g_qlimit_port,
		    MACH_PORT_LIMITS_INFO, (mach_port_info_t)&limits, &cnt);
		report("[T7] mach_port_get_attributes(LIMITS_INFO) on TEST 4's port\n", kr);
		if (kr == KERN_SUCCESS) {
			diag_num("[T7] mpl_qlimit read back = ", (long)limits.mpl_qlimit);
			if (limits.mpl_qlimit != 16) {
				diag("[T7] FAIL: MPO_QLIMIT did not take effect (expected 16)\n");
				g_fail++;
			}
		}
	}

	/* Then the same thing by hand on a fresh, plainly-allocated port:
	 * allocate + set_attributes is precisely the pair the kernel's own
	 * mach_port_construct() runs internally for MPO_QLIMIT, so this
	 * separates "port limits are broken" from "construct is broken". */
	kr = mach_port_allocate(mach_task_self_, MACH_PORT_RIGHT_RECEIVE, &name);
	report("[T7] mach_port_allocate for the manual limits test\n", kr);
	if (kr != KERN_SUCCESS) return;

	memset(&limits, 0, sizeof(limits));
	limits.mpl_qlimit = 16;
	kr = mach_port_set_attributes(mach_task_self_, name, MACH_PORT_LIMITS_INFO,
	    (mach_port_info_t)&limits, MACH_PORT_LIMITS_INFO_COUNT);
	report("[T7] mach_port_set_attributes(LIMITS_INFO, 16) by hand\n", kr);

	(void)mach_port_mod_refs(mach_task_self_, name, MACH_PORT_RIGHT_RECEIVE, -1);
}

/* --------------------- the abort() sub-modes --------------------- */

/* Real BSD kill(2) via the same direct-trap shape every other
 * hand-written syscall stub in this tree uses (syscall_trap.c,
 * setrlimit_trap.c): arm64 BSD syscalls put the number in x16 and
 * signal failure via the carry flag, with errno in x0 on failure and
 * the return value in x0 on success. Named raw_kill, not kill, so it
 * cannot collide with the real kill() some sibling dylib exports.
 *
 * The carry flag is genuinely read here rather than assumed clear
 * (DAR-261 review): this whole file exists to stop one failure being
 * misreported as another, and returning x0 unconditionally would print
 * a failing kill()'s errno as if it were a return value, then blame the
 * kernel's signal delivery 10 seconds later for a call that never
 * happened. `*err_out` is set to the errno on failure, 0 on success. */
static long raw_kill(int pid, int sig, int *err_out)
{
	register long x0 asm("x0") = pid;
	register long x1 asm("x1") = sig;
	register long x16 asm("x16") = 37; /* SYS_kill, bsd/kern/syscalls.master */
	long ret;
	long failed;

	asm volatile("svc #0x80\n\t"
		     "cset %1, cs\n\t"       /* carry set == syscall failed */
		     "mov %0, x0"
	    : "=r"(ret), "=r"(failed), "+r"(x0), "+r"(x1)
	    : "r"(x16)
	    : "memory", "cc");

	if (failed) {
		*err_out = (int)ret;
		return -1;
	}
	*err_out = 0;
	return ret;
}

static void *abort_thread_body(void *arg)
{
	(void)arg;
	diag("[abort-thread] secondary thread calling abort() now\n");
	abort();
	diag("[abort-thread] FAIL: abort() RETURNED on a secondary thread\n");
	return NULL;
}

static int run_abort_mode(const char *mode)
{
	int spins;

	if (!strcmp(mode, "abort-main")) {
		diag("[abort-main] calling abort() on the main thread\n");
		abort();
		diag("[abort-main] FAIL: abort() RETURNED -- the process is still "
		     "running after SIGABRT\n");
		return 1;
	}

	if (!strcmp(mode, "abort-assert")) {
		diag("[abort-assert] calling __assert_rtn() -- notifyd's exact path\n");
		__assert_rtn("run_abort_mode", "mach_port_construct_smoketest.c",
		    __LINE__, "kstatus == KERN_SUCCESS");
		diag("[abort-assert] FAIL: __assert_rtn() RETURNED\n");
		return 1;
	}

	if (!strcmp(mode, "abort-thread")) {
		pthread_t t;
		int rc = pthread_create(&t, NULL, abort_thread_body, NULL);
		if (rc != 0) {
			diag_num("[abort-thread] FAIL: pthread_create rc=", rc);
			return 1;
		}
		pthread_detach(t);
		/* Bounded, so a non-terminating abort() is reported as such
		 * instead of hanging the boot test the way notifyd did. */
		for (spins = 0; spins < 20; spins++) sleep(1);
		diag("[abort-thread] FAIL: still alive 20s after a secondary thread "
		     "called abort() -- a process-directed SIGABRT raised from a "
		     "non-main thread did not terminate this process\n");
		return 1;
	}

	if (!strcmp(mode, "raw-kill")) {
		int err = 0;
		long rc;

		diag("[raw-kill] kill(getpid(), SIGABRT) direct, no abort() wrapper\n");
		diag_num("[raw-kill] getpid() = ", (long)getpid());
		rc = raw_kill(getpid(), SIGABRT_REAL, &err);
		if (rc < 0) {
			/* The syscall itself was refused -- say so plainly
			 * rather than letting the 10s timeout below blame
			 * signal delivery for a signal never posted. */
			diag_num("[raw-kill] FAIL: kill() failed, errno = ", (long)err);
			return 1;
		}
		diag_num("[raw-kill] kill() returned ", rc);
		for (spins = 0; spins < 10; spins++) sleep(1);
		diag("[raw-kill] FAIL: still alive 10s after a SUCCESSFUL "
		     "kill(self, SIGABRT) -- the kernel accepted the signal and "
		     "did not terminate this process\n");
		return 1;
	}

	diag("unknown mode -- valid: abort-main abort-assert abort-thread raw-kill\n");
	return 2;
}

int main(int argc, char **argv)
{
	int fd = open("/dev/console", O_WRONLY);
	if (fd >= 0) g_out = fd;

	if (argc > 1) {
		diag("mach_port_construct_smoketest: mode ");
		diag(argv[1]);
		diag("\n");
		return run_abort_mode(argv[1]);
	}

	diag("mach_port_construct_smoketest: start (DAR-261)\n");

	test_task_self();
	test_allocate();
	test_construct_plain();
	test_construct_qlimit();
	test_construct_raw_trap();
	test_insert_right();
	test_port_limits();

	if (g_qlimit_port != 0) {
		/* srdelta must match what TEST 6 actually did: -1 only if the
		 * send right was really inserted. Passing -1 unconditionally
		 * fails with KERN_INVALID_VALUE when TEST 6 failed, leaking the
		 * receive right on precisely the run this test is about. */
		(void)mach_port_destruct(mach_task_self_, g_qlimit_port,
		    g_qlimit_port_has_send ? -1 : 0, 0);
	}

	if (g_fail == 0) {
		diag("\nmach_port_construct_smoketest: ALL PASS\n");
		return 0;
	}
	diag_num("\nmach_port_construct_smoketest: FAILURES = ", g_fail);
	return 1;
}
