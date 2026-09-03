/* Real, minimal raw syscall wrapper(s) for liblaunchd_core.a's own real
 * direct socket-syscall calls (core.o's socketgroup_setup_one, ipc.o's
 * ipc_server_init), found migrating build_launchd.sh off
 * libCoreFoundation.a for DAR-195.
 *
 * REAL PRE-EXISTING GAP, same class already found+fixed for launchctl
 * (launchctl_syscall_gaps.c, DAR-195 follow-on): build_corefoundation_dylib.sh
 * narrowed cf_socket_syscalls.o's own real bind()/getsockname()/setsockopt()
 * wrappers out of libCoreFoundation.dylib on the premise that
 * libsystem_kernel.dylib already exports them -- true for
 * recv/recvfrom/sendto/socket (confirmed via nm -gU), FALSE for bind/
 * getsockname/setsockopt (confirmed via nm -gU against this project's own
 * built libsystem_kernel.dylib: none of the three are present; only
 * libresolv.dylib -- deliberately NOT linked into launchd.macho, see
 * build_launchd.sh's own "libresolv.dylib/libcopyfile.dylib deliberately
 * NOT added" comment -- happens to export them). While CF was statically
 * embedded this went unnoticed because the archive's own cf_socket_syscalls.o
 * satisfied the reference directly; converting to a dylib is what surfaced
 * it (same shape as launchctl's own find).
 *
 * Real syscall number, cross-checked against this project's own kernel
 * source (third_party/xnu-7195/bsd/kern/syscalls.master:163):
 *   #104 AUE_BIND  bind(s, name, namelen)  NO_SYSCALL_STUB
 *
 * Same real "small, self-contained per-consumer svc #0x80 wrapper" pattern
 * as launchctl_syscall_gaps.c / cf_socket_syscalls.c. Only bind() is
 * provided here -- getsockname()/setsockopt() are the other two real gaps
 * this same narrow introduced, but liblaunchd_core.a's own real call sites
 * don't reach them (confirmed via the real link below reporting only
 * `_bind` undefined).
 */
extern long cerror_nocancel(int err);

static long syscall3(long num, long a0, long a1, long a2) {
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

int bind(int s, const void *name, unsigned int namelen) {
    return (int)syscall3(104, s, (long)name, (long)namelen);
}
