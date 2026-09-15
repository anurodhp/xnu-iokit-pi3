/* Real raw-syscall stat64_extended/lstat64_extended/fstat64_extended
 * (bsd/kern/syscalls.master #341/#342/#343, all NO_SYSCALL_STUB - no
 * hand-written libsyscall counterpart exists on real Apple either,
 * same class of gap build_corefoundation.sh's own header already
 * documents) that real Libc/sys/statx_np.c's statx1() (already,
 * unmodified, compiled into ls) calls as __stat64_extended/
 * __lstat64_extended/__fstat64_extended. Same real carry-flag ABI /
 * cerror_nocancel() convention as cp_mv_syscalls.c's lchown/sigaction/
 * setattrlist precedent (DAR-148).
 *
 * Signature per syscalls.master: (path|fd, ub, xsecurity,
 * xsecurity_size) - xsecurity is the real kauth_filesec_t output
 * buffer, xsecurity_size is a real in/out size_t* (buffer size in,
 * bytes actually needed out - statx1()'s own realloc-and-retry loop
 * already handles the "buffer too small" case this returns).
 */
#include <sys/types.h>
#include <sys/stat.h>

extern long cerror_nocancel(int err);

static inline long ls_acl_syscall4(long num, long a0, long a1, long a2, long a3) {
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
    if (is_error) return cerror_nocancel((int)x0);
    return x0;
}

int __stat64_extended(const char *path, struct stat *ub, void *xsecurity, size_t *xsecurity_size) {
    return (int)ls_acl_syscall4(341, (long)path, (long)ub, (long)xsecurity, (long)xsecurity_size);
}

int __lstat64_extended(const char *path, struct stat *ub, void *xsecurity, size_t *xsecurity_size) {
    return (int)ls_acl_syscall4(342, (long)path, (long)ub, (long)xsecurity, (long)xsecurity_size);
}

int __fstat64_extended(int fd, struct stat *ub, void *xsecurity, size_t *xsecurity_size) {
    return (int)ls_acl_syscall4(343, (long)fd, (long)ub, (long)xsecurity, (long)xsecurity_size);
}
