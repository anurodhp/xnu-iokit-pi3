/* pw(8) build-wide augmentation of <login_cap.h> (DAR-341).
 *
 * Real, unmodified FreeBSD lib/libutil/login_cap.h (its own real
 * `login_getcaptime()`/`login_getcapnum()`/`login_getcapsize()`
 * prototypes) uses `rlim_t` without including <sys/resource.h> itself --
 * relies on the includer having already pulled it in. login_cap.c (also
 * real, unmodified) does include <sys/resource.h> first, so it's fine
 * on its own; login_crypt.c and pw_user.c do NOT (confirmed via grep),
 * so their own real `#include <login_cap.h>` hits an undeclared `rlim_t`
 * (cascading into a real, confusing "redefinition of parameter" error
 * from the parser trying to treat the unknown type name as a K&R-style
 * parameter name). Fixed the same way this project's other *_compat_
 * include augmentation headers do: pull in the one real header
 * login_cap.h actually needs, ahead of it, rather than edit either real
 * upstream file.
 */
#ifndef _PW_LOGIN_CAP_AUGMENT_H_
#define _PW_LOGIN_CAP_AUGMENT_H_

#include <sys/resource.h>
#include_next <login_cap.h>

#endif
