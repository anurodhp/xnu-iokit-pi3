/* Real raw syscall implementation of __bsdthread_register, the exact
 * real symbol third_party/libpthread/src/imports_internal.h declares
 * (six real args -- threadstart, wqthread, pthsize, pthread_init_data,
 * pthread_init_data_size, dispatchqueue_offset; the real caller's
 * retval is the raw syscall's own return value, not a passed pointer --
 * confirmed by reading third_party/libpthread/src/pthread.c:1980's real
 * call site, not guessed) backed by real syscall 366 per
 * third_party/xnu-7195/bsd/kern/syscalls.master. NO_SYSCALL_STUB (same
 * class as __bsdthread_create/__ulock_wait, see
 * pthread_bsdthread_create_syscall.c/libplatform_syscalls.c) --
 * hand-written here with this project's established raw-syscall
 * carry-flag pattern.
 *
 * Needed once real_bsdthread_create (bsd/kern/bsd_pthread_workq.c,
 * 2026-08-25) started reading pthread_kern->proc_get_threadstart(p) for
 * the new thread's real entry PC instead of using bsdthread_create's own
 * `func` argument directly -- without a real bsdthread_register() call
 * first, proc_get_threadstart() returns NULL and proc_get_register(p)
 * reads 0, which real_bsdthread_create now correctly rejects with
 * EINVAL (see that function's own first check) rather than silently
 * jumping to a NULL pc. This file plus pthread_main_thread_bootstrap.c's
 * one-time registration call close that gap. wqthread/pthread_init_data
 * are passed as 0/NULL -- this project has no real workqueue-thread
 * consumer of bsdthread_create() and uses its own separate, already-real
 * platform_syscall-based TSD bootstrap instead of the kernel's
 * pthread_init_data-derived tsd_offset path (real_bsdthread_register
 * itself treats pthread_init_data==0 as a real, valid "skip that path"
 * case, not an error -- confirmed by reading it before relying on this).
 */

#include "internal.h"

static inline long syscall7(long num, long a0, long a1, long a2, long a3,
    long a4, long a5)
{
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x3 asm("x3") = a3;
    register long x4 asm("x4") = a4;
    register long x5 asm("x5") = a5;
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return -x0;
    }
    return x0;
}

/*
 * Parameter types below match third_party/libpthread/src/
 * imports_internal.h's real, already-in-scope (transitively, via
 * "internal.h") declaration of this exact symbol EXACTLY, not because
 * they're semantically accurate (they aren't -- see below) but because
 * a second, differently-shaped extern declaration/definition of the
 * same external symbol in the same translation unit is a hard C
 * "conflicting types" compile error (found by trying a more sensible-
 * looking signature first, not guessed). Real Apple's own declared
 * types for the last two parameters are stale/misleading: parameter 4
 * (`targetconc_ptr`, typed as a function pointer matching `threadstart`)
 * and parameter 5 (typed `int32_t *`) are what pthread.c's own real
 * call site (_pthread_bsdthread_init(), line ~1980) actually uses as a
 * plain `void *pthread_init_data` and a `size_t`/`uintptr_t`
 * `pthread_init_data_size` respectively -- tolerated there as an
 * implicit pointer/integer-to-pointer conversion warning, not a hard
 * error, and the real kernel-side real_bsdthread_register (bsd/kern/
 * bsd_pthread_workq.c) treats them exactly that way too (a real data
 * pointer + its byte size, not a second function pointer or an
 * out-param). This function's body casts all six through `long` for
 * the raw syscall regardless, so the declared parameter types only
 * matter for satisfying the compiler's redeclaration check, not for
 * this function's own real behavior.
 */
int
__bsdthread_register(
    void (*threadstart)(pthread_t, mach_port_t, void *(*)(void *), void *, size_t, unsigned int),
    void (*start_wqthread)(pthread_t, mach_port_t, void *, void *, int),
    int pthsize,
    void (*targetconc_ptr)(pthread_t, mach_port_t, void *(*)(void *), void *, size_t, unsigned int),
    int32_t *pthread_init_data_size,
    uint64_t dispatchqueue_offset)
{
    long r = syscall7(366, (long)threadstart, (long)start_wqthread, (long)pthsize,
            (long)targetconc_ptr, (long)pthread_init_data_size,
            (long)dispatchqueue_offset);
    if (r < 0) {
        errno = (int)-r;
        return -1;
    }
    return (int)r;
}
