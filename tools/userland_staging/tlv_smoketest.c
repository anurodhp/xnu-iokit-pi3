/*
 * tlv_smoketest.c -- real arm64 Thread-Local Variable (__thread) probe.
 *
 * Written for DAR-279 (found while investigating DAR-270). Until
 * 2026-09-09 this port had no real TLV
 * support at all: dyld's own machinery ran correctly and installed
 * libdyld's tlv_get_addr into every TLV descriptor, but that function
 * was a `return 0;` stand-in (libdyld_compat_misc.c), so the very first
 * __thread access in any process dereferenced NULL. Nothing detected
 * that for as long as no shipped binary happened to declare a __thread
 * variable; the moment one did (libSystemConfiguration.dylib's
 * SCNetworkInterface.c S_interface_cache, reached by configd once en0
 * existed) it became an unbroken crash loop.
 *
 * This exists so that failure mode can never come back silently. It
 * checks the three things that actually distinguish working TLV from
 * every plausible half-working version:
 *
 *   1. INITIAL CONTENT. A __thread with an initializer must read back
 *      that initializer, and a zero-initialized one must read 0. That
 *      is dyld copying the image's __thread_data/__thread_bss template
 *      into the per-thread buffer (RuntimeState::_instantiateTLVs), not
 *      just handing out any writable page.
 *   2. PER-THREAD SEPARATION. A second thread must see the pristine
 *      initial values, not whatever the main thread stored, and the two
 *      threads' addresses for the same variable must differ. This is
 *      the part a naive "return a single static buffer" implementation
 *      would fail while looking fine in a single-threaded test.
 *   3. PERSISTENCE. After the second thread has run and exited, the
 *      main thread's own values must be exactly what it stored. This
 *      catches a fast path that recomputes or reallocates instead of
 *      finding the existing buffer through the thread's TSD.
 *
 * Both the fast path (TSD slot already populated) and the lazy-allocate
 * slow path are exercised: every thread's first touch takes the slow
 * path through _instantiateTLVs_thunk, every later touch takes the
 * TPIDRRO_EL0 fast path.
 *
 * Exit status is 0 only if every check passed; the final line is
 * "tlv_smoketest: PASS" or "tlv_smoketest: FAIL (n)" so a boot test can
 * assert on it directly.
 */
#include <stdio.h>
#include <pthread.h>
#include <sched.h>

/* One in __thread_data (has an initializer) and one in __thread_bss
 * (does not) -- dyld handles the two through the same descriptor but
 * different halves of the initial-content template, so testing only one
 * would leave half the copy untested. */
static __thread unsigned long tlv_initialized = 0xA5A5A5A5UL;
static __thread unsigned long tlv_zero;

#define TLV_INIT_VALUE   0xA5A5A5A5UL
#define MAIN_STORE_A     0x1111111111111111UL
#define MAIN_STORE_B     0x2222222222222222UL
#define CHILD_STORE_A    0x3333333333333333UL
#define CHILD_STORE_B    0x4444444444444444UL

static int failures;

static void
check(const char *what, unsigned long got, unsigned long want)
{
	if (got == want) {
		printf("tlv_smoketest: ok   %s = 0x%lx\n", what, got);
	} else {
		printf("tlv_smoketest: FAIL %s = 0x%lx, expected 0x%lx\n",
		    what, got, want);
		failures++;
	}
}

/* Captured so the main thread can prove the child got a different
 * allocation rather than sharing one buffer. */
static unsigned long *child_addr_of_initialized;

/* The child->main handoff is a plain volatile flag plus sched_yield(),
 * and that is deliberate.
 *
 * pthread_join() is not among this port's real libsystem_pthread.dylib
 * exports (pthread_create/pthread_detach/pthread_exit are). The obvious
 * substitute -- a mutex plus condition variable -- was tried first and
 * turned out to take SIGSEGV on this port: a real QEMU boot put the
 * main thread in _pthread_cond_wait+0x2a8 reading through a NULL
 * pointer (far 0x26). That is a real, separate bug, filed on its own;
 * it is not this test's subject, and depending on it would mean this
 * test could never report PASS for reasons that have nothing to do with
 * TLV. A volatile flag needs nothing but loads and stores.
 *
 * Bounded, so a child that never runs fails loudly instead of hanging
 * the boot test that invokes this. */
