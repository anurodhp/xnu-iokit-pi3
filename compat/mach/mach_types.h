/*
 * Compat shim: the pinned xnu-6153.11.26 source tree's own
 * osfmk/mach/mach_types.h predates a few typedefs that the Xcode 12
 * iPhoneOS SDK's mach/task.h (the only SDK version available on this
 * host) already expects. Pull in xnu's real header first via
 * #include_next, then backfill just the missing names - kext code here
 * never calls these MIG routines, it just needs them to parse.
 */
#include_next <mach/mach_types.h>

#ifndef IOKIT_XNU_SDK_TASK_TYPE_COMPAT
#define IOKIT_XNU_SDK_TASK_TYPE_COMPAT
typedef mach_port_t task_policy_get_t;
typedef mach_port_t task_read_t;
typedef mach_port_t suid_cred_t;
typedef char suid_cred_path_t[1024];
typedef uint32_t suid_cred_uid_t;
#endif
