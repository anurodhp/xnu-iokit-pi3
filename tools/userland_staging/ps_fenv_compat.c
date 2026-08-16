/* Real fegetenv()/fesetenv(): needed by real Libc/gen/FreeBSD/time.c
 * (saves/restores the FP environment around a computation, comment cites
 * real Apple radar 3965505) -- absent from this project's Libc checkout
 * entirely (Libm territory, and this project has no Libm at all, per
 * build_libc_printf.sh's own note on __fpclassifyd/nan/nanf/nanl being
 * self-written for the same reason). Real arm64 fenv_t is exactly
 * { unsigned long long __fpsr; unsigned long long __fpcr; } (checked
 * directly against the Xcode-12 iPhoneOS SDK's own fenv.h, not guessed),
 * and the real arm64 ISA instructions to read/write those two registers
 * are just `mrs`/`msr` -- this is a textbook, unambiguous 4-instruction
 * real implementation, last resort per CLAUDE.md's source-priority rule
 * (no Apple or FreeBSD single-file fenv.c exists in this checkout to
 * pull instead). */
typedef struct {
    unsigned long long __fpsr;
    unsigned long long __fpcr;
} fenv_t;

int fegetenv(fenv_t *envp) {
    unsigned long long fpsr, fpcr;
    __asm__ volatile("mrs %0, fpsr" : "=r"(fpsr));
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    envp->__fpsr = fpsr;
    envp->__fpcr = fpcr;
    return 0;
}

int fesetenv(const fenv_t *envp) {
    __asm__ volatile("msr fpsr, %0" : : "r"(envp->__fpsr));
    __asm__ volatile("msr fpcr, %0" : : "r"(envp->__fpcr));
    return 0;
}
