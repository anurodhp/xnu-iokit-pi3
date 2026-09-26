/* Real, minimal, standard Itanium C++ ABI __cxa_atexit/__cxa_finalize --
 * NOT Apple-specific (same documented semantics in every real
 * implementation). Moved here from libdyld_compat_misc.c during
 * static-to-dylib migration step 5 (docs/daemon-roadmap.md): real
 * Apple's own __cxa_atexit genuinely lives in Libc (this project's
 * pinned third_party/Libc/stdlib/FreeBSD/atexit.c has a real, complete
 * implementation, but it pulls in Apple's internal namespacing macros/
 * os_alloc_once_private.h/Block_private.h machinery this port hasn't
 * assembled), not libc++abi -- and now that real libc++.dylib is its
 * own separate dylib (this migration's own step 5), it needs a real
 * provider for this symbol reachable via its libsystem_c.dylib
 * dependency, not a copy privately bundled inside libdyld.dylib only.
 *
 * REAL BUG FOUND AND FIXED 2026-09-03 (DAR-227): this table used to be
 * updated with plain, unsynchronized reads/writes on the stated grounds
 * that this target is "provably single-threaded" -- the same premise
 * pthread_tsd_shim.c's own DAR-227 fix already found and refuted (dead
 * since 2026-08-20, Phase 7.5). This table is process-wide and every
 * translation unit's static initializers can call __cxa_atexit during
 * real concurrent dyld/libinit activity, so two threads racing here used
 * to both read the same g_cxa_atexit_count, both write the SAME slot,
 * and both increment the count -- one registered destructor silently
 * overwritten, the count off by one from the entries actually stored.
 *
 * Fixed with C11/GCC atomic builtins, not os_unfair_lock, on purpose and
 * for the same reason as pthread_tsd_shim.c: __cxa_atexit can run before
 * this process's TSD block is bootstrapped (global C++ constructors run
 * very early), and os_unfair_lock's owner identity resolves through a
 * TSD slot pthread_tsd_shim.c itself provides -- so taking a real lock
 * here risks the identical silent-no-op circular dependency documented
 * there. atomic_fetch_add gives each caller a uniquely-owned slot index
 * with no lock at all. */
#define CXA_ATEXIT_MAX 64
struct cxa_atexit_entry {
    void (*func)(void *);
    void *arg;
    void *dso;
    int used;
};
static struct cxa_atexit_entry g_cxa_atexit_table[CXA_ATEXIT_MAX];
static int g_cxa_atexit_count = 0;

int __cxa_atexit(void (*func)(void *), void *arg, void *dso) {
    /* Each caller gets a unique index -- no two threads can ever claim
     * the same slot, so the writes below need no synchronization with
     * each other, only a release fence before publishing "used". */
    int i = __atomic_fetch_add(&g_cxa_atexit_count, 1, __ATOMIC_RELAXED);
    if (i >= CXA_ATEXIT_MAX) {
        return -1;
    }
    g_cxa_atexit_table[i].func = func;
    g_cxa_atexit_table[i].arg = arg;
    g_cxa_atexit_table[i].dso = dso;
    /* Release: __cxa_finalize's acquire load of `used` is guaranteed to
     * see func/arg/dso fully written first. */
    __atomic_store_n(&g_cxa_atexit_table[i].used, 1, __ATOMIC_RELEASE);
    return 0;
}

void __cxa_finalize(void *dso) {
    /* Acquire: see every slot published by a fetch_add that happened
     * before this load, per the release-store pairing above. */
    int count = __atomic_load_n(&g_cxa_atexit_count, __ATOMIC_ACQUIRE);
    if (count > CXA_ATEXIT_MAX) {
        count = CXA_ATEXIT_MAX;
    }
    for (int i = count - 1; i >= 0; i--) {
        int expected = 1;
        if (g_cxa_atexit_table[i].dso == dso || dso == 0) {
            /* CAS-claim the entry so two concurrent __cxa_finalize calls
             * (e.g. one per unloading DSO) can never both run the same
             * destructor. Acquire on success: func/arg are stable by the
             * time we read them below, per the store's own release. */
            if (__atomic_compare_exchange_n(&g_cxa_atexit_table[i].used, &expected, 0,
                                            0 /* strong */, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
                g_cxa_atexit_table[i].func(g_cxa_atexit_table[i].arg);
            }
        }
    }
}

/* atexit(3) (DAR-383, 2026-09-26: libXau's AuFileName.c:80 is the first
 * dylib in this tree to call it; before this no dylib exported atexit at
 * all -- launchd.macho carries its own private copy).
 *
 * Real Apple semantics, third_party/Libc/stdlib/FreeBSD/atexit.c:146-166:
 * atexit() registers an ATEXIT_FN_STD entry with fn_arg = NULL and, on
 * TARGET_OS_IPHONE (this SDK), fn_dso = NULL (the dladdr() branch at
 * :156-161 is compiled out); __cxa_finalize() runs it as
 * fn->fn_ptr.std_func() (:280-281). This table stores
 * void (*)(void *) + arg, so the std function travels as the arg of a
 * trampoline that calls it with no arguments, giving exactly that call.
 *
 * Not the same matching as Libc: this table's __cxa_finalize above runs a
 * dso-0 entry only for __cxa_finalize(0) (its test is `entry.dso == dso
 * || dso == 0`). Libc's __cxa_finalize_ranges() (atexit.c:246-257) also
 * runs an ATEXIT_FN_STD entry on an image unload when the function's
 * address lies inside the unloaded image. Nothing here unloads images or
 * calls __cxa_finalize(dso), so the difference is not reachable today.
 *
 * KNOWN, PRE-EXISTING GAP (not introduced here): this port's exit()
 * (libc_stdio_process_shims.c) calls only __cleanup (stdio flush) and
 * then the raw exit trap -- it never calls __cxa_finalize(NULL), so no
 * atexit()/__cxa_atexit() handler runs at process exit today, exactly as
 * for every C++ static destructor already registered through this file.
 * Registration itself is real and correct. */
static void atexit_std_trampoline(void *func) {
    ((void (*)(void))func)();
}

int atexit(void (*func)(void)) {
    return __cxa_atexit(atexit_std_trampoline, (void *)func, 0);
}
