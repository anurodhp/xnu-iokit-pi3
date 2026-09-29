/* Force-included by build_xserver.sh (meson c_args). reallocarray() is
 * exported by libsystem_malloc.dylib, its Apple owner (third_party/
 * libmalloc src/malloc.c:2023, exported since DAR-424), so the X server's
 * meson has_function('reallocarray') probe passes and include/os.h skips
 * its own prototype -- but no SDK header here declares it (neither the
 * Xcode 12 iPhoneOS 14.4 SDK nor the host's current MacOSX SDK), so every
 * call was an implicit-declaration error. This is libmalloc's own
 * signature. */
#include <stddef.h>
void *reallocarray(void *in_ptr, size_t nmemb, size_t size);
