/*
 * creat(), wait() and sigsuspend(): three libc entry points xdm calls
 * (xdm/error.c InitErrorLog, xdm/session.c, xdm/dm.c WaitForChild) that no
 * libc-owning dylib in this tree exports today (nm -gU over
 * libc_build/system: creat only leaks out of libsystem_notify.dylib,
 * sigsuspend out of libdispatch.dylib, wait from nothing). They are
 * compiled into xdm itself, the way build_su.sh compiles Libc's crypt.c,
 * until libsystem_c/libsystem_kernel export them; being strong local
 * definitions in the executable they cannot conflict with a later export.
 * Each body is the real Apple/xnu one, reduced to its public-API form:
 *
 *  creat:      third_party/Libc/compat-43/FreeBSD/creat.c, __creat():
 *              open(path, O_WRONLY|O_CREAT|O_TRUNC, mode).
 *  wait:       third_party/Libc/gen/FreeBSD/wait.c, __wait():
 *              wait4(WAIT_ANY, istat, 0, NULL) (libsystem_c exports wait4).
 *  sigsuspend: raw BSD syscall #111 (third_party/xnu-7195/bsd/kern/
 *              syscalls.master:175, NO_SYSCALL_STUB), the same carry-flag
 *              trap tools/userland_staging/dispatch_gap_closures3.c uses
 *              for libdispatch.dylib's copy.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

int creat(const char *path, mode_t mode)
{
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

pid_t wait(int *istat)
{
    return wait4(WAIT_ANY, istat, 0, (struct rusage *)0);
}

int sigsuspend(const sigset_t *mask)
{
    register long x0 asm("x0") = (long)*mask;
    register long x16 asm("x16") = 111;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        errno = (int)x0;
        return -1;
    }
    return (int)x0;
}
