/* Compat stand-in for real macOS's private <get_compat.h> (DAR-185).
 *
 * Real Apple main.c:61-64 (third_party/vim/src/main.c):
 *
 *     #ifdef __APPLE__
 *     #include <get_compat.h>
 *     #else
 *     #define COMPAT_MODE(a,b) (1)
 *     #endif
 *
 * __APPLE__ is always defined by this project's toolchain (real Darwin
 * target), so the #ifdef always takes the <get_compat.h> branch -- but
 * that header is real macOS libc-internal Unix2003-compat-mode detection
 * (used only to tell whether the running binary was invoked via a
 * "bin/vi"/"bin/ex" legacy-compat symlink, main.c's own two call sites,
 * both real-macOS-only), with no equivalent here. This header supplies
 * exactly vim's own non-Apple fallback value verbatim, so COMPAT_MODE()
 * always evaluates true -- the same real behavior a non-Apple build
 * already gets from real, unmodified upstream source.
 */
#ifndef _GET_COMPAT_H_
#define _GET_COMPAT_H_
#define COMPAT_MODE(a,b) (1)
#endif
