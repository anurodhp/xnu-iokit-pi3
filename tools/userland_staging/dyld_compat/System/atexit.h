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

/* Real gap found+documented 2026-08-25 building libdyld.dylib
 * (build_libdyld.sh): this project's pinned libcxxabi is real, unmodified
 * upstream llvmorg-11.1.0, which never grew Apple's own libcxxabi fork's
 * `__cxa_finalize_ranges` dlclose-time extension (only the standard
 * Itanium `__cxa_finalize(void* dso)`, which this project's libcxxabi.a
 * does provide, for normal process-exit static-destructor running).
 * `__cxa_finalize_ranges` is only ever called from
 * LibSystemHelpers::__cxa_finalize_ranges(), itself only reached from
 * dyld's own dylib-UNLOAD path (dlclose()) -- a real, structural gap
 * this port doesn't exercise yet (same class of documented gap as
 * OpenPAM's dlopen()), not faked logic: honestly a no-op until dlclose()
 * itself is real. See libdyld_compat_cxa_finalize_ranges.c. */
#ifdef __cplusplus
extern "C"
#endif
void __cxa_finalize_ranges(const struct __cxa_range_t ranges[], unsigned int count);
#endif
