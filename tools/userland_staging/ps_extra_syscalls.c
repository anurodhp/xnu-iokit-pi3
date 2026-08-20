/* Real raw syscall wrappers ps needs that no prior Phase 4 tier hand-
 * wired yet -- same carry-flag/cerror_nocancel() ABI convention as every
 * other raw syscall in this project (see libc_gen_syscalls.c's header
 * comment for the full history of why that convention matters). Numbers
 * cross-checked directly against third_party/xnu-7195/bsd/kern/
 * syscalls.master, not guessed.
 *
 * access/getuid are plain, non-NO_SYSCALL_STUB entries (Apple's real
 * generator would produce trivial stubs for these too -- this project
 * just hasn't run that generator over the full syscall table, only
 * specific ones per build_libsyscall_smoketest.sh). __gettimeofday is a
 * real NO_SYSCALL_STUB entry -- Apple hand-writes it in libsyscall/
 * custom/ too; this project's checkout doesn't (confirmed by find), same
 * gap class as libc_gen_syscalls.c's own hand-written wrappers (which is
 * also now where this file's own former ioctl/_ioctl/_writev/_select
 * copies moved to, see that file's header comment -- ps and bash both
 * link libc_gen.a now, so one shared copy avoids a duplicate symbol).
 *
 * __gettimeofday is the raw syscall real third_party/Libc/sys/
 * gettimeofday.c (compiled for real into ps.macho, see build_ps.sh)
 * falls back to if the commpage fast path (__commpage_gettimeofday,
 * already real and proven working on this target -- see
 * build_libunwind.sh's real mach_absolute_time.s) reports itself
 * unavailable. Real signature per syscalls.master line 185: int
 * gettimeofday(struct timeval *tp, struct timezone *tzp, uint64_t
 * *mach_absolute_time) -- gettimeofday.c's own extern declares only 2
 * args for __gettimeofday, so the 3rd (mach_absolute_time out-param) is
 * passed as NULL, matching real Apple's own libsyscall/custom/
 * __gettimeofday.s convention (checked: that real file, when the
 * generator produces it, always passes a real address there, but this
 * project's fallback path only needs a valid wall-clock time, not the
 * mach_absolute_time cross-check pair, so NULL is safe here -- the
 * kernel's real gettimeofday implementation tolerates a NULL 3rd arg,
 * confirmed by reading bsd/kern/kern_time.c's real gettimeofday_pf()). */
#define SYS_access 33
#define SYS_getuid 24
#define SYS_gettimeofday 116

extern long cerror_nocancel(int err);

static inline long syscall1(long num, long a0) {
    register long x0 asm("x0") = a0;
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return cerror_nocancel((int)x0);
    }
    return x0;
}

static inline long syscall0(long num) {
    register long x0 asm("x0");
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "=r"(x0), "=r"(is_error)
                 : "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return cerror_nocancel((int)x0);
    }
    return x0;
}

static inline long syscall2(long num, long a0, long a1) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return cerror_nocancel((int)x0);
    }
    return x0;
}

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

int access(const char *path, int flags) {
    return (int)syscall2(SYS_access, (long)path, flags);
}

unsigned int getuid(void) {
    return (unsigned int)syscall0(SYS_getuid);
}

/* ioctl/_ioctl/_writev/_select used to be defined here (real termios.c
 * needs all four) -- MOVED to the shared libc_gen_syscalls.c
 * (build_libc_gen.sh) 2026-08-19 once bash needed the same real
 * termios.c too (see that file's own header comment for the full
 * writeup). Both ps and bash link libc_gen.a, so a single shared copy
 * avoids a duplicate-symbol clash; removed here rather than shadow it. */

struct timeval;
int __gettimeofday(void *tp, void *tzp) {
    (void)tzp;
    return (int)syscall3(SYS_gettimeofday, (long)tp, 0, 0);
}
