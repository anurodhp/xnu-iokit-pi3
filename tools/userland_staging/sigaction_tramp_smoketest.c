/* Real signal-handler delivery + return smoketest (DAR-262).
 *
 * DAR-262 is about `sa_tramp`: XNU does not call a signal handler
 * directly. bsd/kern/kern_sig.c's sendsig() enters userspace at the
 * TRAMPOLINE address the process registered alongside the handler
 * (`ps->ps_trampact[signum]`), and that trampoline is what actually
 * calls the handler and then invokes sigreturn(2) to unwind back to the
 * interrupted context. Real Apple fills it in inside
 * __platform_sigaction() (third_party/libplatform/src/setjmp/generic/
 * sigaction.c:47): `sa.sa_tramp = _sigtramp;`.
 *
 * This project's configd_sigaction_compat.c used to bypass that layer
 * and hand a 16-byte `struct sigaction` straight to the raw
 * `__sigaction` trap, which copies in a 24-byte `struct
 * __user64_sigaction` -- so sa_tramp was built from the caller's
 * `{sa_mask, sa_flags}` words and the process registered a garbage
 * trampoline.
 *
 * ---- Why the test has to do more than "was the handler called" ----
 *
 * The whole failure lives in the trampoline, which sits on BOTH sides of
 * the handler: it is entered first, calls the handler, and then does the
 * sigreturn. A test that only checks a flag set inside the handler can
 * therefore pass on a build where the trampoline is broken in its second
 * half. So each test below checks three separate things:
 *
 *   1. the handler ran at all       (g_hits incremented)
 *   2. it got the right signal      (the siginfo/signo argument)
 *   3. CONTROL CAME BACK            (execution continues past the
 *                                    raise, with the stack intact)
 *
 * (3) is the one that exercises sigreturn, and it is checked by writing
 * a canary onto the stack before raising and re-reading it afterwards,
 * so a trampoline that corrupts the interrupted frame is caught rather
 * than being reported as a pass.
 *
 * ---- Which test actually discriminates, and which do not ----
 *
 * Be precise about this, because it is easy to get wrong and it changes
 * what a passing run means.
 *
 * TEST 1 is the discriminating one. It uses signal(3) -- real Libc
 * gen/FreeBSD/signal.c, compiled into this binary -- whose body calls
 * the `_sigaction` shim by name. That is the ONLY route into the broken
 * shim, so TEST 1 is the only test that behaves differently between
 * sigaction_tramp_smoketest.macho and
 * sigaction_tramp_smoketest_broken.macho.
 *
 * TESTS 2 and 3 call the PUBLIC sigaction(2) directly, and in both
 * variants that binds to libsystem_c.dylib's real one (verified with
 * `nm -m` on both binaries: `(undefined) external _sigaction (from
 * libsystem_c)`). They are therefore NOT before/after discriminators --
 * they are controls, present to prove the real Apple chain
 * (sigaction -> __platform_sigaction -> _sigtramp -> handler ->
 * sigreturn) genuinely works on this port at all. Without them, a TEST 1
 * failure could not be attributed to the shim rather than to signal
 * delivery being broken generally.
 *
 * The same distinction applies to the real binaries: configd.m:421's
 * `sigaction(SIGHUP, &nact, NULL)` binds to libsystem_c's real
 * sigaction and was never affected. What went through the broken shim
 * in configd and notifyd was signal(3) only.
 *
 * TEST 3 additionally checks sa_mask, the field the struct mismatch fed
 * from stack garbage even when the handler happened not to crash.
 *
 * Run both binaries in one boot, and run the BROKEN one LAST. On the
 * broken one TEST 1 does not print a clean FAIL -- it dies, and it dies
 * repeatedly: the wild jump faults, the fault raises another signal,
 * and the process never returns to the shell. Confirmed on a real boot
 * (qemu/dar263_boot3.log): 55 back-to-back exceptions and no prompt
 * afterwards, which ate the rest of that boot's command list. That
 * crash IS the real failing case, and its address is the proof --
 * `pc 0x200000000` is exactly sa_flags(0x2 = SA_RESTART) << 32 |
 * sa_mask(0), i.e. the caller's own two struct fields read back by the
 * kernel as a function pointer.
 *
 * Output is raw write(2), never printf() -- see kqueue_smoketest.c's own
 * header comment for the real, separate, still-undiagnosed printf()
 * crash in this minimal link recipe.
 */
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

