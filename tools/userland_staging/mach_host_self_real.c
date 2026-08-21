/* Real, verbatim mach_host_self() body, curated from third_party/
 * xnu-7195/libsyscall/mach/mach_legacy.c (real, unmodified, 3-function
 * file: mach_host_self/mach_task_self/mach_thread_self, all trivial
 * `return X_self_trap();` wrappers) -- only mach_host_self() is kept
 * here. mach_task_self() is NOT needed: this project already has a
 * real, working mach_task_self() via the standard
 * `#define mach_task_self() mach_task_self_` macro (mach/mach_init.h)
 * plus the real `mach_task_self_` global mach_init.c's mach_init_doit()
 * already sets (linked into libc_build/libmalloc.a, exercised by every
 * consumer that calls malloc() -- see libmalloc_dispatch.c's get_zone()).
 * mach_thread_self() is dropped: no caller in this project's Phase 4.6
 * item 5 work needs it, and pulling it in would need thread_self_trap
 * declared too (see mach_msg_trap_decl.h's header comment for why the
 * SDK's own mach_traps.h doesn't declare it under this project's build
 * flags) for zero real benefit.
 *
 * host_self_trap() itself is declared in mach_msg_trap_decl.h, same
 * SDK-header-shadowing reason as mach_msg_trap -- see that file's
 * comment.
 */
#include <mach/mach_types.h>

extern mach_port_name_t host_self_trap(void);

mach_port_t
mach_host_self(void)
{
	return host_self_trap();
}
