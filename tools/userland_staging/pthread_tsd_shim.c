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
    /* REAL BUG FOUND AND FIXED HERE (2026-08-30, docs/daemon-roadmap.md
     * Phase 6.8 continued item 1): this used to unconditionally clobber
     * TPIDRRO_EL0 with THIS translation unit's own private g_tsd_block.
     * The g_tsd_bootstrapped guard above is per-COPY, and this file is
     * genuinely compiled into more than one image at once (confirmed via
     * `nm`: _g_tsd_block is defined in BOTH libsystem_malloc.dylib and
     * launchd.macho) -- so the guard cannot see that another copy, or
     * the authoritative pthread_main_thread_bootstrap(), already
     * installed a real TSD base. This file's own header comment above
     * predicted exactly this failure ("whichever bootstraps SECOND wins
     * TPIDRRO_EL0 and the other's TSD block silently goes stale"); it
     * stopped being hypothetical once launchd.macho linked the full
     * libdispatch/ICU/libc++ graph.
     *
     * Real, QEMU-diagnosed symptom this fixes: libSystem.B.dylib's
     * constructor (libsystem_dyld_initializer_compat.c) correctly ran
     * pthread_main_thread_bootstrap(), installing the real signed
     * _main_thread as the TSD base -- and then _dyld_initializer()'s
     * first malloc reached libmalloc_dispatch.c's get_zone(), which
     * calls this function, which overwrote that base with
     * libsystem_malloc.dylib's private zero-filled g_tsd_block. libc++'s
     * iostream static initializer then called pthread_self(), which
     * computed (TPIDRRO_EL0 - 0xe0), read sig == 0, failed
     * _pthread_validate_signature() and called abort_with_reason() --
     * a silent exit(1) with no console output at all. Root-caused with a
     * temporary kernel-side exit(2) user-backtrace diagnostic (in
     * bsd/kern/kern_exit.c, since reverted), because every userland-side
     * marker was invisible -- they all wrote to /dev/console, which
     * cannot distinguish "never called" from "called, write failed". See
     * docs/daemon-roadmap.md's Phase 6.8 item 1 write-up for the full
     * trail and for why that diagnostic channel choice mattered.
     *
     * Fix: adopt an already-installed base instead of clobbering it.
     * This function is the OPPORTUNISTIC bootstrap ("give me some valid
     * TSD block so os_unfair_lock/_os_tsd_get_direct work"); any base
     * already installed satisfies that contract exactly as well as our
     * own block does, and the TSD slots are then genuinely shared
     * process-wide rather than split across private per-image blocks.
     * pthread_tsd_shim_bootstrap_with_base()
     * (pthread_tsd_platform_bootstrap.c) stays unconditional on purpose
     * -- it is the AUTHORITATIVE installer used by
     * pthread_main_thread_bootstrap() to publish the real, correctly
     * signed _main_thread, and must always win regardless of call order. */
    void *existing = platform_syscall_get_cthread_self();
    if (existing != (void *)0) {
        g_tsd_bootstrapped = 1;
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
 * for the full real-vs-simplified breakdown - faithful to
 * third_party/libpthread/src/pthread_tsd.c's constants/logic).
 *
 * REAL BUG FOUND AND FIXED 2026-09-03 (DAR-227): this section used to
 * omit all synchronization, on the stated grounds that "this target is
 * provably single-threaded so there is no concurrent access to protect
 * against". That premise died on 2026-08-20 (Phase 7.5 step 1) when
 * bsdthread_create(2) stopped returning ENOTSUP and real pthread_create()
 * started spawning real kernel threads; DAR-222 then crashed on
 * libdispatch's real manager thread in a process (syslogd) simultaneously
 * running its main thread and dispatch worker threads.
 *
 * These were not theoretical races confined to a smoketest. The five
 * public functions below are __attribute__((weak)) so real
 * libsystem_pthread.dylib overrides them wherever both are linked -- but
 * libsystem_malloc.dylib links ONLY this file, and re-exports the
 * FreeBSD-compat aliases at the bottom of this file as STRONG symbols
 * (__pthread_key_create/__pthread_setspecific/__pthread_getspecific/
 * __pthread_once, confirmed via `nm -gU`). libsystem_c.dylib's real Libc
 * consumers (ttyname.c, strsignal.c, localtime.c) bind to them there --
 * `nm -m libsystem_c.dylib` really shows
 * "(undefined) external __pthread_key_create (from libsystem_malloc)" --
 * so this unlocked code ran on every thread of every process on this
 * stack, over libsystem_malloc's own private g_key_destructors table.
 *
 * Fix uses C11/GCC atomic builtins rather than real os_unfair_lock ON
 * PURPOSE, and this is load-bearing, not a shortcut: libplatform's
 * os_unfair_lock resolves its owner identity through
 * _os_lock_owner_get_self() -> _pthread_getspecific_direct(TSD slot 3),
 * i.e. through the very TSD block this file is responsible for
 * bootstrapping. Taking an os_unfair_lock here would be a circular
 * dependency, and worse, a SILENT one: before the TSD block is populated
 * the owner reads back as 0 == OS_LOCK_NO_OWNER, so the lock's cmpxchg
 * 0->0 trivially "succeeds" for every caller at once and the lock
 * degrades to a no-op with no abort and no diagnostic. Compiler atomics
 * lower to bare LDXR/STXR here, depend on nothing, and are correct
 * before any bootstrap has run. ------------------------------------- */

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
/* REAL BUG found while root-causing the Eighteenth pass's real bash
 * SIGSEGV (docs/roadmap.md): this file gets compiled into libc_locale.a,
 * which every real consumer of the curated libc stack links; once
 * build_libpthread.sh's real pthread_tsd.c/pthread_main_thread_bootstrap.c
 * also exist and export the SAME 4 public POSIX names, a consumer that
 * links BOTH libc_locale.a and libpthread.a (real bash's build in
 * particular) got this file's NAIVE versions instead of the real ones,
 * with zero duplicate-symbol error -- ld64 resolves archives lazily and
 * left-to-right, and libc_locale.a's own internal Libc source (e.g.
 * strsignal.c) demands these symbols WHILE libc_locale.a itself is being
 * scanned, satisfying them locally before libpthread.a (already fully
 * scanned by then, or not yet reached, depending on link order -- order
 * turned out not to matter) ever gets a chance. Confirmed via disassembly:
 * real bash's linked `pthread_once` matched this file's
 * `once->sig != PTHREAD_ONCE_SIG_DONE` check byte-for-byte, not real
 * libpthread's `_os_once` cmpxchg/callout sequence -- and confirmed via a
 * standalone repro (pthread_once_deep_repro.c) that real libpthread's
 * implementation does NOT corrupt a caller's stack the way real bash's
 * (wrongly-linked) build did, isolating the bug to which implementation
 * gets linked, not to real pthread_once()/_os_once() logic itself.
 *
 * Simply removing this file's definitions (or stripping pthread_tsd_shim.o
 * from libc_locale.a's archive, the first fix attempted) breaks every
 * standalone smoketest tier (locale/stdio/gen/printf/malloc) that does
 * NOT separately link libpthread.a and still genuinely needs SOME
 * implementation of these 4 functions (real Libc source calls them
 * unconditionally) plus this file's OTHER, still-load-bearing exports
 * (pthread_tsd_shim_bootstrap, needed by real libmalloc's _get_zone; the
 * underscore-prefixed _pthread_once/_pthread_setspecific/etc FreeBSD-compat
 * aliases below, needed by real strsignal.c/localtime.c) -- confirmed by
 * hitting exactly that regression before switching approaches.
 *
 * Fixed correctly with __attribute__((weak)) on just these 4 functions:
 * when a consumer links ONLY libc_locale.a (no libpthread.a), these weak
 * definitions are the only ones available and behave exactly as before --
 * zero regression. When a consumer links BOTH (real bash), the linker
 * prefers libpthread.a's REAL strong definitions and silently discards
 * these weak ones -- real correctness restored, real bash gets the real,
 * QEMU-verified _os_once-based pthread_once instead of this naive stand-in.
 * The underscore-prefixed aliases below (_pthread_once et al) are
 * deliberately NOT marked weak -- libpthread.a's real pthread.c doesn't
 * export FreeBSD-compat aliases under those exact names at all, so they
 * remain this file's sole, still-necessary provider regardless of what
 * else is linked; since they simply call through to the (now correctly
 * overridable) public names above, they automatically forward to whichever
 * implementation actually won at link time.
 *
 * REAL BUG FOUND 2026-08-27 (build_simple_cmds.sh's 76-command tier,
 * `duplicate symbol '_pthread_key_init_np'` between
 * libc_locale_for_stdio.a(pthread_tsd_shim.o) and
 * libpthread.a(pthread_tsd.o)): this file actually exports FIVE names that
 * collide with libpthread.a's real pthread_tsd.c, not four --
 * pthread_key_init_np (below) was missed when __attribute__((weak)) was
 * added to the other four during the bash SIGSEGV fix above, because at
 * the time nothing linked both archives AND called it directly. Confirmed
 * via `nm -m` on the actual archive members: pthread_key_create/
 * setspecific/getspecific/once all show "weak external" while
 * pthread_key_init_np showed plain "external" (strong) -- a real, provable
 * asymmetry, not a guess. Same fix, same rationale, extended to the 5th
 * function. */
__attribute__((weak))
int pthread_key_create(unsigned long *key, void (*destructor)(void *)) {
    pthread_tsd_shim_bootstrap();
    for (unsigned long k = TSD_DYNAMIC_START; k < TSD_DYNAMIC_END; k++) {
        unsigned long *ptr = &g_key_destructors[k];
        unsigned long value = ~(unsigned long)destructor;
        /* DAR-227: was a plain `if (*ptr == 0) { *ptr = value; }` --
         * two threads reading 0 from the same slot both claimed it and
         * both returned the SAME key, silently aliasing two unrelated
         * subsystems' TSD. Real pthread_tsd.c claims its slot with
         * os_atomic_cmpxchg for exactly this reason; same contract here.
         * On failure the CAS reloads `expected`, so we simply move on to
         * the next slot -- no retry loop on the same index is needed. */
        unsigned long expected = 0;
        if (__atomic_compare_exchange_n(ptr, &expected, value, 0 /* strong */,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
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
__attribute__((weak))
int pthread_key_init_np(int key, void (*destructor)(void *)) {
    pthread_tsd_shim_bootstrap();
    if (key < TSD_FIRST || key >= TSD_DYNAMIC_START) {
        return 22 /* EINVAL */;
    }
    /* DAR-227: release store, not a plain store. `key` here is a
     * compile-time-constant reserved slot so there is no claim race to
     * lose, but pthread_setspecific() below reads this same word with an
     * acquire load to decide whether the key is live -- the pairing is
     * what makes that decision well-defined across threads. */
    __atomic_store_n(&g_key_destructors[key], ~(unsigned long)destructor,
                     __ATOMIC_RELEASE);
    return 0;
}

__attribute__((weak))
int pthread_setspecific(unsigned long key, const void *value) {
    pthread_tsd_shim_bootstrap();
    if (key < TSD_FIRST || key >= TSD_DYNAMIC_END) {
        return 22 /* EINVAL */;
    }
    /* DAR-227: acquire load pairs with the release stores in
     * pthread_key_create()/pthread_key_init_np() above, so a key observed
     * live here is guaranteed to have been fully published first. */
    int created = (__atomic_load_n(&g_key_destructors[key], __ATOMIC_ACQUIRE) != 0);
    if (key < TSD_DYNAMIC_START || created) {
        /* The TSD slot write itself needs no atomic: TPIDRRO_EL0 is
         * per-thread by construction, so slot storage is never shared. */
        _os_tsd_set_direct(key, (void *)value);
        if (key < TSD_DYNAMIC_START) {
            /* Matches real pthread_tsd.c: mark "claimed, no destructor",
             * not "never claimed" - see tsd_bootstrap.c's own self-review
             * note for why this specific value (all-ones, not zero)
             * matters. DAR-227: this IS shared (the destructor table is
             * process-wide), so it is a release store. Two threads racing
             * to claim the same reserved key both write the identical
             * ~0UL, so the CAS/plain-store distinction is immaterial --
             * only the publication ordering matters. */
            __atomic_store_n(&g_key_destructors[key], ~0UL, __ATOMIC_RELEASE);
        }
        return 0;
    }
    return 22 /* EINVAL */;
}

__attribute__((weak))
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
 *
 * SECOND REAL BUG, FIXED 2026-09-03 (DAR-227): the check and the set were
 * separate, unsynchronized operations, justified by "single-threaded
 * target: no race to guard against between the check and the call". Once
 * real threads existed that was a textbook double-init race, AND the
 * ordering was inverted from what the once contract requires -- the flag
 * was published BEFORE init_routine() ran, so a second thread arriving
 * mid-initialization saw "done" and proceeded to use state the first
 * thread had not finished building yet. That is precisely the failure
 * shape DAR-222 hit in libnotify's dispatch_once(notify_server_port).
 * "Run exactly once, and don't return until it HAS run" is the entire
 * contract of this primitive; a plain flag cannot express the second
 * half. Real algorithm below, mirroring libplatform's own _os_once gate. */
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
/* DAR-227: third state, "some thread is running init_routine() right
 * now". Must be distinct from both DONE and every plausible "not yet
 * run" value (0 and 0x30B1BCBA), same reasoning as DONE's choice above. */
#define PTHREAD_ONCE_SIG_INPROGRESS 2

__attribute__((weak))
int pthread_once(void *once_control, void (*init_routine)(void)) {
    struct pthread_once_shim *once = (struct pthread_once_shim *)once_control;
    for (;;) {
        long v = __atomic_load_n(&once->sig, __ATOMIC_ACQUIRE);
        if (v == PTHREAD_ONCE_SIG_DONE) {
            /* Acquire above pairs with the release store made by the
             * winner AFTER init_routine() returned, so everything that
             * routine published is visible to us before we return. */
            return 0;
        }
        if (v == PTHREAD_ONCE_SIG_INPROGRESS) {
            /* Another thread is initializing. Real libplatform parks on
             * __ulock_wait here; this file deliberately depends on
             * nothing (see the section header comment on why it cannot
             * reach for libplatform), so it spins with the ARM `yield`
             * hint instead. Correct but not power-optimal: `yield` is a
             * hint to the CPU, and the kernel's preemption timer still
             * runs, so the initializing thread is guaranteed to be
             * scheduled and finish even on this single-core-scheduled
             * target (architecture.md: secondary cores are parked). */
            __asm__ __volatile__("yield" ::: "memory");
            continue;
        }
        /* v is some "not yet run" value (0 or PTHREAD_ONCE_INIT's
         * 0x30B1BCBA). Try to become the initializer. */
        long expected = v;
        if (__atomic_compare_exchange_n(&once->sig, &expected,
                                        PTHREAD_ONCE_SIG_INPROGRESS,
                                        0 /* strong */,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            init_routine();
            /* Release: publish DONE only after every write made by
             * init_routine() is visible to whoever observes DONE. */
            __atomic_store_n(&once->sig, (long)PTHREAD_ONCE_SIG_DONE,
                             __ATOMIC_RELEASE);
            return 0;
        }
        /* Lost the CAS -- reload and re-dispatch on the new state. */
    }
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
