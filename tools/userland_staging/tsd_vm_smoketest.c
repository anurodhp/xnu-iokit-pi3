/* Real fresh-thread TSD hygiene + anonymous-memory zero-fill smoketest
 * (DAR-258).
 *
 * DAR-258 reports that a brand-new pthread_create()'d thread's
 * tsd[dispatch_wlh_key] slot already reads back DISPATCH_WLH_ANON
 * (0xFFFFFFFFFFFFFFFC) before that thread has ever called
 * _dispatch_adopt_wlh_anon() or any other wlh-setting API, reproducibly
 * across boots. The ticket lists two candidate causes and confirms
 * neither: (1) this project's from-scratch kernel VM not honestly
 * zero-filling freshly mapped anonymous pages -- "a much bigger and more
 * dangerous concern than this one symptom" -- or (2) something in the
 * from-scratch thread-bootstrap path.
 *
 * Everything about that observation is libdispatch-specific only by
 * accident. dispatch_wlh_key is, on this build, an ordinary DYNAMIC key
 * from pthread_key_create() (shims/tsd.h's `#else` branch -- this
 * project sets USE_APPLE_TSD_OPTIMIZATIONS=0/HAVE_PTHREAD_KEY_INIT_NP=0
 * in dispatch_compat_headers/config/config_ac.h, so DISPATCH_USE_DIRECT_TSD
 * is never defined and the fixed __PTK_LIBDISPATCH_KEY7 slot at
 * shims/tsd.h:93 is not the one in play), and DISPATCH_WLH_ANON is just
 * the constant ~0x3ul (event/event_internal.h:99). So the whole claim
 * restates in plain POSIX terms, with no libdispatch involved at all:
 *
 *   "a brand-new thread's pthread_getspecific(k) does not return NULL
 *    for a key that thread never set"
 *
 * ...which is exactly what TEST 1 below checks, using DISPATCH_WLH_ANON's
 * own bit pattern as the sentinel so a hit here is literally the same
 * value DAR-258 saw, not an analogue.
 *
 * TEST 1 also discriminates between the ticket's two candidates, which a
 * dispatch-side probe could not:
 *   - Thread A reads its keys (expect all NULL), then writes the sentinel
 *     into every key and exits.
 *   - Thread B is created only AFTER A is joined, and reads the same keys.
 *   - If B reads back A's sentinel, this is stale memory being handed to a
 *     new thread -- _pthread_allocate() (third_party/libpthread/src/
 *     pthread.c, real unmodified source) mach_vm_map()s a genuinely fresh
 *     VM_FLAGS_ANYWHERE region per thread with no cache or free list, and
 *     struct _pthread's tsd[] array lives inside that region, so a
 *     recycled non-zero mapping is candidate (1).
 *   - If B reads NULL but the ORIGINAL dispatch symptom still occurs, the
 *     TSD path is clean and the fault is candidate (2).
 * Both threads print their own pthread_self() so an identical address
 * across A and B makes address reuse explicit rather than inferred.
 *
 * TEST 2 checks the same zero-fill question directly and generally, at
 * the mmap(MAP_ANON) level: map, verify zero, poison, unmap, re-map the
 * same size, verify zero again. Anonymous memory reading back non-zero
 * after a map/unmap/map cycle would be a real, systemic VM correctness
 * bug affecting far more than one TSD slot, so this test is worth having
 * permanently regardless of what DAR-258 turns out to be.
 *
 * Output is raw write(2), never printf() -- see kqueue_smoketest.c's own
 * header comment for the real, separate, still-undiagnosed printf()
 * crash in this minimal link recipe.
 */
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

/* DISPATCH_WLH_ANON, verbatim from third_party/libdispatch/src/event/
 * event_internal.h:99 -- ((dispatch_wlh_t)(void*)(~0x3ul)). Used as this
 * test's sentinel so a hit reproduces DAR-258's exact observed bit
 * pattern rather than merely resembling it. */
#define WLH_ANON_SENTINEL ((void *)(~0x3ul))

