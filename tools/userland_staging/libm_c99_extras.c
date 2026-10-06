/* libm functions the iokit port's libsystem_m does not export and msun's versions cannot be compiled for here
 * (s_fmax.c wants FreeBSD's fpmath.h, s_lround.c sys/limits.h, s_lrintf.c a templated math.h clash). Each is the
 * C99 definition (n1570 7.12.12 fmax/fmin, 7.12.9 lrint/lround/llrint/llround, 7.12.6.6 ldexp) over functions
 * the port does export (rint, rintf, round, roundf, scalbn). DARW-290: moved here from Florence's compat/flo_math.c so libsystem_m exports them. */
double rint(double); float rintf(float); double round(double); float roundf(float); double scalbn(double, int);

/* fmax/fmin: a NaN argument is ignored if the other is a number (7.12.12.2 / 7.12.12.3) */
double fmax(double x, double y) { if (x != x) return y; if (y != y) return x; return x > y ? x : y; }
double fmin(double x, double y) { if (x != x) return y; if (y != y) return x; return x < y ? x : y; }
float fmaxf(float x, float y) { if (x != x) return y; if (y != y) return x; return x > y ? x : y; }
float fminf(float x, float y) { if (x != x) return y; if (y != y) return x; return x < y ? x : y; }

long lrintf(float x) { return (long)rintf(x); }
long long llrintf(float x) { return (long long)rintf(x); }
long lround(double x) { return (long)round(x); }
long lroundf(float x) { return (long)roundf(x); }
long long llroundf(float x) { return (long long)roundf(x); }

/* ldexp(x, e) is scalbn(x, e) when FLT_RADIX is 2 (7.12.6.6) */
double ldexp(double x, int e) { return scalbn(x, e); }

/* Float variants as the double function rounded to float: the port exports the double ones, and msun's own float
 * versions (e_coshf.c, s_asinhf.c ...) pull in fenv and complex helpers (fetestexcept, crealf) it does not export.
 * double -> float rounding is within 1 ulp of the correctly rounded float result. */
double sinh(double), cosh(double), tanh(double), asinh(double), acosh(double), atanh(double);
float sinhf(float x) { return (float)sinh(x); }
float coshf(float x) { return (float)cosh(x); }
float tanhf(float x) { return (float)tanh(x); }
float asinhf(float x) { return (float)asinh(x); }
float acoshf(float x) { return (float)acosh(x); }
float atanhf(float x) { return (float)atanh(x); }
/* nearbyint is rint without raising FE_INEXACT; nothing here reads the flag */
double nearbyint(double x) { return rint(x); }
float nearbyintf(float x) { return rintf(x); }

/* C99 <fenv.h> exception-flag functions msun's float code calls (feclearexcept, fetestexcept, feholdexcept,
 * feupdateenv). arm64 Darwin <fenv.h>: fenv_t is { __fpcr, __fpsr } (64-bit each), FE_INVALID 0x1 .. FE_INEXACT 0x10
 * are FPSR bits 0-4 (IOC, DZC, OFC, UFC, IXC; Arm ARM D12.2.64). No trapping is ever enabled on this port. */
typedef struct { unsigned long long fpcr, fpsr; } fl_fenv_t;
#define FL_FE_ALL 0x1fULL
static inline unsigned long long rd_fpsr(void) { unsigned long long v; __asm__ volatile("mrs %0, fpsr" : "=r"(v)); return v; }
static inline void wr_fpsr(unsigned long long v) { __asm__ volatile("msr fpsr, %0" :: "r"(v)); }
static inline unsigned long long rd_fpcr(void) { unsigned long long v; __asm__ volatile("mrs %0, fpcr" : "=r"(v)); return v; }
static inline void wr_fpcr(unsigned long long v) { __asm__ volatile("msr fpcr, %0" :: "r"(v)); }
int fetestexcept(int e) { return (int)(rd_fpsr() & (unsigned long long)e & FL_FE_ALL); }
int feclearexcept(int e) { wr_fpsr(rd_fpsr() & ~((unsigned long long)e & FL_FE_ALL)); return 0; }
int feholdexcept(fl_fenv_t *env) { env->fpcr = rd_fpcr(); env->fpsr = rd_fpsr(); wr_fpsr(env->fpsr & ~FL_FE_ALL); return 0; }
int feupdateenv(const fl_fenv_t *env) {
    unsigned long long raised = rd_fpsr() & FL_FE_ALL;
    wr_fpcr(env->fpcr); wr_fpsr(env->fpsr | raised);
    return 0;
}
