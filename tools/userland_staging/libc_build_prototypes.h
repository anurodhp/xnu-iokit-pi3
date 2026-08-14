/* Real prototypes for this project's own shim functions
 * (pthread_tsd_shim.c, libc_locale_syscalls.c, libc_locale_env_stubs.c),
 * force-included (-include) into every Libc source file this build
 * compiles.
 *
 * REAL BUG FOUND BY ADVERSARIAL REVIEW, FIXED HERE: with -w suppressing
 * all warnings (including -Werror-promoted ones), several real Apple/
 * FreeBSD Libc source files were calling these functions with ZERO
 * prototype in scope - `_pthread_key_create`/`_pthread_once`/
 * `_pthread_getspecific` (strsignal.c), `_open`/`_read`/`_write`/
 * `_close`/`_fstat` (collate.c/ldpart.c/rune.c/setlocale.c),
 * `_getprogname` (collate.c). C's implicit-int-declaration fallback
 * silently assumed `int name()` for all of them - happened to match by
 * luck for the ones returning int, but a real, unverified assumption
 * (and definitely wrong for `_getprogname`, which returns `const char *`,
 * not `int` - a real return-type mismatch that -w was hiding). Removing
 * -w and adding real, correct prototypes here turns "compiles because
 * nothing checked" into "compiles because the compiler verified the call
 * sites match the real signatures".
 */
#ifndef _LIBC_BUILD_PROTOTYPES_H_
#define _LIBC_BUILD_PROTOTYPES_H_

typedef unsigned long __libc_build_size_t;
typedef long __libc_build_ssize_t;

/* libc_locale_syscalls.c - _open's real signature (matching real open(2))
 * takes mode as a variadic third arg, only meaningful with O_CREAT; a
 * fixed 3-required-args prototype rejected real call sites in setlocale.c
 * that pass just (path, flags) - found by this project's own compiler
 * once real prototypes replaced the previous implicit-int assumption. */
int _open(const char *path, int flags, ...);
__libc_build_ssize_t _read(int fd, void *buf, __libc_build_size_t n);
__libc_build_ssize_t _write(int fd, const void *buf, __libc_build_size_t n);
int _close(int fd);
int _fstat(int fd, void *statbuf);
int close(int fd); /* public alias, see libc_locale_syscalls.c */

/* libc_locale_env_stubs.c */
const char *_getprogname(void);

/* pthread_tsd_shim.c - underscore-prefixed private aliases. Key type is
 * `unsigned long`, matching real pthread_key_t (see pthread_tsd_shim.c's
 * own comment on the incompatible-pointer-types bug this fixed - an
 * earlier `int`/`int *` version silently truncated real callers'
 * 8-byte pthread_key_t writes). */
int _pthread_key_create(unsigned long *key, void (*destructor)(void *));
int _pthread_setspecific(unsigned long key, const void *value);
void *_pthread_getspecific(unsigned long key);
int _pthread_once(void *once_control, void (*init_routine)(void));

#endif
