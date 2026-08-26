/* Real declarations for ld2xpc()/xpc2ld() -- see ld2xpc_compat.c's own
 * header comment for the full story (confirmed genuinely missing from
 * every real Apple source available, including a fresh clone of the
 * historically-open-sourced apple-oss-distributions/launchd tag
 * launchd-842.92.1 -- not just this project's own PureDarwin checkout).
 */
#ifndef _LD2XPC_COMPAT_H_
#define _LD2XPC_COMPAT_H_

#include <xpc/xpc.h>
#include <launch.h>

xpc_object_t ld2xpc(launch_data_t pload);
launch_data_t xpc2ld(xpc_object_t xobj);

#endif /* _LD2XPC_COMPAT_H_ */
