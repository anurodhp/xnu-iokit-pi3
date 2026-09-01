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

/* Real pipe(2) raw syscall wrapper -- REMOVED 2026-08-31 (DAR-124
 * sweep). This was a real, correct, already-QEMU-verified fix for the
 * same real class as csops above (found blocking a real QEMU boot of
 * launchd: "symbol not found in flat namespace '_pipe'") -- but it gave
 * libxpc.dylib its own private copy of a symbol a broader DAR-124 audit
 * has since found needs to be a real, single, project-wide canonical
 * owner: libsystem_kernel.dylib (real Apple's actual home for pipe(2),
 * confirmed via the same syscalls.master AUE_PIPE entry cited below --
 * see build_libsystem_kernel.sh's own pipe_trap.o/pipe_forwarder.o
 * comment for the fuller citation, including why it needs BOTH the raw
 * __pipe.s trap AND a thin C forwarder on top). libxpc.dylib already
 * links libsystem_kernel.dylib as a sibling, so the reference this
 * file's csops() neighbor never needed now resolves there instead of
 * carrying a second, independently-compiled copy -- same real class as
 * this project's own now-closed duplicate-_kqueue finding (libdispatch.
 * dylib vs libsystem_kernel.dylib, same session). The real syscall-ABI
 * detail this implementation got right (kernel returns BOTH fds via
 * x0/x1, not a pointer argument, despite the public prototype being
 * `int pipe(int fildes[2])`) is preserved verbatim in libsystem_kernel.
 * dylib's own real Apple __pipe.s + the same forwarder shape. */
