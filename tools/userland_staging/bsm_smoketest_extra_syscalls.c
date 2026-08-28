/* Smoketest-only compat: real getgid(2)/getuid(2) (syscalls #47/#24,
 * cross-checked against third_party/xnu-7195/bsd/kern/syscalls.master),
 * needed because bsm_smoketest.macho's link pulls in bsm_token.o's real
 * au_to_me() (a real, unmodified BSM-13 function bundled in the same
 * object file as au_to_subject32()/au_to_return32()/au_to_text() --
 * static `ar` linking is per-object-file, so au_to_me()'s own real
 * getgid()/geteuid()/getuid()/gettimeofday() references come along even
 * though this smoketest never calls au_to_me() itself). geteuid/
 * gettimeofday are already real and available from libremovefile.a
 * (linked below); getgid/getuid were missing from every archive already
 * on this smoketest's minimal link line. Deliberately NOT added to
 * libbsm.a itself or to any
 * shared archive -- this is smoketest link scaffolding only, same
 * "don't touch the shared, committed archive" caution this project's
 * other duplicate-symbol fixes already follow (see build_llvm_libcxx.sh's
 * own header comment for the precedent). Same raw carry-flag svc pattern
 * as bsm_syscalls.c/notify_fileport_syscalls.c/getty_syscalls.c.
 */

extern long cerror_nocancel(int err);

static inline long bsm_smoketest_syscall0(long num) {
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

#define SYS_bsm_smoketest_getgid 47
#define SYS_bsm_smoketest_getuid 24

int getgid(void) {
    return (int)bsm_smoketest_syscall0(SYS_bsm_smoketest_getgid);
}

int getuid(void) {
    return (int)bsm_smoketest_syscall0(SYS_bsm_smoketest_getuid);
}
