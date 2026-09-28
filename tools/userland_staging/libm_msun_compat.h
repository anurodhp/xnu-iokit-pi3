/* Force-included (-include) when build_libm_dylib.sh compiles real,
 * unmodified FreeBSD msun sources (third_party/freebsd-src, release/13.2.0,
 * lib/msun/src) against Darwin's own headers. DAR-384. Supplies only the
 * handful of FreeBSD <sys/cdefs.h> / <machine/_types.h> spellings msun
 * uses that Darwin's headers spell differently or lack; nothing here
 * changes an algorithm.
 *
 * Sources for each definition (read at release/13.2.0 with
 * `git -C third_party/freebsd-src show HEAD:<path>`):
 *
 *   __CONCAT   sys/sys/cdefs.h:150-151. FreeBSD's two-level form expands
 *              its arguments before pasting; Darwin's <sys/cdefs.h> pastes
 *              directly (`x ## y`), which breaks math_private.h:637's
 *              __CONCAT(0x1.8p, LDBL_MANT_DIG) ("exponent has no digits").
 *   __always_inline  sys/sys/cdefs.h:352 (e_rem_pio2f.c:44).
 *   __double_t, __float_t  sys/arm64/include/_types.h:60-61 (double and
 *              float on arm64; math_private.h:593/:613).
 *   __weak_reference  sys/sys/cdefs.h:536-538 emits ELF `.weak alias` +
 *              `.equ alias, sym`. The Mach-O equivalent here is a plain
 *              `.globl _alias` + `.set _alias, _sym` -- a strong alias, on
 *              purpose: a weak definition in a shipped dylib creates
 *              <weak-def-coalesce> binds (see CLAUDE.md, weak_def_audit.sh),
 *              and on Darwin long double is double, so e.g. frexpl really
 *              is frexp, which is how Apple's own libm exports it too.
 *   (__FBSDID needs nothing: Darwin's <sys/cdefs.h>:414 already defines
 *   it to expand to nothing.)
 */
#ifndef LIBM_MSUN_COMPAT_H
#define LIBM_MSUN_COMPAT_H
#include <sys/cdefs.h>

#undef __CONCAT
#define __CONCAT1(x, y) x ## y
#define __CONCAT(x, y) __CONCAT1(x, y)

#ifndef __always_inline
#define __always_inline __attribute__((__always_inline__))
#endif

typedef double __double_t;
typedef float __float_t;

#define __weak_reference(sym, alias) \
    __asm__(".globl _" #alias "\n\t.set _" #alias ", _" #sym)

/* FreeBSD sys/sys/cdefs.h's __strong_reference is a strong alias
 * (__attribute__((__alias__))); Mach-O has no alias attribute, so this
 * uses the same .set form as __weak_reference above. Used by
 * s_scalbnf.c (scalbnf -> ldexpf), DAR-424. */
#define __strong_reference(sym, alias) \
    __asm__(".globl _" #alias "\n\t.set _" #alias ", _" #sym)

#endif