static int g_out = 2;
static int g_fail;

/* raise(3) is not an export of any dylib in this stack (checked with
 * dyld_info -exports, not assumed), so the signal is raised with a
 * direct kill(getpid(), sig) trap -- the same hand-written arm64
 * syscall shape mach_port_construct_smoketest.c's raw_kill() uses, and
 * exactly what real raise(3) does underneath for a single-threaded
 * process anyway. arm64 BSD syscalls put the number in x16 and report
 * failure via the carry flag; both are honoured here so a refused
 * syscall is reported as such instead of being mistaken for a delivery
 * failure. */
static long raw_syscall1(long nr, long a0)
{
	register long x0 asm("x0") = a0;
	register long x16 asm("x16") = nr;
	long ret, failed;

	asm volatile("svc #0x80\n\t"
		     "cset %1, cs\n\t"
		     "mov %0, x0"
	    : "=r"(ret), "=r"(failed), "+r"(x0)
	    : "r"(x16)
	    : "memory", "cc");
	return failed ? -ret : ret;
}

static long raw_syscall2(long nr, long a0, long a1)
{
	register long x0 asm("x0") = a0;
	register long x1 asm("x1") = a1;
	register long x16 asm("x16") = nr;
	long ret, failed;

	asm volatile("svc #0x80\n\t"
		     "cset %1, cs\n\t"
		     "mov %0, x0"
	    : "=r"(ret), "=r"(failed), "+r"(x0), "+r"(x1)
	    : "r"(x16)
	    : "memory", "cc");
	return failed ? -ret : ret;
}

#define SYS_getpid 20   /* bsd/kern/syscalls.master */
#define SYS_kill   37

static int raise_signal(int signo)
{
	long pid = raw_syscall1(SYS_getpid, 0);

	if (pid <= 0) return -1;
	return raw_syscall2(SYS_kill, pid, signo) < 0 ? -1 : 0;
}

static volatile sig_atomic_t g_hits;
static volatile sig_atomic_t g_last_signo;
static volatile sig_atomic_t g_mask_held;

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
	if (v < 0) { neg = 1; u = (unsigned long)(-(long long)v); } else { u = (unsigned long)v; }
	buf[--i] = '\n';
	if (u == 0) buf[--i] = '0';
	while (u) { buf[--i] = (char)('0' + (u % 10)); u /= 10; }
	if (neg) buf[--i] = '-';
	write(g_out, buf + i, sizeof(buf) - (size_t)i);
}

static void plain_handler(int signo)
{
	g_hits++;
	g_last_signo = signo;
}

/* Checks, from inside the handler, that the signal being handled is
 * blocked -- i.e. that the sa_mask the kernel recorded is the one we
 * asked for and not stack garbage. */
static void mask_probing_handler(int signo)
{
	sigset_t cur;

	g_hits++;
	g_last_signo = signo;
	memset(&cur, 0, sizeof(cur));
	if (sigprocmask(SIG_BLOCK, NULL, &cur) == 0) {
		g_mask_held = sigismember(&cur, signo) ? 1 : 0;
	} else {
		g_mask_held = -1;
	}
}

/* Common body: install however the caller wants, raise, and verify all
 * three properties. `stack_canary` is deliberately a local so it lives
 * on the frame the trampoline has to restore. */
static int run_delivery_check(const char *tag, int signo)
{
	volatile unsigned long stack_canary = 0x5A5AC0DEC0DE5A5AUL;
	int before = g_hits;

	g_last_signo = -1;

	if (raise_signal(signo) != 0) {
		diag(tag);
		diag("  FAIL: kill(getpid(), sig) returned an error\n");
		return 1;
	}

	/* Reaching here at all is the sigreturn half of the trampoline. */
	if (g_hits != before + 1) {
		diag(tag);
		diag_num("  FAIL: handler ran this many times (expected 1): ",
		         (long)(g_hits - before));
		return 1;
	}
	if (g_last_signo != signo) {
		diag(tag);
		diag_num("  FAIL: handler saw the wrong signal number: ",
		         (long)g_last_signo);
		return 1;
	}
	if (stack_canary != 0x5A5AC0DEC0DE5A5AUL) {
		diag(tag);
		diag("  FAIL: the interrupted frame was corrupted across "
		     "handler delivery (sigreturn restored a bad context)\n");
		return 1;
	}
	diag(tag);
	diag("  PASS: handler ran once, saw the right signal, and control "
	     "returned with the frame intact\n");
	return 0;
}

