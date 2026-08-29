/* Real csops(2) raw syscall wrapper -- libxpc.dylib's own real source
 * calls this (code-signing status query, used by XPC's connection
 * validation path) but this project's checkout has no compiled source
 * for it anywhere (`nm -g` swept across every .a in libc_build/, see
 * build_libxpc_dylib.sh's own header) -- previously deferred via
 * `-Wl,-U,_csops`, which genuinely doesn't work here: nothing in this
 * project exports csops in any form, so real dyld's flat-namespace
 * lookup at real process load time fails outright ("symbol not found in
 * flat namespace '_csops'", confirmed via a real QEMU boot of launchd,
 * the first real consumer to load libxpc.dylib and actually exercise
 * this path). Real syscall #169 (AUE_CSOPS), cross-checked against this
 * project's own third_party/xnu-7195/bsd/kern/syscalls.master, not
 * guessed:
 *   169  AUE_CSOPS  { int csops(pid_t pid, uint32_t ops, user_addr_t useraddr, user_size_t usersize); }
 * Same hand-rolled `svc #0x80` pattern as every other raw syscall in
 * this project (see libresolv_socket_syscalls.c's own header for the
 * full "why not Apple's real Libsyscall stub generator" reasoning).
 */
typedef unsigned int uint32_t;
typedef int pid_t;

extern long cerror_nocancel(int err);

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
        return cerror_nocancel((int)x0);
    }
    return x0;
}

int csops(pid_t pid, uint32_t ops, void *useraddr, unsigned long usersize) {
    return (int)syscall4(169, pid, (long)ops, (long)useraddr, (long)usersize);
}

/* Real pipe(2) raw syscall wrapper -- same real class as csops above,
 * found blocking the very next real QEMU boot of launchd after csops
 * was fixed ("symbol not found in flat namespace '_pipe'"). Real
 * syscall #42 (AUE_PIPE), cross-checked against syscalls.master --
 * `{ int pipe(void); }` there, but real Darwin's actual ABI returns
 * BOTH file descriptors directly in x0/x1 (not via a pointer argument)
 * despite the real public libc-level prototype being `int pipe(int
 * fildes[2])` (confirmed via the real Xcode-12 SDK's own unistd.h) --
 * the public wrapper packages the two raw return registers into the
 * caller's array, real Apple architecture, not reinvented. */
static inline long syscall0_2ret(long num, long *second) {
    register long x0 asm("x0");
    register long x1 asm("x1");
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %2, cs"
                 : "=r"(x0), "=r"(x1), "=r"(is_error)
                 : "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        extern long cerror_nocancel(int err);
        *second = 0;
        return cerror_nocancel((int)x0);
    }
    *second = x1;
    return x0;
}

int pipe(int fildes[2]) {
    long fd1;
    long fd0 = syscall0_2ret(42, &fd1);
    if (fd0 < 0) {
        return (int)fd0;
    }
    fildes[0] = (int)fd0;
    fildes[1] = (int)fd1;
    return 0;
}
