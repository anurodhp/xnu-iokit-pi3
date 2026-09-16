/* Real raw BSD-syscall wrappers for __ulock_wait/__ulock_wake - the two
 * real syscalls real libplatform/src/os/lock.c's os_unfair_lock/os_once
 * implementation actually calls. Real numbers 515/516, cross-checked
 * directly against third_party/xnu-7195/bsd/kern/syscalls.master:
 *   515  ulock_wait  { int ulock_wait(uint32_t operation, void *addr, uint64_t value, uint32_t timeout) NO_SYSCALL_STUB; }
 *   516  ulock_wake  { int ulock_wake(uint32_t operation, void *addr, uint64_t wake_value) NO_SYSCALL_STUB; }
 * NO_SYSCALL_STUB means Apple's own Perl generator (already used for real
 * in build_libsyscall_smoketest.sh) deliberately produces no stub for
 * these; real Apple ships hand-written ones elsewhere in libpthread's own
 * internal sources (not this project's third_party/libpthread checkout's
 * public headers). Hand-written here with this project's own established
 * raw-syscall carry-flag pattern (same as libc_gen_syscalls.c/
 * libc_stdio_syscalls.c/tools/init_binary/init.c) rather than guessed.
 */

typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

static inline long syscall3(long num, long a0, long a1, long a2) {
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
        return -x0;
    }
    return x0;
}

static inline long syscall4(long num, long a0, long a1, long a2, long a3) {
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

extern int errno;

/* DAR-280: real Apple callers (libplatform's os/lock.c os_unfair_lock
 * implementation, libdispatch's src/shims/lock.c _dlock_wait/_dlock_wake -
 * everything os_unfair_lock-based, which is what real ulock-mode
 * pthread_mutex/pthread_cond sit on top of) OR this flag into `operation`
 * specifically to get the RAW kernel return value back - negative means
 * "-errno", zero-or-positive means success - rather than the traditional
 * "-1 and set the global errno" libc convention. They need this because
 * the raw value distinguishes e.g. -ENOENT ("no one was waiting to wake,
 * not an error") from a real failure, and they check `-ret` themselves;
 * collapsing everything to -1 makes every ENOENT (the ordinary, expected
 * case whenever a wake races ahead of the corresponding wait - extremely
 * common, not a bug) look identical to an unknown failure. This wrapper
 * used to always collapse to -1 regardless of the flag, which meant
 * __LIBPLATFORM_INTERNAL_CRASH__/DISPATCH_INTERNAL_CRASH fired on `-ret ==
 * 1` (i.e. `-(-1)`) for what was actually a completely normal wake-with-
 * no-waiter - a real, intermittent (timing-dependent on which side of the
 * race lost) BRK trap, root-caused via a real QEMU crash symbolicated
 * into libdispatch's _dlock_wake()/__dispatch_unfair_lock_wake() while
 * chasing DAR-280's pthread_cond_wait SIGSEGV (that SIGSEGV had a
 * separate, now-fixed root cause - a missing __attribute__((packed)) on
 * bsd_pthread_workq.c's registration-data struct - but fixing it and
 * switching mutex/cond sync onto the real ulock path is exactly what
 * started exercising this wrapper's ULF_NO_ERRNO gap for the first time).
 */
#define ULF_NO_ERRNO 0x01000000U

int __ulock_wait(uint32_t operation, void *addr, uint64_t value, uint32_t timeout) {
    long r = syscall4(515, (long)operation, (long)(long long)(unsigned long)addr, (long)value, (long)timeout);
    if (operation & ULF_NO_ERRNO) {
        return (int)r;
    }
    if (r < 0) {
        errno = (int)-r;
        return -1;
    }
    return (int)r;
}

int __ulock_wake(uint32_t operation, void *addr, uint64_t wake_value) {
    long r = syscall3(516, (long)operation, (long)(long long)(unsigned long)addr, (long)wake_value);
    if (operation & ULF_NO_ERRNO) {
        return (int)r;
    }
    if (r < 0) {
        errno = (int)-r;
        return -1;
    }
    return (int)r;
}
