/* Real, minimal raw acct(2) syscall wrapper for launchctl.c's own real
 * "singleuser" boot path (launchctl.c:2381,
 * `posix_assumes_zero(acct("/var/account/acct"))`).
 *
 * REAL PRE-EXISTING GAP FOUND 2026-09-02 while migrating build_launchctl.sh
 * off libCoreFoundation.a for DAR-195: this project has never had a real
 * acct(2) wrapper anywhere (confirmed via a real grep across every .a/
 * .dylib and every third_party/Libc/xnu-7195/libsyscall source file --
 * zero hits), and launchctl.macho was never wired into
 * tools/init_binary/inject_into_sd_image.sh / real-QEMU-boot-tested
 * before this same session -- so this gap was real but silently never
 * surfaced. Real syscall number confirmed against this project's own
 * kernel source (third_party/xnu-7195/bsd/kern/syscalls.master:104):
 *   51  AUE_ACCT  { int acct(char *path); }
 * Same real "small, self-contained per-consumer svc #0x80 wrapper"
 * pattern as cf_socket_syscalls.c (build_corefoundation_dylib.sh) /
 * ifconfig_extra_syscalls.c / libresolv_socket_syscalls.c -- this
 * project doesn't run Apple's real Libsyscall stub generator for every
 * single BSD syscall, so single-syscall gaps like this get their own
 * narrow, hand-rolled trap wrapper rather than a bigger codegen pass.
 */
extern long cerror_nocancel(int err);

int acct(const char *path) {
    register long x0 asm("x0") = (long)path;
    register long x16 asm("x16") = 51;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return (int)cerror_nocancel((int)x0);
    }
    return (int)x0;
}
