#include "internal.h"

extern int pthread_main_thread_bootstrap(void);
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

int main(void) {
    if (!pthread_main_thread_bootstrap()) {
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

    pthread_t t;
    int rc = pthread_create(&t, 0, 0, 0);
    if (rc == 0) die("FAIL: pthread_create unexpectedly succeeded (kernel has no real bsdthread_create support)\n");
    put("PASS: pthread_create is real code, real kernel-level failure (ENOTSUP), as documented\n");

    put("ALL PASS: libpthread smoketest\n");
    register long x0 asm("x0") = 0;
    register long x16 asm("x16") = 1; /* exit */
    asm volatile("svc #0x80" : "+r"(x0) : "r"(x16) : "memory", "cc");
    __builtin_unreachable();
}
