/* Real OpenBSD pwd.h private constants (DAR-174, pwd_mkdb.tproj) --
 * system_cmds/pwd_mkdb.tproj/pwd_mkdb.c's own file header identifies it
 * as $OpenBSD: pwd_mkdb.c,v 1.36 2003/06/08 21:14:55 millert Exp $, and
 * it uses two private pwd.h constants Apple's own public iPhoneOS SDK
 * pwd.h (the one this build's <pwd.h> resolves to -- this project has
 * no vendored pwd.h of its own, confirmed via `find`) never carries:
 * _PW_NAME_LEN (username length bound for the -u single-record-update
 * path) and _PW_YPTOKEN (the literal NIS/YP marker key pwd_mkdb.c
 * writes only when hasyp is true -- always false in this build, no YP
 * support compiled in, same real "dead code, still has to compile"
 * shape as build_passwd.sh's own INFO_NIS discussion). Real values
 * confirmed via a real `curl` of openbsd/src's own include/pwd.h
 * (the same real upstream pwd_mkdb.c itself traces to):
 *   #define _PW_NAME_LEN 31
 *   #define _PW_YPTOKEN "__YP!"
 * Apple's own public SDK pwd.h DOES already define _PW_KEYBYNAME/
 * _PW_KEYBYNUM/_PW_KEYBYUID (confirmed via grep) -- only these two are
 * missing, not the whole private block. */
#ifndef _PWD_MKDB_OPENBSD_PWD_COMPAT_H_
#define _PWD_MKDB_OPENBSD_PWD_COMPAT_H_
#ifndef _PW_NAME_LEN
#define _PW_NAME_LEN 31
#endif
#ifndef _PW_YPTOKEN
#define _PW_YPTOKEN "__YP!"
#endif
#endif
