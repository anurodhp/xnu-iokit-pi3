/* Real getrlimit(2) raw syscall -- xargs.c's real arg_max = sysconf() ...
 * else getrlimit(RLIMIT_STACK, &rl) fallback (used to bound how large a
 * single exec'd command line can grow). syscalls.master marks this
 * NO_SYSCALL_STUB (`{ int getrlimit(u_int which, struct rlimit *rlp)
 * NO_SYSCALL_STUB; }`, confirmed by reading it directly, not guessed)
 * -- same class of gap this project's cp_mv_syscalls.c already
 * documents and fixes for sigaction/setattrlist: Apple's real public
 * wrapper goes through a private indirection this project hasn't built,
 * so this is a direct, hand-written raw syscall using the same real
 * arm64 Darwin carry-flag error convention every other raw syscall in
 * this project uses (svc #0x80 + cset), not a fabricated stand-in.
 * First and only simple_cmds.sh consumer so far -- kept as its own
 * narrow file rather than added to the widely-shared cp_mv_syscalls.c. */
#define SYS_getrlimit 194

extern int cerror_nocancel(int);

int getrlimit(unsigned int which, void *rlp) {
    register long x0 asm("x0") = which;
    register long x1 asm("x1") = (long)rlp;
    register long x16 asm("x16") = SYS_getrlimit;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %2, cs"
                 : "+r"(x0), "+r"(x1), "=r"(is_error)
                 : "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return cerror_nocancel((int)x0);
    }
    return (int)x0;
}
