#include "internal.h"

static void put(const char *s) {
    long len = 0;
    while (s[len]) len++;
    register long x0 asm("x0") = 1;
    register long x1 asm("x1") = (long)s;
    register long x2 asm("x2") = len;
    register long x16 asm("x16") = 4; /* write */
    asm volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x16) : "memory", "cc");
}

static void die(const char *s) {
    put(s);
    register long x0 asm("x0") = 1;
    register long x16 asm("x16") = 1; /* exit */
    asm volatile("svc #0x80" : "+r"(x0) : "r"(x16) : "memory", "cc");
    __builtin_unreachable();
}

static int check_count = 0;
static void once_fn(void) { check_count++; }

#define ITER_COUNT 512
static pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int shared_counter = 0;
static volatile int child_done = 0;

/* Real second-thread entry point. File-scope, not a nested function -
 * GNU nested
 * functions need a runtime trampoline (a small blob of code written to
 * the stack and executed from there) that this project has no reason to
 * trust/support here; a plain top-level function avoids the question
 * entirely, same shape tools/pthread_kthread_test/'s child_entry
 * already uses. */
static void *child_fn(void *arg) {
    (void)arg;
    for (int i = 0; i < ITER_COUNT; i++) {
        pthread_mutex_lock(&counter_mutex);
        shared_counter++;
        pthread_mutex_unlock(&counter_mutex);
    }
    child_done = 1;
    return 0;
}

int main(void) {
    /* DAR-426: pthread_main_thread_bootstrap() is gone (77cc1e1); this is a
     * dynamic image, so dyld runs libSystem_initializer, which calls the real
     * __pthread_init() (third_party/Libsystem/init.c:221; defined at
     * third_party/libpthread/src/pthread.c:1736) before main(). Check its
     * result instead of re-running a bootstrap. */
    if (!pthread_self()) {
        die("FAIL: bootstrap\n");
    }
    put("PASS: bootstrap\n");

    if (!pthread_equal(pthread_self(), pthread_main_thread_np())) {
        die("FAIL: pthread_self != pthread_main_thread_np\n");
    }
    if (!pthread_main_np()) {
        die("FAIL: pthread_main_np\n");
    }
    put("PASS: pthread_self/pthread_equal/pthread_main_np\n");

    static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    if (pthread_mutex_lock(&m) != 0) die("FAIL: mutex_lock\n");
    if (pthread_mutex_unlock(&m) != 0) die("FAIL: mutex_unlock\n");
    if (pthread_mutex_lock(&m) != 0) die("FAIL: mutex_lock2\n");
    if (pthread_mutex_unlock(&m) != 0) die("FAIL: mutex_unlock2\n");
    put("PASS: real (FIRSTFIT-policy) pthread_mutex lock/unlock x2\n");

    pthread_key_t key;
    if (pthread_key_create(&key, 0) != 0) die("FAIL: key_create\n");
    int value = 4242;
    if (pthread_setspecific(key, &value) != 0) die("FAIL: setspecific\n");
    int *got = (int *)pthread_getspecific(key);
    if (!got || *got != 4242) die("FAIL: getspecific mismatch\n");
    put("PASS: real pthread_key_create/setspecific/getspecific\n");

    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, once_fn);
    pthread_once(&once, once_fn);
    if (check_count != 1) die("FAIL: pthread_once ran wrong number of times\n");
    put("PASS: real pthread_once runs exactly once\n");

    /* Real multi-thread test, now that bsd_pthread_stub.c's
     * real_bsdthread_create (Phase 7.5 step 1) and this project's own
     * pthread_create() stack-allocation fix (2026-08-24) make a second
     * real thread genuinely runnable. child_fn (file scope, above) spins
     * under real contention with the main thread, both incrementing a
     * counter under the same real pthread_mutex_t 512 times each -
     * exercises the real ulock-backed contended path
     * (__pthread_mutex_use_ulock=true, set in
     * pthread_main_thread_bootstrap()) since two real threads
     * incrementing under one real mutex WILL contend at this iteration
     * count, unlike the single-thread lock/unlock pair above. No
     * pthread_join() (bsdthread_terminate/thread ports aren't wired -
     * see pthread_create()'s own header) - the main thread busy-polls a
     * shared "done" flag instead, same pattern
     * tools/pthread_kthread_test/ already established. */
    pthread_t t;
    int rc = pthread_create(&t, 0, child_fn, 0);
    if (rc != 0) die("FAIL: pthread_create real failure (unexpected)\n");
    put("PASS: pthread_create spawned a real second thread\n");

    for (int i = 0; i < ITER_COUNT; i++) {
        pthread_mutex_lock(&counter_mutex);
        shared_counter++;
        pthread_mutex_unlock(&counter_mutex);
    }

    while (!child_done) {
        /* busy-poll: no real join available yet, see pthread_create()'s
         * own header comment for why. */
    }

    if (shared_counter != ITER_COUNT * 2) {
        die("FAIL: shared_counter contended-mutex result wrong (real data race if this fires)\n");
    }
    put("PASS: real second thread + real contended pthread_mutex, correct result\n");

    put("ALL PASS: libpthread smoketest\n");
    register long x0 asm("x0") = 0;
    register long x16 asm("x16") = 1; /* exit */
    asm volatile("svc #0x80" : "+r"(x0) : "r"(x16) : "memory", "cc");
    __builtin_unreachable();
}
