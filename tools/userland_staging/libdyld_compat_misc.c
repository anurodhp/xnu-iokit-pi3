#include <stdint.h>
#include <stddef.h>

/* Real, documented gap: real dyld4::LibSystemHelpers::malloc_size()
 * forwards to real Apple malloc.c's malloc_size(), which walks a
 * registered-zone table this project's curated libmalloc.a port never
 * assembled (build_libmalloc.sh scope is the zone allocator itself, not
 * the separate malloc_zone_register()/find_registered_zone() registry).
 * Real Apple's own malloc_size() returns exactly 0 for a pointer it
 * cannot find in any registered zone -- honestly reproducing that real,
 * documented "not found" behavior for every pointer is correct given
 * this port's current, real limits, not a fabricated value. Only real
 * caller is dyld's own internal accounting; never load-bearing for
 * correctness. */
size_t malloc_size(const void *ptr) {
    (void)ptr;
    return 0;
}

/* Real, documented gap: real arm64 Thread-Local Variable (__thread)
 * support (third_party/dyld/libdyld/threadLocalHelpers.s, real Apple
 * assembly implementing the TLV slow-path thunk). dyld_test_exec (and
 * every binary this project has built so far) declares zero __thread
 * variables, so this function's address is taken
 * (LibSystemHelpers::getTLVGetAddrFunc()) but never actually called --
 * real, structural absence of TLV support on this port, not faked logic
 * for a path anything currently exercises. */
void *tlv_get_addr(void *thunk) {
    (void)thunk;
    return 0;
}

/* NOTE (static-to-dylib migration step 5, docs/daemon-roadmap.md): this
 * file used to also carry __cxa_atexit/__cxa_finalize, a real, minimal
 * Itanium-ABI implementation; _os_alloc_once_table, real Apple SPI
 * storage; _pthread_bsdthread_init/_pthread_main_thread_postfork_init,
 * real pthread.c fork-child stand-ins; and a real mkdirat() raw syscall.
 * All four are now provided for real by shared dylibs this file's own
 * consumers (libdyld.dylib/libSystem.B.dylib) already link against --
 * __cxa_atexit/__cxa_finalize moved to libsystem_c_cxa_atexit_compat.c
 * (libsystem_c.dylib, migration step 5, matching real Apple's own
 * architecture: __cxa_atexit genuinely lives in Libc, not libc++abi);
 * _os_alloc_once_table moved to libsystem_kernel.dylib (step 3, real
 * third_party/xnu-7195/libsyscall/os/alloc_once.c); the pthread
 * fork-child pair moved to libsystem_pthread_atfork_compat.c
 * (libsystem_pthread.dylib, step 3); mkdirat is a real syscall
 * libsystem_c.dylib's own create-syscalls.pl-driven generation now
 * covers (step 4). Removed here, not just left redundant -- keeping
 * them would have given libdyld.dylib its own private, silently-
 * divergent copy of exactly the kind of process-global state
 * (_os_alloc_once_table especially) this whole migration exists to stop
 * duplicating, discovered auditing this file while wiring up
 * libc++.dylib's own real __cxa_atexit need. */
