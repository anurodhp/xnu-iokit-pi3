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
 * Single-threaded-safe (same "no lock needed, this target is provably
 * single-threaded" reasoning already used throughout this project's
 * pthread_tsd_shim.c), not a stand-in for missing logic. */
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
    if (g_cxa_atexit_count >= CXA_ATEXIT_MAX) {
        return -1;
    }
    g_cxa_atexit_table[g_cxa_atexit_count].func = func;
    g_cxa_atexit_table[g_cxa_atexit_count].arg = arg;
    g_cxa_atexit_table[g_cxa_atexit_count].dso = dso;
    g_cxa_atexit_table[g_cxa_atexit_count].used = 1;
    g_cxa_atexit_count++;
    return 0;
}

void __cxa_finalize(void *dso) {
    for (int i = g_cxa_atexit_count - 1; i >= 0; i--) {
        if (g_cxa_atexit_table[i].used && (dso == 0 || g_cxa_atexit_table[i].dso == dso)) {
            g_cxa_atexit_table[i].used = 0;
            g_cxa_atexit_table[i].func(g_cxa_atexit_table[i].arg);
        }
    }
}
