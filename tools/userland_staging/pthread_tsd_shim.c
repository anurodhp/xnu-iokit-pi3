/* Real-named pthread TSD shim for linking against Libc's locale/wide-char
 * files - same verified mechanism as tsd_bootstrap.c (QEMU-confirmed
 * 2026-08-13: platform_syscall bootstrap, TPIDRRO_EL0 round-trip,
 * independent key slots all work for real on-target), re-expressed under
 * the real POSIX names (pthread_key_create/setspecific/getspecific) plus
 * pthread_key_init_np/pthread_once, which Libc's locale subsystem
 * actually calls. tsd_bootstrap.c itself is left untouched (it's the
 * already-QEMU-verified artifact - don't disturb it for a refactor).
 *
 * See tsd_bootstrap.c's own header comment for the full ABI trace
 * (platform_syscall calling convention, TSD block layout,
 * _os_tsd_get_direct/_os_tsd_set_direct) - not repeated here, only
 * genuinely new material is commented below.
 */

#include "../../third_party/xnu-7195/libsyscall/os/tsd.h"

/* ---- platform_syscall bootstrap (identical mechanism to tsd_bootstrap.c) */

#define PLATFORM_SYSCALL_TRAP_NO 0x80000000

static void platform_syscall_set_cthread_self(void *tsd_base) {
    register long x0 asm("x0") = (long)tsd_base;
    register long x3 asm("x3") = 2; /* set cthread self */
    register long x16 asm("x16") = PLATFORM_SYSCALL_TRAP_NO;
    asm volatile("svc #0x80" : "+r"(x0) : "r"(x3), "r"(x16) : "memory", "cc", "x1", "x2");
}

static void *platform_syscall_get_cthread_self(void) {
    register long x0 asm("x0") = 0;
    register long x3 asm("x3") = 3; /* get cthread self */
    register long x16 asm("x16") = PLATFORM_SYSCALL_TRAP_NO;
    asm volatile("svc #0x80" : "+r"(x0) : "r"(x3), "r"(x16) : "memory", "cc", "x1", "x2");
    return (void *)x0;
}

#define TSD_HEADER_SIZE 224
#define TSD_SLOT_COUNT  768

struct tsd_block {
    unsigned char header[TSD_HEADER_SIZE];
    void *tsd[TSD_SLOT_COUNT];
};

/* Self-review note: this file and tsd_bootstrap.c each keep their OWN
 * static g_tsd_block/bootstrap state - correct as long as only one of
 * them ever runs the platform_syscall bootstrap in a given process (true
 * today: tsd_bootstrap.c is its own standalone smoketest binary, never
 * linked together with this file's consumers). If that ever changes,
 * whichever bootstraps SECOND wins TPIDRRO_EL0 and the other's TSD block
 * silently goes stale - don't link both into the same binary without
 * reconciling this. */
__attribute__((aligned(16))) static struct tsd_block g_tsd_block;

/* Returns 1 if confirmed live (see tsd_bootstrap.c for why the readback
 * check matters: machine_thread_set_tsd_base()'s failure path is silent
 * to platform_syscall()'s caller). Idempotent/safe to call more than
 * once - callers below call it lazily on first use. */
static int g_tsd_bootstrapped = 0;

int pthread_tsd_shim_bootstrap(void) {
    if (g_tsd_bootstrapped) {
        return 1;
    }
    void *base = (void *)&g_tsd_block.tsd[0];
    platform_syscall_set_cthread_self(base);
    void *readback = platform_syscall_get_cthread_self();
    g_tsd_bootstrapped = (readback == base);
    return g_tsd_bootstrapped;
}

/* ---- pthread_key_create/setspecific/getspecific/key_init_np/once,
 * real names, same adapted algorithm as tsd_bootstrap.c (see that file
 * for the full real-vs-simplified breakdown - identical here: faithful
 * to third_party/libpthread/src/pthread_tsd.c's constants/logic, real
 * os_unfair_lock omitted because this target is provably single-threaded
 * so there is no concurrent access to protect against). ---------------- */

#define TSD_RESERVED_MAX  9
#define TSD_FIRST         (TSD_RESERVED_MAX + 1) /* 10 */
#define TSD_DYNAMIC_START 256
#define TSD_DYNAMIC_END   768

static unsigned long g_key_destructors[TSD_DYNAMIC_END];

int pthread_key_create(int *key, void (*destructor)(void *)) {
    pthread_tsd_shim_bootstrap();
    for (int k = TSD_DYNAMIC_START; k < TSD_DYNAMIC_END; k++) {
        unsigned long *ptr = &g_key_destructors[k];
        unsigned long value = ~(unsigned long)destructor;
        if (*ptr == 0) {
            *ptr = value;
            *key = k;
            return 0;
        }
    }
    return 35 /* EAGAIN */;
}

/* Real signature per third_party/libpthread/private/pthread/tsd_private.h:
 * extern int pthread_key_init_np(int, void (*)(void *)); - registers a
 * destructor for a FIXED low/reserved key (real callers: Libc's locale
 * subsystem claiming one of its own well-known slots), same real
 * semantics as third_party/libpthread/src/pthread_tsd.c's version. */
int pthread_key_init_np(int key, void (*destructor)(void *)) {
    pthread_tsd_shim_bootstrap();
    if (key < TSD_FIRST || key >= TSD_DYNAMIC_START) {
        return 22 /* EINVAL */;
    }
    g_key_destructors[key] = ~(unsigned long)destructor;
    return 0;
}

int pthread_setspecific(int key, const void *value) {
    pthread_tsd_shim_bootstrap();
    if (key < TSD_FIRST || key >= TSD_DYNAMIC_END) {
        return 22 /* EINVAL */;
    }
    int created = (g_key_destructors[key] != 0);
    if (key < TSD_DYNAMIC_START || created) {
        _os_tsd_set_direct((unsigned long)key, (void *)value);
        if (key < TSD_DYNAMIC_START) {
            /* Matches real pthread_tsd.c: mark "claimed, no destructor",
             * not "never claimed" - see tsd_bootstrap.c's own self-review
             * note for why this specific value (all-ones, not zero)
             * matters. */
            g_key_destructors[key] = ~0UL;
        }
        return 0;
    }
    return 22 /* EINVAL */;
}

void *pthread_getspecific(int key) {
    pthread_tsd_shim_bootstrap();
    return _os_tsd_get_direct((unsigned long)key);
}

/* pthread_once: real signature is (pthread_once_t *, void (*)(void)),
 * pthread_once_t is typically `long` (0 = not yet run). Single-threaded
 * target: no race to guard against between the check and the call, so
 * this is a correct, minimal implementation, not a stubbed one. */
int pthread_once(long *once_control, void (*init_routine)(void)) {
    if (*once_control == 0) {
        *once_control = 1;
        init_routine();
    }
    return 0;
}

/* Real Apple private aliases (third_party/Libc/fbsdcompat/reentrant.h maps
 * FreeBSD-lineage thr_keycreate/thr_setspecific/thr_getspecific/thr_once
 * to these underscore-prefixed names, not the public POSIX ones) - real,
 * separate exported symbols in real Libc, not just a macro-level rename;
 * thin wrappers here since this shim's implementation is the same either
 * way. */
int _pthread_key_create(int *key, void (*destructor)(void *)) {
    return pthread_key_create(key, destructor);
}

int _pthread_setspecific(int key, const void *value) {
    return pthread_setspecific(key, value);
}

void *_pthread_getspecific(int key) {
    return pthread_getspecific(key);
}

int _pthread_once(long *once_control, void (*init_routine)(void)) {
    return pthread_once(once_control, init_routine);
}
