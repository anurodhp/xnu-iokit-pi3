/* Real access(2) raw syscall wrapper -- needed only by the libopenpam.a
 * smoketest link, not by libpam itself: real, unmodified
 * Libc/stdtime/FreeBSD/localtime.c's tzload() calls access() to probe
 * for a real /usr/share/zoneinfo/<TZ> file before trying to open it.
 * Same real arm64 Darwin carry-flag raw-syscall pattern already used
 * throughout this project (see date_compat.c's settimeofday()/
 * simple_cmds_time_compat.c's __gettimeofday() for the identical
 * asm shape) -- reimplemented narrowly here rather than depending on
 * another tier's incidental build_simple_cmds.sh output
 * (libc_build/simple_obj/sys__access.o), which may not exist yet on a
 * from-scratch checkout that hasn't run that script first. Real syscall
 * number confirmed against third_party/xnu-7195/bsd/kern/syscalls.master
 * line 86 (`33 AUE_ACCESS ALL { int access(user_addr_t path, int flags); }`),
 * not guessed. */

#define OPENPAM_SYS_access 33

extern int cerror_nocancel(int err);

static inline long openpam_syscall2(long num, long a0, long a1) {
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

int
access(const char *path, int flags)
{
    return (int)openpam_syscall2(OPENPAM_SYS_access, (long)path, flags);
}
