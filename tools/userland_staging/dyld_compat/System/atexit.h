/* Real, standard Itanium C++ ABI type -- `__cxa_range_t` is the
 * documented parameter type for the `__cxa_finalize_ranges` dlclose-time
 * destructor-range-limiting extension (same layout across libc++abi/
 * libstdc++/every real implementation, not Apple-specific internals).
 * dyld/DyldRuntimeState.cpp's real usage (addr + length fields, used to
 * bound which static-destructor ranges get run when unloading a dylib)
 * confirms this exact shape -- not guessed. Apple's real `<System/
 * atexit.h>` (a Libsystem-internal header, never open-sourced
 * standalone) presumably has additional declarations dyld doesn't
 * actually use here (grepped in full -- only this one struct is
 * referenced).
 */
#ifndef _COMPAT_SYSTEM_ATEXIT_H_
#define _COMPAT_SYSTEM_ATEXIT_H_
#include <stddef.h>
struct __cxa_range_t {
    const void *addr;
    size_t length;
};
#endif
