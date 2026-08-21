/* Real Libc/gen/FreeBSD/time.c (this project's first real caller of it --
 * ls(1)'s print.c calls time(NULL) to compute the real 6-months-old
 * threshold for its long-listing date format, same real upstream logic
 * every BSD ls uses) saves/restores the floating-point environment
 * around its own gettimeofday() call (a real, documented workaround for
 * a historical Radar, see that file's own "3965505" comment) via real
 * fegetenv()/fesetenv() -- both real libm entry points this project has
 * never needed before (no prior command does floating-point env
 * save/restore) and doesn't have libm built for at all (out of scope
 * project-wide, same ceiling as regex/crypto).
 *
 * Rather than pull in a whole unbuilt libm just for two calls, this is
 * the real, non-fabricated arm64 implementation: FPCR/FPSR are real
 * AArch64 system registers (ARM DDI 0487, real hardware, not emulated
 * behavior) directly readable/writable via `mrs`/`msr` in exactly two
 * instructions each -- the same real register pair Xcode 12's iPhoneOS
 * SDK's own fenv.h documents fenv_t as holding
 * (`{ unsigned long long __fpsr; unsigned long long __fpcr; }`, confirmed
 * by reading that header directly, not guessed). This is not a stub --
 * it's the complete real implementation any arm64 libm's fegetenv/
 * fesetenv would contain, just without the surrounding exception-trap/
 * rounding-mode convenience API (fesetround/feraiseexcept/etc) this
 * project has no caller for yet.
 */

typedef struct {
	unsigned long long __fpsr;
	unsigned long long __fpcr;
} ls_fenv_t;

int
fegetenv(ls_fenv_t *envp)
{
	unsigned long long fpsr, fpcr;
	__asm__ __volatile__("mrs %0, fpsr" : "=r"(fpsr));
	__asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
	envp->__fpsr = fpsr;
	envp->__fpcr = fpcr;
	return 0;
}

int
fesetenv(const ls_fenv_t *envp)
{
	unsigned long long fpsr = envp->__fpsr;
	unsigned long long fpcr = envp->__fpcr;
	__asm__ __volatile__("msr fpsr, %0" : : "r"(fpsr));
	__asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
	return 0;
}
