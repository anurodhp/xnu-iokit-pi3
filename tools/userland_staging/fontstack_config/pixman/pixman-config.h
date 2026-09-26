/* pixman-0.46.4 pixman-config.h (DAR-384). Stands in for the file
 * pixman/meson.build:21 configure_file()s from meson.build's `config`
 * object. Each define below cites the meson.build line that sets it; the
 * answers are what `meson setup` gave when its checks were run against
 * this project's own Xcode 12 iPhoneOS 14.4 toolchain and its own
 * libc_build/system dylibs (has_function links against those dylibs, so
 * "YES" means this libSystem really exports the function).
 *
 * Left undefined on purpose:
 *   HAVE_GETISAX        (:497) Solaris only; no such function here.
 *   HAVE_FEENABLEEXCEPT (:510) glibc extension; not in Apple's libm.
 *   HAVE_FLOAT128       (:565-568) "Has float128 support" does not link.
 *   USE_ARM_NEON        (:303-317) 32-bit ARM NEON; this is AArch64.
 *   USE_OPENMP          (-Dopenmp=disabled; tests only).
 *   ASM_HAVE_FUNC_DIRECTIVE / ASM_HAVE_SYNTAX_UNIFIED (:252/:260) the
 *                       Darwin assembler accepts neither directive, so
 *                       meson's compile checks fail, as on any Mac.
 */
#pragma once
#define ASM_LEADING_UNDERSCORE 1                  /* :276, Mach-O C symbols carry a leading _ */
#define HAVE_ALARM 1                              /* :497-507 */
#define HAVE_BUILTIN_CLZ 1                        /* :572 */
#define HAVE_FEDIVBYZERO 1                        /* :516-518 */
#define HAVE_FENV_H 1                             /* :520-524 */
#define HAVE_GCC_VECTOR_EXTENSIONS 1              /* :580 */
#define HAVE_GETPAGESIZE 1                        /* :497-507 */
#define HAVE_GETTIMEOFDAY 1                       /* :497-507 */
#define HAVE_MMAP 1                               /* :497-507 */
#define HAVE_MPROTECT 1                           /* :497-507 */
#define HAVE_POSIX_MEMALIGN 1                     /* :499-507 */
#define HAVE_PTHREADS 1                           /* :494 */
#define HAVE_SIGACTION 1                          /* :497-507 */
#define HAVE_SYS_MMAN_H 1                         /* :520-524 */
#define HAVE_UNISTD_H 1                           /* :520-524 */
#define PACKAGE foo                               /* :590, verbatim */
#define SIZEOF_LONG 8                             /* :587 */
#define TLS __thread                              /* :542; dyld-940 TLVs work here (tlv_smoketest) */
#define TOOLCHAIN_SUPPORTS_ATTRIBUTE_CONSTRUCTOR 1 /* :553 */
#define TOOLCHAIN_SUPPORTS_ATTRIBUTE_DESTRUCTOR 1  /* :562 */
#define USE_ARM_A64_NEON 1                        /* :319-330, a64-neon-test.S compiles */
#define USE_GCC_INLINE_ASM 1                      /* :418 */