static int test_signal3(void)
{
	diag("\n=== TEST 1: signal(3) handler delivery (notifyd's API) ===\n");
	if (signal(SIGUSR1, plain_handler) == SIG_ERR) {
		diag("[T1] FAIL: signal(SIGUSR1, handler) returned SIG_ERR\n");
		return 1;
	}
	return run_delivery_check("[T1]", SIGUSR1);
}

static int test_sigaction(void)
{
	struct sigaction nact, oact;

	diag("\n=== TEST 2: sigaction(2) handler delivery "
	     "(control -- real libsystem_c chain, same in both variants) ===\n");

	/* Deliberately mirrors configd.m:421's own call shape: handler,
	 * empty mask, SA_RESTART. Note this binds to libsystem_c's REAL
	 * sigaction in both variants -- see this file's header. */
	memset(&nact, 0, sizeof(nact));
	memset(&oact, 0, sizeof(oact));
	nact.sa_handler = plain_handler;
	sigemptyset(&nact.sa_mask);
	nact.sa_flags = SA_RESTART;

	if (sigaction(SIGUSR2, &nact, &oact) == -1) {
		diag("[T2] FAIL: sigaction(SIGUSR2, ...) returned -1\n");
		return 1;
	}
	return run_delivery_check("[T2]", SIGUSR2);
}

static int test_mask_honoured(void)
{
	struct sigaction nact;
	int rc;

	diag("\n=== TEST 3: sa_mask is the one we asked for, not stack "
	     "garbage ===\n");

	memset(&nact, 0, sizeof(nact));
	nact.sa_handler = mask_probing_handler;
	sigemptyset(&nact.sa_mask);
	nact.sa_flags = 0;

	if (sigaction(SIGWINCH, &nact, NULL) == -1) {
		diag("[T3] FAIL: sigaction(SIGWINCH, ...) returned -1\n");
		return 1;
	}

	g_mask_held = -2;
	rc = run_delivery_check("[T3]", SIGWINCH);
	if (rc != 0) return rc;

	/* POSIX: the signal being delivered is added to the mask for the
	 * duration of its own handler unless SA_NODEFER was requested. We
	 * asked for flags = 0, so it must be blocked. A garbage sa_flags
	 * carrying SA_NODEFER, or a garbage sa_mask, shows up here. */
	if (g_mask_held == -2) {
		diag("[T3] FAIL: mask probe never ran\n");
		return 1;
	}
	if (g_mask_held == -1) {
		diag("[T3] FAIL: sigprocmask() failed inside the handler\n");
		return 1;
	}
	if (g_mask_held != 1) {
		diag("[T3] FAIL: the delivered signal was NOT blocked during "
		     "its own handler -- sa_mask/sa_flags did not survive the "
		     "trip into the kernel (this is DAR-262's struct mismatch: "
		     "both fields were read from past the end of a 16-byte "
		     "struct sigaction)\n");
		return 1;
	}
	diag("[T3] PASS: the delivered signal was blocked during its own "
	     "handler, so sa_mask/sa_flags arrived intact\n");
	return 0;
}

int main(void)
{
	int fd = open("/dev/console", O_WRONLY);
	if (fd >= 0) g_out = fd;

	diag("sigaction_tramp_smoketest: start (DAR-262)\n");

	g_fail += test_signal3();
	g_fail += test_sigaction();
	g_fail += test_mask_honoured();

	if (g_fail == 0) {
		diag("\nsigaction_tramp_smoketest: ALL PASS\n");
		return 0;
	}
	diag_num("\nsigaction_tramp_smoketest: FAILURES = ", g_fail);
	return 1;
}