static volatile int child_done;
#define HANDOFF_SPIN_LIMIT 100000000

static void *
child_main(void *arg)
{
	(void)arg;

	/* First touch on this thread: the lazy-allocate slow path. The
	 * values must be the image's template, NOT what the main thread
	 * stored before spawning us. */
	child_addr_of_initialized = &tlv_initialized;
	check("child initial tlv_initialized", tlv_initialized, TLV_INIT_VALUE);
	check("child initial tlv_zero", tlv_zero, 0UL);

	tlv_initialized = CHILD_STORE_A;
	tlv_zero = CHILD_STORE_B;

	/* Second touch: the TPIDRRO_EL0 fast path, same buffer. */
	check("child stored tlv_initialized", tlv_initialized, CHILD_STORE_A);
	check("child stored tlv_zero", tlv_zero, CHILD_STORE_B);

	/* Progress markers, not decoration: the TLV checks above and the
	 * thread handoff below are separate subsystems, and when this test
	 * first ran for real it passed every TLV check and then took
	 * SIGSEGV somewhere after them. The markers are what identified the
	 * crash as _pthread_cond_wait's, not TLV's, from the boot log alone.
	 * Keep them. */
	printf("tlv_smoketest: mark child-signal\n");
	child_done = 1;
	return NULL;
}

int
main(int argc, char **argv)
{
	/* argv[1] == "nothread" runs only the main thread's own checks.
	 * The two halves of this test exercise different machinery -- TLV
	 * itself vs. pthread_create and thread handoff -- and a crash in
	 * the second half would otherwise read as "TLV is broken" when it
	 * is not. This mode is how you tell those apart in one command, and
	 * it is exactly how the _pthread_cond_wait crash above was pinned
	 * down: `nothread` passed while the full run did not. */
	int nothread = (argc > 1 && argv[1][0] == 'n');

	printf("tlv_smoketest: start%s\n", nothread ? " (nothread)" : "");

	unsigned long *main_addr_of_initialized = &tlv_initialized;

	check("main initial tlv_initialized", tlv_initialized, TLV_INIT_VALUE);
	check("main initial tlv_zero", tlv_zero, 0UL);

	tlv_initialized = MAIN_STORE_A;
	tlv_zero = MAIN_STORE_B;
	check("main stored tlv_initialized", tlv_initialized, MAIN_STORE_A);
	check("main stored tlv_zero", tlv_zero, MAIN_STORE_B);

	if (!nothread) {
		pthread_t th;
		printf("tlv_smoketest: mark main-create\n");
		int rc = pthread_create(&th, NULL, child_main, NULL);
		if (rc != 0) {
			printf("tlv_smoketest: FAIL pthread_create rc=%d\n", rc);
			failures++;
		} else {
			printf("tlv_smoketest: mark main-wait\n");
			long spins = 0;
			while (!child_done && spins < HANDOFF_SPIN_LIMIT) {
				sched_yield();
				spins++;
			}
			if (!child_done) {
				printf("tlv_smoketest: FAIL child never ran "
				    "(%ld spins)\n", spins);
				failures++;
			}
			printf("tlv_smoketest: mark main-woke\n");

			if (child_addr_of_initialized == main_addr_of_initialized) {
				printf("tlv_smoketest: FAIL both threads share one "
				    "buffer at %p\n",
				    (void *)main_addr_of_initialized);
				failures++;
			} else {
				printf("tlv_smoketest: ok   per-thread addresses "
				    "differ (%p vs %p)\n",
				    (void *)main_addr_of_initialized,
				    (void *)child_addr_of_initialized);
			}
		}

		/* The child stored its own values; ours must be untouched. */
		check("main after child tlv_initialized", tlv_initialized,
		    MAIN_STORE_A);
		check("main after child tlv_zero", tlv_zero, MAIN_STORE_B);
	}

	if (failures == 0) {
		printf("tlv_smoketest: PASS\n");
		return 0;
	}
	printf("tlv_smoketest: FAIL (%d)\n", failures);
	return 1;
}
