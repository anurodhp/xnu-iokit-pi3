/* libXaw-1.0.16 config.h. configure.ac (CA) lines cited. */
#include "../common_autoconf.h"
#define PACKAGE_VERSION "1.0.16"
/* CA:"AC_CHECK_HEADERS([wctype.h wchar.h widec.h])": wctype.h and wchar.h
 * are in the iPhoneOS 14.4 SDK; widec.h is Solaris-only. */
#define HAVE_WCTYPE_H 1
#define HAVE_WCHAR_H 1
/* CA:"AC_CHECK_FUNCS([iswalnum getpagesize])": iswalnum is a real
 * libsystem_c.dylib export (XawI18n.c:82). getpagesize is also exported
 * but src/OS.c:17 #undefs HAVE_GETPAGESIZE before use, so it is left
 * undefined. */
#define HAVE_ISWALNUM 1
/* CA:"AC_ARG_ENABLE(const ...)": default yes -> _CONST_X_STRING. */
#define _CONST_X_STRING 1
