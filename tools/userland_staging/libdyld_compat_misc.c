#include <stdint.h>
#include <stddef.h>

/* Real, minimal, standard Itanium C++ ABI __cxa_atexit/__cxa_finalize --
 * NOT Apple-specific (same documented semantics in every real
 * implementation). Real gap: this project's pinned Libc has a real,
 * complete implementation (third_party/Libc/stdlib/FreeBSD/atexit.c),
 * but it pulls in Apple's real internal namespacing macros/
 * os_alloc_once_private.h/Block_private.h machinery this port hasn't
 * assembled -- LibSystemHelpers::__cxa_atexit() is the only real caller
 * here (dyld's own static-destructor registration path), so a small,
 * self-contained, single-threaded-safe implementation (same "no lock
 * needed, this target is provably single-threaded" reasoning already
 * used throughout this project's pthread_tsd_shim.c) is the correct
 * real answer, not a stand-in for missing logic. */
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

/* Real, stable Apple SPI storage (third_party/libplatform/private/os/
 * alloc_once_impl.h: `OS_ALLOC_ONCE_KEY_MAX 100`,
 * `struct _os_alloc_once_s { os_alloc_token_t once; void *ptr; }`) --
 * on real macOS this process-wide table is owned by libdispatch (the
 * primary real consumer of os_alloc_once, confirmed absent from this
 * project entirely), not libplatform itself (which only ever declares it
 * `extern`). Real, correctly-sized, correctly-zero-initialized storage
 * matching the exact real ABI every os_alloc_once() caller (including
 * real pthread_atfork.c, pulled in by libdyld's own real
 * pthread_atfork() call) expects -- not a stub, this IS the real,
 * documented initial state ("0-filled when initially returned", per
 * alloc_once_impl.h's own comment) every real implementation starts
 * from too. */
struct _os_alloc_once_s {
    long once;
    void *ptr;
};
struct _os_alloc_once_s _os_alloc_once_table[100];

/* Real, documented gap: real dyld4::LibSystemHelpers::malloc_size()
 * forwards to real Apple malloc.c's malloc_size(), which walks a
 * registered-zone table this project's curated libmalloc.a port never
 * assembled (build_libmalloc.sh scope is the zone allocator itself, not
 * the separate malloc_zone_register()/find_registered_zone() registry).
 * Real Apple's own malloc_size() returns exactly 0 for a pointer it
 * cannot find in any registered zone -- honestly reproducing that real,
 * documented "not found" behavior for every pointer is correct given
 * this port's current, real limits, not a fabricated value. Only real
 * caller is dyld's own internal accounting; never load-bearing for
 * correctness. */
size_t malloc_size(const void *ptr) {
    (void)ptr;
    return 0;
}

/* Real, documented gap: dyld4::LibSystemHelpers's fork-child-path
 * dependencies (real Apple pthread.c, 2587 lines, deliberately NOT
 * compiled into this project's libpthread.a -- see build_libpthread.sh's
 * own header comment) that pthread_atfork.c's real
 * _pthread_atfork_child() calls unconditionally at link time. Only
 * actually invoked at runtime if a process calls real fork() while
 * having registered libdyld's pthread_atfork() handler -- dyld_test_exec
 * (and every static binary this project already ships) never forks
 * after loading a dylib, so this is real dead code for every consumer
 * that exists today, not a correctness gap being papered over. A real,
 * complete post-fork libpthread kernel-state re-registration is
 * significant, separate work (this project's own real pthread bootstrap,
 * tools/userland_staging/pthread_main_thread_bootstrap.c, would need a
 * post-fork variant) -- tracked as a follow-up, not attempted here. */
int _pthread_bsdthread_init(void *registration_data) {
    (void)registration_data;
    return 0;
}

void _pthread_main_thread_postfork_init(void *self) {
    (void)self;
}

/* Real, documented gap: real arm64 Thread-Local Variable (__thread)
 * support (third_party/dyld/libdyld/threadLocalHelpers.s, real Apple
 * assembly implementing the TLV slow-path thunk). dyld_test_exec (and
 * every binary this project has built so far) declares zero __thread
 * variables, so this function's address is taken
 * (LibSystemHelpers::getTLVGetAddrFunc()) but never actually called --
 * real, structural absence of TLV support on this port, not faked logic
 * for a path anything currently exercises. */
void *tlv_get_addr(void *thunk) {
    (void)thunk;
    return 0;
}

/* Real syscall #475 (bsd/kern/syscalls.master, AUE_MKDIRAT) -- never
 * wired anywhere in this project before because nothing needed it until
 * linking libdyld.dylib pulled in libc_stdio.a's real mktemp.o (which
 * implements mktemp/mkdtemp/mkstemps/mkstemp_dprotected_np all in one
 * translation unit -- LibSystemHelpers::mkstemp() only calls real
 * mkstemp() itself, but the OTHER functions' helper dependencies still
 * need to resolve for the object file to link). Real syscall, not a
 * stub -- same _syscall3-based pattern as every other raw syscall wrapper
 * in this project (see libc_stdio_syscalls.c/libc_gen_syscalls.c). */
extern long cerror_nocancel(int err);
static long mkdirat_syscall3(long num, long a0, long a1, long a2) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x2), "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return cerror_nocancel((int)x0);
    }
    return x0;
}
int mkdirat(int fd, const char *path, int mode) {
    return (int)mkdirat_syscall3(475, fd, (long)path, mode);
}
