/*
 * Defines every X.org configure.ac here gets for free from autoconf's
 * AC_INCLUDES_DEFAULT header probe (run by the first AC_CHECK_HEADER/
 * AC_CHECK_FUNC in each configure) plus AC_SYS_LARGEFILE. Shared by every
 * x11_config/<lib>/config.h (DAR-383). Each header below exists in the
 * Xcode 12 iPhoneOS 14.4 SDK this project compiles against.
 *
 * AC_SYS_LARGEFILE defines nothing on Darwin: off_t is already 64-bit
 * (sys/_types/_off_t.h), so _FILE_OFFSET_BITS stays unset, as it does on
 * a real macOS configure run.
 */
#ifndef X11_COMMON_AUTOCONF_H
#define X11_COMMON_AUTOCONF_H
#define HAVE_INTTYPES_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define STDC_HEADERS 1
#endif
