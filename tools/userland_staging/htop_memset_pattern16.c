/* memset_pattern16() for htop.macho. libplatform's own public wrapper is
 * third_party/libplatform/src/string/generic/memset_pattern.c:98-100 (`memset_pattern16` calling
 * `_platform_memset_pattern16`), but that file only defines it under `#if VARIANT_STATIC` (:94-97 pattern, same as
 * memset_pattern4 at :43-47), and libsystem_platform.dylib is built without it (build_libsystem_platform.sh), so no
 * dylib exports the public name; it does export `__platform_memset_pattern16` (nm -gU). This is that same one-line
 * wrapper. htop calls it from darwin/Platform.c's/RichString-style fills; no other program here needs it yet. */
#include <stddef.h>
extern void _platform_memset_pattern16(void *b, const void *pattern16, size_t len);
void memset_pattern16(void *b, const void *pattern16, size_t len) { _platform_memset_pattern16(b, pattern16, len); }
