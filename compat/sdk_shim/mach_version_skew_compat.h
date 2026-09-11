/*
 * Compatibility shim for verifying driver code that transitively includes
 * <mach/mach_port.h>/<mach/mach_host.h> against a live installed SDK that's
 * newer than this project's pinned (2019-era, XNU 6153.11.26) xnu checkout.
 * See project memory: this is the same class of gap as the MIG-generated
 * headers under generated/mach/ - the SDK's copies of these userspace
 * syscall-interface headers have grown types this old xnu source doesn't
 * define. None of the types below are ever used by our driver code; they
 * only need to parse so the SDK header's unrelated function prototypes
 * type-check. Not a real definition of any of these - purely for local
 * -fsyntax-only verification, never linked/built for real.
 */
#ifndef _MACH_VERSION_SKEW_COMPAT_H
#define _MACH_VERSION_SKEW_COMPAT_H

#define __swift_nonisolated_unsafe

#include <mach/mach_types.h>

typedef mach_port_t io_main_t;
typedef unsigned int ipc_info_object_type_t;
typedef ipc_space_t ipc_space_read_t;
typedef char kobject_description_t[64];
typedef struct { char reserved[256]; } mach_service_port_info_data_t;
typedef int mach_task_flavor_t;

#endif /* _MACH_VERSION_SKEW_COMPAT_H */
