/* Real getentropy(2) raw syscall wrapper -- same hand-rolled `svc #0x80`
 * pattern as ifconfig_extra_syscalls.c's socket()/libresolv_socket_syscalls.c
 * (see those files' own header comments for why this project hand-rolls
 * these instead of running Apple's real Libsyscall stub generator for
 * every single syscall -- that generator IS used elsewhere in this
 * project, see build_simple_cmds.sh's create-syscalls.pl invocation, but
 * duplicating that whole batch machinery for one more syscall here would
 * be a much larger, less scoped change than this tier needs).
 *
 * Real number cross-checked against this project's own kernel source,
 * third_party/xnu-7195/bsd/kern/syscalls.master:
 *   #500 AUE_NULL  { int getentropy(void *buffer, size_t size); }
 * (real modern Darwin has no dedicated gethostname(2)/sethostname(2)
 * syscalls any more either -- #87/#88 are both `nosys` in this exact
 * kernel's syscalls.master, "{ old gethostname }"/"{ old sethostname }" --
 * real Apple's own gen/FreeBSD/gethostname.c gets it via sysctl()
 * instead, already a real working syscall in this project; see
 * build_libresolv.sh for where that real source is compiled in.)
 *
 * Needed because this tier compiles real, unmodified
 * third_party/Libc/gen/FreeBSD/arc4random.c for res_init.c's real
 * res_randomid() (DNS transaction ID generation) -- that file's own
 * `#if defined(__APPLE__) && !defined(VARIANT_STATIC)` real corecrypto/
 * ccrng.h path is compiled OUT under this project's standard
 * `-DVARIANT_STATIC` (already set for every tier, see build_libc_locale.sh),
 * which is the exact real condition Apple's own source reserves for
 * static-linked/pre-libSystem-init consumers that can't reach corecrypto
 * yet -- confirmed by reading the file, not guessed. Under that real
 * branch, arc4random.c's primary entropy source is a direct
 * getentropy(2) call (_my_getentropy()), with no other real dependency.
 */
typedef unsigned long size_t;

extern long cerror_nocancel(int err);

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

#define SYS_getentropy 500

int getentropy(void *buffer, size_t size) {
    return (int)syscall2(SYS_getentropy, (long)buffer, (long)size);
}
