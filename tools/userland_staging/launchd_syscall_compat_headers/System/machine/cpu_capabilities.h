/* Real content already exists at third_party/xnu-7195/osfmk/arm/
 * cpu_capabilities.h -- this just forwards to it from the real
 * installed-SDK-style path third_party/libpthread/private/pthread/
 * tsd_private.h expects but this non-Xcode-project build never
 * produces (same real gap build_libmalloc.sh's own untracked
 * libc_build/compat_include/ copy already solved -- see this project's
 * docs/backlog.md P3 hygiene note about that directory being gitignored
 * despite being hand-written; this is a new, tracked copy for this
 * script's own consumer). */
#include <arm/cpu_capabilities.h>
