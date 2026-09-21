/* pw(8) build-wide augmentation of <unistd.h> (DAR-341).
 *
 * `crypt_set_format()` -- real, unmodified FreeBSD lib/libutil/
 * login_crypt.c (pulled via setup_third_party.sh) calls
 * `crypt_set_format(cipher)`. Real FreeBSD declares this in <unistd.h>;
 * this project's pinned Apple/iPhoneOS SDK <unistd.h> never had a
 * crypt_set_format() concept to declare at all (this project's crypt(3)
 * is Apple's own single-format DES-only implementation -- see
 * pw_crypt_set_format_compat.c's own header for the full real semantics
 * this project's implementation matches).
 */
/*
 * `chflagsat()` -- real FreeBSD declares this in <sys/stat.h>
 * (int chflagsat(int, const char *, unsigned long, int); confirmed via a
 * real `git show` against this project's own pinned third_party/
 * freebsd-src @ release/13.2.0, sys/sys/stat.h). cpdir.c's own real,
 * unmodified copymkdir() calls it but includes neither <sys/stat.h> nor
 * any header this project's own pinned Apple iPhoneOS SDK uses to
 * declare it (confirmed absent, see pw_chflagsat_compat.c's own header
 * for the full real-implementation citation) -- declared here since
 * cpdir.c does include <unistd.h>.
 */
#ifndef _PW_UNISTD_AUGMENT_H_
#define _PW_UNISTD_AUGMENT_H_

#include_next <unistd.h>

int crypt_set_format(const char *);
int chflagsat(int fd, const char *path, unsigned long flags, int atflag);

#endif
