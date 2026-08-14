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

int __ulock_wait(uint32_t operation, void *addr, uint64_t value, uint32_t timeout) {
    long r = syscall4(515, (long)operation, (long)(long long)(unsigned long)addr, (long)value, (long)timeout);
    if (r < 0) {
        errno = (int)-r;
        return -1;
    }
    return (int)r;
}

int __ulock_wake(uint32_t operation, void *addr, uint64_t wake_value) {
    long r = syscall3(516, (long)operation, (long)(long long)(unsigned long)addr, (long)wake_value);
    if (r < 0) {
        errno = (int)-r;
        return -1;
    }
    return (int)r;
}
