/* Real CONTENDED coverage for build_libplatform.sh's libplatform.a -
 * DAR-227's item #6: libplatform_smoketest.c (this file's uncontended
 * sibling) deliberately never links libpthread.a, to exercise
 * libplatform.a standalone (see build_libplatform.sh's own header
 * comment on that split) - so real cross-thread contention on
 * os_unfair_lock/os_once was never actually exercised anywhere, only
 * the single-thread fast path. That gap is exactly what let the
 * DAR-222 bug class (locking silently dropped on a "provably
 * single-threaded" premise that stopped being true 2026-08-20) go
 * uncaught by any smoketest. This is a separate, additive binary
 * rather than a retrofit of the original, so that one keeps testing
 * libplatform.a with zero pthread dependency, as designed.
 *
 * Does NOT use pthread_join(): confirmed while writing this test that
 * it doesn't exist anywhere in this project (real, separate, deep gap -
 * see pthread_main_thread_bootstrap.c's own header comment: real
 * bsdthread_create() never returns a real thread port, so there is no
 * way to individually target/wait on a spawned thread yet; a thread
 * only stops when the whole task exits). Worked around here with a
 * shared atomic completion counter main spins on instead - out of
 * scope for DAR-227 to fix, not silently worked around without saying
 * so.
 */

#include <os/lock.h>
#include <os/lock_private.h>
#include <os/once_private.h>
#include <libkern/OSAtomic.h>
#include <pthread.h>

typedef unsigned long size_t;

extern int pthread_tsd_shim_bootstrap(void);
extern int libplatform_thread_self_bootstrap(void);

static long syscall3(long num, long a0, long a1, long a2) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x16 asm("x16") = num;
    asm volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x16) : "memory", "cc");
    return x0;
}

static void put(const char *s) {
    const char *p = s;
    while (*p) {
        p++;
    }
    syscall3(4 /* SYS_write */, 1, (long)s, (long)(p - s));
}

void exit(int code) __attribute__((noreturn));
void exit(int code) {
    syscall3(1 /* SYS_exit */, code, 0, 0);
    __builtin_unreachable();
}
#define _exit exit

/* Real contended os_unfair_lock: NUM_THREADS threads each do
 * ITERS_PER_THREAD increments of a shared, lock-protected counter with
 * no atomic instruction of its own (`g_shared_counter++`, a real
 * read-modify-write race if the lock ever fails to exclude). If
 * os_unfair_lock's real fast-path cmpxchg/TSD-owner logic is broken
 * under real contention (the exact bug class a no-op lock can never
 * catch), the final count comes up short - a silent wrong answer, not a
 * crash, so this checks the arithmetic exactly, not just "didn't hang". */
#define NUM_THREADS 4
#define ITERS_PER_THREAD 2000

static os_unfair_lock g_lock = OS_UNFAIR_LOCK_INIT;
static long g_shared_counter = 0;

static os_once_t g_once_token;
static volatile int g_once_ran = 0;
static volatile int g_once_concurrent_entries = 0;
static volatile int g_once_saw_concurrent_entry = 0;

static void once_init(void *ctxt) {
    (void)ctxt;
    /* If os_once() ever let two callers into this body concurrently
     * (the exact failure this ticket's pthread_once/os_once fixes
     * exist to prevent), this counter observes it directly. */
    __atomic_fetch_add(&g_once_concurrent_entries, 1, __ATOMIC_RELAXED);
    if (__atomic_load_n(&g_once_concurrent_entries, __ATOMIC_RELAXED) > 1) {
        g_once_saw_concurrent_entry = 1;
    }
    g_once_ran++;
    __atomic_fetch_sub(&g_once_concurrent_entries, 1, __ATOMIC_RELAXED);
}

static volatile int g_workers_done = 0;

static void *worker(void *arg) {
    (void)arg;
    for (int i = 0; i < ITERS_PER_THREAD; i++) {
        os_unfair_lock_lock(&g_lock);
        g_shared_counter++;
        os_unfair_lock_unlock(&g_lock);

        _os_once(&g_once_token, 0, once_init);
    }
    __atomic_fetch_add(&g_workers_done, 1, __ATOMIC_RELEASE);
    return 0;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (!pthread_tsd_shim_bootstrap()) {
        put("FAIL: pthread_tsd_shim_bootstrap\n");
        _exit(1);
    }
    if (!libplatform_thread_self_bootstrap()) {
        put("FAIL: libplatform_thread_self_bootstrap\n");
        _exit(1);
    }

    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        int rv = pthread_create(&threads[i], 0, worker, 0);
        if (rv != 0) {
            put("FAIL: pthread_create\n");
            _exit(1);
        }
    }
    /* No real pthread_join() in this project yet (see this file's own
     * header) -- spin on the shared completion counter instead. */
    while (__atomic_load_n(&g_workers_done, __ATOMIC_ACQUIRE) < NUM_THREADS) {
        __asm__ __volatile__("yield" ::: "memory");
    }

    if (g_shared_counter != (long)NUM_THREADS * ITERS_PER_THREAD) {
        put("FAIL: os_unfair_lock lost updates under real contention\n");
        _exit(1);
    }
    put("PASS: real os_unfair_lock survives contended multi-thread stress\n");

    if (g_once_ran != 1) {
        put("FAIL: os_once ran more than once under real contention\n");
        _exit(1);
    }
    if (g_once_saw_concurrent_entry) {
        put("FAIL: os_once let two callers run its init body concurrently\n");
        _exit(1);
    }
    put("PASS: real os_once runs exactly once under real contention\n");

    put("ALL PASS: libplatform contention smoketest\n");
    _exit(0);
}
