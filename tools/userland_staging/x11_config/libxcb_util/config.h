/* config.h shared by xcb-util 0.4.1, xcb-util-wm 0.4.2, xcb-util-renderutil
 * 0.3.10, xcb-util-image 0.4.1 and xcb-util-cursor 0.1.6 (build_libxcb_util.sh;
 * Gershwin WindowManager M1). The five sources only test the macros below. */
#include "../common_autoconf.h"

/* xcb-util atoms.c:30 tests HAVE_VASPRINTF (AC_CHECK_FUNCS([vasprintf]) in
 * xcb-util/configure.ac). vasprintf is declared in the iOS 14.4 SDK's
 * <stdio.h>:377 and exported by libsystem_c. */
#define HAVE_VASPRINTF 1

/* xcb-util-image configure.ac:12 AC_CHECK_HEADERS([sys/shm.h]); the SDK has it. */
#define HAVE_SYS_SHM_H 1

/* xcb-util-cursor configure.ac:22 AC_CHECK_HEADERS([endian.h sys/endian.h
 * sys/byteorder.h libkern/OSByteOrder.h], [break]): <endian.h> and
 * <sys/endian.h> are absent from the SDK, so the loop reaches
 * libkern/OSByteOrder.h (parse_cursor_file.c:51 uses it). le32toh is not
 * defined by it, which parse_cursor_file.c:48-55 handles. */
#define HAVE_LIBKERN_OSBYTEORDER_H 1
