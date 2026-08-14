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
 * once. */
static int g_tsd_bootstrapped = 0;

/* REAL BUG FOUND BY ADVERSARIAL REVIEW, FIXED HERE: this used to be
 * called lazily, on first entry to this shim's own
 * pthread_key_create/setspecific/getspecific. That's not good enough -
 * real Libc's actual fast path for TSD reads
 * (third_party/Libc/locale/xlocale_private.h's __current_locale() macro:
 * `_pthread_has_direct_tsd() ? _pthread_getspecific_direct(key) :
 * pthread_getspecific(key)` - and _pthread_has_direct_tsd() returns 1
 * unconditionally off-simulator, confirmed by reading
 * third_party/libpthread/private/pthread/tsd_private.h directly) calls
 * _os_tsd_get_direct() -> raw `mrs TPIDRRO_EL0` + index, WITHOUT ever
 * calling into this shim's pthread_getspecific() at all. So the lazy
 * self-bootstrap this file's own functions do (kept below as a defensive
 * fallback for callers that DO go through them) never triggers for that
 * fast path - on a freshly exec'd thread TPIDRRO_EL0 is 0, so real Libc
 * code hitting the fast path before this function has been called
 * explicitly reads/writes through a NULL-based index. This function is
 * NOT optional/lazy - it MUST be called explicitly, as close to the
 * first instruction of a real program's main() as possible, before
 * calling anything that might touch Libc's locale/TSD-backed state. */
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

/* REAL BUG FOUND BY ADVERSARIAL REVIEW, FIXED HERE: `__locale_key`
 * (third_party/Libc/locale/xlocale.c) starts at (pthread_key_t)-1 and is
 * only ever assigned by the real `__xlocale_init()` - whose only real
 * caller in the apple-oss-distributions tree is `sys/_libc_init.c`,
 * which this project doesn't compile or call. Without this,
 * __current_locale() indexes TSD slot -1 (i.e. 8 bytes *before* this
 * shim's TSD block), which only "worked" by accident (adjacent memory
 * happened to be zero) - not a real, trustworthy result. `__xlocale_init()`
 * itself is small, real, and safe to call directly (extern, declared in
 * xlocale_private.h, defined in xlocale.c - just claims a fixed reserved
 * TSD key via this shim's own real pthread_key_init_np()) - see
 * pthread_tsd_shim_libc_bootstrap.c (separate file, deliberately: this
 * file must stay usable standalone with zero Libc-locale dependency,
 * e.g. for pthread_tsd_shim_smoketest.c, which never touches locale
 * code at all - putting an extern reference to __xlocale_init directly
 * in THIS translation unit would force every consumer of this file to
 * also link a full locale archive, even ones that only need bare TSD).
 */

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

/* REAL BUG FOUND BY ADVERSARIAL REVIEW, FIXED HERE: every key parameter
 * in this section used to be a plain `int`/`int *`. Real `pthread_key_t`
 * is `unsigned long` (8 bytes on this LP64 target - confirmed against
 * the real SDK's sys/_pthread/_pthread_types.h). Real callers (e.g.
 * third_party/Libc/string/FreeBSD/strsignal.c, which declares
 * `pthread_key_t sig_key`) pass a `pthread_key_t *` - writing through
 * that as an `int *` (4 bytes) only ever wrote the low 4 bytes of an
 * 8-byte slot, leaving the real variable's value ABI-dependent on
 * whatever was in the untouched upper 4 bytes rather than reliably
 * correct. Caught by the compiler itself
 * ("incompatible pointer types passing 'pthread_key_t *' ... to
 * parameter of type 'int *'") once `-w` was removed and this file's
 * prototypes became visible to real callers - exactly the class of bug
 * `-w` had been hiding. */
