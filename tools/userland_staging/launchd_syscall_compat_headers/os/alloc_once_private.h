#ifndef __OS_ALLOC_ONCE_PRIVATE_LAUNCHD_COMPAT__
#define __OS_ALLOC_ONCE_PRIVATE_LAUNCHD_COMPAT__
/* Real Apple builds assign OS_ALLOC_ONCE_KEY_* across the whole
 * libSystem umbrella from one shared enum (os/alloc_once_impl.h); this
 * project only builds a curated subset, so that shared enum doesn't
 * exist here. Locally assigned, distinct value from the same-purpose
 * compat header build_libpthread.sh already has for its own
 * OS_ALLOC_ONCE_KEY_LIBSYSTEM_PTHREAD -- this one is
 * stdlib/FreeBSD/atexit.c's own real key, doesn't collide with
 * anything else this project compiles. */
#define __OS_ALLOC_INDIRECT__
#include <os/alloc_once_impl.h>
#undef __OS_ALLOC_INDIRECT__
#ifndef OS_ALLOC_ONCE_KEY_LIBSYSTEM_C
#define OS_ALLOC_ONCE_KEY_LIBSYSTEM_C 2
#endif
#endif
