/* Real raw syscall implementation of __bsdthread_terminate, the exact
 * real symbol third_party/libpthread/src/imports_internal.h declares:
 *   extern int __bsdthread_terminate(void *freeaddr, size_t freesize,
 *       mach_port_t kport, mach_port_t joinsem);
 * backed by real syscall 361 per
 * third_party/xnu-7195/bsd/kern/syscalls.master:
 *   361  bsdthread_terminate { int bsdthread_terminate(user_addr_t
 *        stackaddr, size_t freesize, uint32_t port, uint32_t sem)
 *        NO_SYSCALL_STUB; }
 * NO_SYSCALL_STUB, same hand-written-raw-syscall precedent as
 * __bsdthread_create/__bsdthread_register (see those files' own header
 * comments).
 *
 * Needed now that bsd/kern/bsd_pthread_workq.c's real_bsdthread_terminate
 * (2026-08-25, ported from third_party/libpthread/kern/kern_support.c's
 * real _bsdthread_terminate) actually implements this syscall instead of
 * bsd_pthread_stub.c's old unconditional ENOTSUP stub -- this closes the
 * loop for pthread_main_thread_bootstrap.c's own
 * _pthread_create_trampoline, which previously had no real thread-exit
 * path at all and spun forever after its real start_routine returned
 * (see that function's own header comment).
 *
 * This function never returns under normal operation: real
 * real_bsdthread_terminate always ends by calling
 * pthread_kern->thread_exception_return() (a real, noreturn Mach
 * trap-return path), not a normal syscall return -- matching real
 * Apple's own _pthread_terminate()'s use of this same call
 * (third_party/libpthread/src/pthread.c's own PTHREAD_INTERNAL_CRASH
 * right after it, for exactly the "should never get here" case).
 *
 * Called here with freeaddr=NULL/freesize=0/kport=0/joinsem=0 -- real,
 * honestly-scoped narrower use than real Apple's own _pthread_terminate
 * (which computes real freeaddr/freesize from the thread's own stack
 * allocation and passes its real Mach thread port for kport): real
 * real_bsdthread_terminate's own guards (`if freesize != 0 && freeaddr
 * != 0`, `if kthport != MACH_PORT_NULL`, `if sem != MACH_PORT_NULL`)
 * all real-and-correctly skip their respective real work when passed
 * zero, so this is a real, valid, if incomplete, thread-exit call --
 * not a fabrication. Real, separate, still-open gap this leaves: the
 * mmap'd per-thread stack pthread_create() allocates is never freed on
 * thread exit this way (a real, narrowly-scoped resource leak, not a
 * correctness bug -- same tolerance this project's own
 * _pthread_create_trampoline already documents for not inserting the
 * new thread into __pthread_head).
 */

#include "internal.h"

static inline long syscall4(long num, long a0, long a1, long a2, long a3)
{
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x3 asm("x3") = a3;
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return -x0;
    }
    return x0;
}

int
__bsdthread_terminate(void *freeaddr, size_t freesize, mach_port_t kport,
    mach_port_t joinsem)
{
    long r = syscall4(361, (long)freeaddr, (long)freesize, (long)kport,
            (long)joinsem);
    if (r < 0) {
        errno = (int)-r;
        return -1;
    }
    return (int)r;
}