int pthread_key_create(unsigned long *key, void (*destructor)(void *)) {
    pthread_tsd_shim_bootstrap();
    for (unsigned long k = TSD_DYNAMIC_START; k < TSD_DYNAMIC_END; k++) {
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
 * extern int pthread_key_init_np(int, void (*)(void *)); - the key here
 * genuinely is a plain `int` in the real declaration (a compile-time
 * constant reserved slot number, not a pthread_key_t* out-param like
 * pthread_key_create takes) - this one was already correct. */
int pthread_key_init_np(int key, void (*destructor)(void *)) {
    pthread_tsd_shim_bootstrap();
    if (key < TSD_FIRST || key >= TSD_DYNAMIC_START) {
        return 22 /* EINVAL */;
    }
    g_key_destructors[key] = ~(unsigned long)destructor;
    return 0;
}

int pthread_setspecific(unsigned long key, const void *value) {
    pthread_tsd_shim_bootstrap();
    if (key < TSD_FIRST || key >= TSD_DYNAMIC_END) {
        return 22 /* EINVAL */;
    }
    int created = (g_key_destructors[key] != 0);
    if (key < TSD_DYNAMIC_START || created) {
        _os_tsd_set_direct(key, (void *)value);
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

void *pthread_getspecific(unsigned long key) {
    pthread_tsd_shim_bootstrap();
    return _os_tsd_get_direct(key);
}

/* pthread_once: REAL bug found by adversarial review and fixed here -
 * pthread_once_t is NOT a plain long. Real Darwin layout (cross-checked
 * against the exact SDK this project's locale build uses:
 * sys/_pthread/_pthread_types.h's struct _opaque_pthread_once_t, and
 * pthread_impl.h's _PTHREAD_ONCE_SIG_init):
 *   struct _opaque_pthread_once_t { long __sig; char __opaque[8]; };
 *   PTHREAD_ONCE_INIT = { 0x30B1BCBA, {0} }  // "not yet run" sentinel
 * A prior version of this function treated the whole struct as a plain
 * `long` and checked `== 0` - PTHREAD_ONCE_INIT's real __sig is
 * 0x30B1BCBA, never 0, so that check never fired and init_routine()
 * silently never ran (returning success regardless). Real consumer
 * already in this project's 109-file Libc set:
 * third_party/Libc/string/FreeBSD/strsignal.c's `thr_once(&sig_init_once,
 * sig_keycreate)` - was silently never initializing its TSD key.
 * Single-threaded target: no race to guard against between the check and
 * the call, so no lock is needed for correctness (same reasoning as this
 * file's other omissions), only the real sentinel value matters. */
struct pthread_once_shim {
    long sig;
    char opaque[8];
};

/* "Done" sentinel deliberately chosen as neither 0 nor the real
 * PTHREAD_ONCE_INIT signature (0x30B1BCBA), so BOTH common
 * initialization idioms correctly trigger exactly one real run: a
 * caller using the real ONCE_INITIALIZER/PTHREAD_ONCE_INIT macro
 * (sig=0x30B1BCBA) and a caller that just zero-initializes a static/BSS
 * once_t without the macro (sig=0) both differ from this "done" value
 * and so both run init_routine() on their first call. */
#define PTHREAD_ONCE_SIG_DONE 1

int pthread_once(void *once_control, void (*init_routine)(void)) {
    struct pthread_once_shim *once = (struct pthread_once_shim *)once_control;
    if (once->sig != PTHREAD_ONCE_SIG_DONE) {
        once->sig = PTHREAD_ONCE_SIG_DONE;
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
int _pthread_key_create(unsigned long *key, void (*destructor)(void *)) {
    return pthread_key_create(key, destructor);
}

int _pthread_setspecific(unsigned long key, const void *value) {
    return pthread_setspecific(key, value);
}

void *_pthread_getspecific(unsigned long key) {
    return pthread_getspecific(key);
}

int _pthread_once(void *once_control, void (*init_routine)(void)) {
    return pthread_once(once_control, init_routine);
}
