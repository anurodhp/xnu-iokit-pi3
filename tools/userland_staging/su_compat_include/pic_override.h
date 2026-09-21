/* Force-included ahead of everything else in build_openpam.sh's own
 * CFLAGS (DAR-339). openpam.h's real, unmodified static-module gate is
 *
 *     #if defined(__GNUC__) && !defined(__PIC__) && !defined(NO_STATIC_MODULES)
 *
 * `-D__FreeBSD__` alone (this build's own real fix for the OTHER half of
 * that decision, `NO_STATIC_MODULES`) isn't enough: this project's
 * actual Xcode-12/arm64-apple-ios14.4 clang always predefines `__PIC__`
 * (confirmed directly -- `-dM -E` on this exact target/flag set shows
 * `__PIC__` defined even with `-fno-PIC` passed explicitly; Darwin
 * clang appears to ignore that flag for this target). `__PIC__`'s real
 * purpose in openpam.h is a heuristic proxy for "linker-set sections may
 * not work safely in position-independent/shared-library code" -- a
 * concern that doesn't apply to su_compat_include/linker_set.h's own
 * replacement (getsectiondata() computes real, already-slid runtime
 * addresses regardless of PIC-ness, unlike real FreeBSD's own
 * boundary-symbol mechanism). `#undef` here only hides the macro from
 * the PREPROCESSOR for `#ifdef`/`#if defined` checks in this translation
 * unit -- it does NOT change actual codegen (that's controlled by the
 * real `-fPIC`/`-fno-PIC` compiler flag, untouched). Confirmed via grep
 * that no other real source file anywhere in this build's dependency
 * closure (third_party/OpenPAM, third_party/Libc) branches on `__PIC__`
 * at all -- openpam.h is the only real consumer, so this is safe and
 * narrowly scoped to exactly the one real decision it needs to flip.
 */
#undef __PIC__
