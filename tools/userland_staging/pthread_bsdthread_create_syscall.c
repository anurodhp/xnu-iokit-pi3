/* Real raw syscall implementation of __bsdthread_create, the exact real
 * symbol third_party/libpthread/src/imports_internal.h declares:
 *   extern pthread_t __bsdthread_create(void *(*func)(void *),
 *       void *func_arg, void *stack, pthread_t thread, unsigned int flags);
 * backed by real syscall 360 per
 * third_party/xnu-7195/bsd/kern/syscalls.master:
 *   360  bsdthread_create { user_addr_t bsdthread_create(user_addr_t func,
 *        user_addr_t func_arg, user_addr_t stack, user_addr_t pthread,
 *        uint32_t flags) NO_SYSCALL_STUB; }
 * NO_SYSCALL_STUB (same as __ulock_wait/__ulock_wake, see
 * libplatform_syscalls.c) - hand-written here with this project's
 * established raw-syscall carry-flag pattern. Real error convention
 * (pthread.c:1270, the only real caller): returns (pthread_t)-1 and
 * sets errno on failure - matched here, not invented. See
 * pthread_main_thread_bootstrap.c's own header for why calling this for
 * real (rather than skipping pthread_create entirely) is the honest
 * choice: this project's kernel-side pthread stand-in
 * (bsd_pthread_stub.c) unconditionally returns ENOTSUP for it.
 */

#include "internal.h"

static inline long syscall5(long num, long a0, long a1, long a2, long a3, long a4) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x3 asm("x3") = a3;
    register long x4 asm("x4") = a4;
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return -x0;
    }
    return x0;
}

pthread_t
__bsdthread_create(void *(*func)(void *), void *func_arg, void *stack,
		pthread_t thread, unsigned int flags)
{
	long r = syscall5(360, (long)(void *)func, (long)func_arg,
			(long)stack, (long)thread, (long)flags);
	if (r < 0) {
		errno = (int)-r;
		return (pthread_t)-1;
	}
	return (pthread_t)r;
}