/* Enough keys to land in the same dynamic-key range DAR-258 observed
 * (key numbers 260-270; pthread_key_create() allocates upward from
 * _INTERNAL_POSIX_THREAD_KEYS_MAX == 256, see
 * third_party/libpthread/src/pthread_tsd.c). */
#define NKEYS 16

static int g_out = 2;
static pthread_key_t g_keys[NKEYS];
static int g_nkeys;

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

/* ---------------- TEST 1: fresh-thread TSD hygiene ---------------- */

static int g_dirty_a;   /* non-NULL slots seen by thread A */
static int g_dirty_b;   /* non-NULL slots seen by thread B */
static int g_sentinel_b;/* slots in B reading back A's sentinel */

/* This project's libsystem_pthread.dylib exports pthread_create/
 * pthread_detach/pthread_exit/pthread_self/pthread_key_create/
 * pthread_{get,set}specific but NOT pthread_join (confirmed via
 * `dyld_info -exports`) -- a real, separate gap in the port, not
 * something this test should paper over by linking a private copy. So A
 * is joined by hand: it sets g_a_done as its very last act, main spins
 * until it sees that, then sleeps a further second so A has provably
 * reached thread exit and released its stack/struct _pthread mapping
 * before B is created. The sleep is what makes the address-reuse
 * question meaningful; without it B could legitimately get a different
 * region simply because A's was still live. */
static volatile int g_a_done;
static volatile int g_b_done;


static int scan_keys(const char *who, int *sentinel_hits)
{
	int dirty = 0;
	int i;

	for (i = 0; i < g_nkeys; i++) {
		void *v = pthread_getspecific(g_keys[i]);
		if (v != NULL) {
			dirty++;
			if (v == WLH_ANON_SENTINEL && sentinel_hits) (*sentinel_hits)++;
			diag(who);
			diag_num("  DIRTY key index ", i);
			diag_num("    key number     ", (long)g_keys[i]);
			diag_hex("    value          ", (unsigned long)v);
		}
	}
	return dirty;
}

static void *thread_a(void *arg)
{
	int i;

	(void)arg;
	diag_hex("[T1] thread A pthread_self  = ", (unsigned long)pthread_self());
	g_dirty_a = scan_keys("[T1] A", NULL);
	diag_num("[T1] thread A non-NULL slots on entry = ", g_dirty_a);

	/* Poison every key with DAR-258's exact sentinel value, then exit. */
	for (i = 0; i < g_nkeys; i++) {
		pthread_setspecific(g_keys[i], WLH_ANON_SENTINEL);
	}
	diag("[T1] thread A poisoned all keys with 0xfffffffffffffffc\n");
	g_a_done = 1;
	return NULL;
}

static void *thread_b(void *arg)
{
	(void)arg;
	diag_hex("[T1] thread B pthread_self  = ", (unsigned long)pthread_self());
	g_dirty_b = scan_keys("[T1] B", &g_sentinel_b);
	diag_num("[T1] thread B non-NULL slots on entry = ", g_dirty_b);
	diag_num("[T1] thread B slots reading A's sentinel = ", g_sentinel_b);
	g_b_done = 1;
	return NULL;
}

