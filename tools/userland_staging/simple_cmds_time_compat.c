/* Real Libc/sys/gettimeofday.c calls a private `__gettimeofday` raw
 * syscall wrapper with only 2 args (tp, tzp), but the real kernel
 * syscall (syscalls.master: `gettimeofday(struct timeval *tp, struct
 * timezone *tzp, uint64_t *mach_absolute_time)`) takes 3 -- the real
 * generated .s stub would leave the 3rd register whatever garbage the
 * caller's own stack/registers happened to hold, since gettimeofday.c
 * itself only ever passes 2. tools/userland_staging/ps_extra_syscalls.c
 * already solved this exact problem (QEMU-verified working) by hand-
 * writing the raw syscall trap instead of using the generated stub,
 * explicitly passing 0 (NULL) for the 3rd arg -- same real fix (the
 * real arm64 Darwin carry-flag error convention + `cerror_nocancel`
 * dispatch, copied from that file directly, not reinvented), reused
 * here since this project's own real kern_time.c gettimeofday_pf()
 * genuinely tolerates a NULL 3rd arg (confirmed by that file's own
 * comment there, not re-verified here). */
#define SYS_gettimeofday 116

extern int cerror_nocancel(int err);

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

int __gettimeofday(void *tp, void *tzp) {
    (void)tzp;
    return (int)syscall3(SYS_gettimeofday, (long)tp, 0, 0);
}
