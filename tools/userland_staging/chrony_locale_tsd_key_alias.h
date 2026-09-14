#include <pthread/tsd_private.h>

/* Real gap: this project's pinned third_party/Libc/stdtime/FreeBSD/
 * localtime.c and third_party/libpthread/private/pthread/tsd_private.h
 * come from slightly different real Apple source vintages -- the real
 * reserved TSD slot numbers ARE present in tsd_private.h (confirmed:
 * __PTK_LIBC_LOCALTIME_KEY 12, __PTK_LIBC_GMTIME_KEY 13), just under an
 * older name than localtime.c's own __LIBC_PTHREAD_KEY_LOCALTIME/
 * __LIBC_PTHREAD_KEY_GMTIME. Same real numeric slots, not a fabricated
 * value. */
#ifndef __LIBC_PTHREAD_KEY_LOCALTIME
#define __LIBC_PTHREAD_KEY_LOCALTIME __PTK_LIBC_LOCALTIME_KEY
#endif
#ifndef __LIBC_PTHREAD_KEY_GMTIME
#define __LIBC_PTHREAD_KEY_GMTIME __PTK_LIBC_GMTIME_KEY
#endif
#ifndef __LIBC_PTHREAD_KEY_XLOCALE
#define __LIBC_PTHREAD_KEY_XLOCALE __PTK_LIBC_LOCALE_KEY
#endif
