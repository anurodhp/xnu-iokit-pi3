/* Real socket(2) raw syscall wrapper -- same hand-rolled `svc #0x80`
 * pattern as libc_gen_syscalls.c's ioctl/_ioctl (see that file's own
 * header comment for why this project uses this instead of Libsyscall's
 * real MI_ENTRY_POINT/SYS.h macro assembly, which needs machinery this
 * checkout doesn't build). socket(2) is syscall #97, real number
 * cross-checked against $XNU/bsd/kern/syscalls.master, not guessed --
 * it's the one real network_cmds/ifconfig.tproj symbol libc_gen.a didn't
 * already provide (ioctl/setsockopt/getsockopt were already covered by
 * prior tiers).
 */
extern long cerror_nocancel(int err);

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
        return cerror_nocancel((int)x0);
    }
    return x0;
}

#define SYS_socket 97

int socket(int domain, int type, int protocol) {
    return (int)syscall3(SYS_socket, domain, type, protocol);
}