static int test1_tsd(void)
{
	pthread_t a, b;
	int i, rc, spins;

	diag("\n=== TEST 1: brand-new thread TSD hygiene (DAR-258) ===\n");

	for (i = 0; i < NKEYS; i++) {
		if (pthread_key_create(&g_keys[i], NULL) != 0) break;
		g_nkeys++;
	}
	diag_num("[T1] dynamic keys created = ", g_nkeys);
	if (g_nkeys == 0) {
		diag("[T1] FAIL: pthread_key_create() created no keys\n");
		return 1;
	}
	diag_num("[T1] first key number     = ", (long)g_keys[0]);
	diag_num("[T1] last  key number     = ", (long)g_keys[g_nkeys - 1]);

	rc = pthread_create(&a, NULL, thread_a, NULL);
	if (rc != 0) { diag_num("[T1] FAIL: pthread_create(A) rc=", rc); return 1; }
	pthread_detach(a);
	for (spins = 0; spins < 200 && !g_a_done; spins++) sleep(1);
	if (!g_a_done) { diag("[T1] FAIL: thread A never finished\n"); return 1; }
	sleep(1);  /* let A actually reach thread exit, see g_a_done comment */

	/* B is created only after A has exited, so if B lands on A's recycled
	 * struct _pthread the reuse is unambiguous. */
	rc = pthread_create(&b, NULL, thread_b, NULL);
	if (rc != 0) { diag_num("[T1] FAIL: pthread_create(B) rc=", rc); return 1; }
	pthread_detach(b);
	for (spins = 0; spins < 200 && !g_b_done; spins++) sleep(1);
	if (!g_b_done) { diag("[T1] FAIL: thread B never finished\n"); return 1; }

	if (g_dirty_a != 0) {
		diag("[T1] RESULT: FAIL -- thread A saw non-NULL TSD on a brand-new "
		     "thread with nothing yet written anywhere\n");
		return 1;
	}
	if (g_sentinel_b != 0) {
		diag("[T1] RESULT: FAIL -- thread B read back thread A's sentinel; "
		     "a new thread inherited a dead thread's TSD (stale/recycled "
		     "struct _pthread memory) -- DAR-258 candidate (1)\n");
		return 1;
	}
	if (g_dirty_b != 0) {
		diag("[T1] RESULT: FAIL -- thread B saw non-NULL TSD that is not "
		     "A's sentinel\n");
		return 1;
	}
	diag("[T1] RESULT: PASS -- both brand-new threads read NULL for every "
	     "never-set dynamic key\n");
	return 0;
}

/* ------------- TEST 2: anonymous-memory zero-fill on reuse ------------- */

#define MAPSZ (256 * 1024)
#define ROUNDS 3

static int test2_zerofill(void)
{
	unsigned char *p, *prev = NULL;
	int round, fail = 0;
	size_t j;

	diag("\n=== TEST 2: mmap(MAP_ANON) zero-fill across map/unmap/map ===\n");

	for (round = 0; round < ROUNDS; round++) {
		p = mmap(NULL, MAPSZ, PROT_READ | PROT_WRITE,
			 MAP_ANON | MAP_PRIVATE, -1, 0);
		if (p == MAP_FAILED) {
			diag_num("[T2] FAIL: mmap failed on round ", round);
			return 1;
		}
		diag_num("[T2] round ", round);
		diag_hex("[T2]   address = ", (unsigned long)p);
		if (prev != NULL && p == prev) {
			diag("[T2]   (same address as previous round -- real reuse)\n");
		}

		for (j = 0; j < MAPSZ; j++) {
			if (p[j] != 0) {
				fail = 1;
				diag_num("[T2]   FAIL: first non-zero byte at offset ", (long)j);
				diag_hex("[T2]          value = ", (unsigned long)p[j]);
				break;
			}
		}
		if (j == MAPSZ) diag("[T2]   all bytes zero on fresh map\n");

		memset(p, 0xAB, MAPSZ);
		prev = p;
		if (munmap(p, MAPSZ) != 0) {
			diag("[T2] FAIL: munmap failed\n");
			return 1;
		}
	}

	if (fail) {
		diag("[T2] RESULT: FAIL -- freshly mapped anonymous memory is not "
		     "zero-filled; this is a systemic VM correctness bug, not a "
		     "TSD issue\n");
		return 1;
	}
	diag("[T2] RESULT: PASS -- every fresh anonymous mapping read back "
	     "fully zero\n");
	return 0;
}

int main(void)
{
	int fd, rc1, rc2;

	fd = open("/dev/console", O_WRONLY);
	if (fd >= 0) g_out = fd;

	diag("tsd_vm_smoketest: start (DAR-258)\n");
	rc1 = test1_tsd();
	rc2 = test2_zerofill();

	if (rc1 == 0 && rc2 == 0) {
		diag("\ntsd_vm_smoketest: ALL PASS\n");
		return 0;
	}
	diag("\ntsd_vm_smoketest: FAILURES ABOVE\n");
	return 1;
}
