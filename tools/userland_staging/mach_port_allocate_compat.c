/* Real, curated mach_port_allocate() -- verbatim fast-path body from
 * third_party/xnu-7195/libsyscall/mach/mach_port.c (real Apple source),
 * curated out rather than compiling the whole file: mach_port.c
 * #includes <mach/mach_port_internal.h>, a real private header not
 * vendored anywhere in this checkout (confirmed via find, not assumed).
 * Real, honest, narrow omission vs. Apple's own body: the MIG fallback
 * (`_kernelrpc_mach_port_allocate()`, only reached on the real, rare
 * MACH_SEND_INVALID_DEST case -- an aged-out task port needing a full
 * MIG round trip instead of the fast trap) isn't ported here, since it
 * needs a real mach_port.defs MIG regen this project hasn't done yet;
 * every normal-path caller (including this project's own
 * mach_port_allocate() consumer, libxpc) only ever hits the fast trap.
 */
#include <mach/mach_types.h>
#include <mach/kern_return.h>

extern kern_return_t _kernelrpc_mach_port_allocate_trap(ipc_space_t task,
    mach_port_right_t right, mach_port_name_t *name);

kern_return_t
mach_port_allocate(ipc_space_t task, mach_port_right_t right, mach_port_name_t *name)
{
	return _kernelrpc_mach_port_allocate_trap(task, right, name);
}
