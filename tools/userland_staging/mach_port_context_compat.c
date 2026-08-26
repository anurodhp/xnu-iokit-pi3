/* Real, curated mach_port_get_context()/mach_port_set_context() --
 * verbatim bodies from third_party/xnu-7195/libsyscall/mach/mach_port.c
 * (real Apple source; this project already curated mach_port_allocate()
 * out of the same file, see mach_port_allocate_compat.c's own header).
 *
 * Both real bodies are thin wrappers that just call
 * _kernelrpc_mach_port_get_context()/_kernelrpc_mach_port_set_context()
 * -- real MIG client routines, NOT raw traps. Real mechanism (confirmed
 * by reading osfmk/mach/mach_port.defs directly): that .defs file has
 * `UserPrefix _kernelrpc_;` gated `#if !KERNEL && !LIBSYSCALL_INTERFACE`
 * -- i.e. a plain (non-LIBSYSCALL_INTERFACE) client mig regen of
 * mach_port.defs, exactly what build_libxpc_syscalls2.sh does, produces
 * routines literally named `_kernelrpc_mach_port_get_context`/
 * `_kernelrpc_mach_port_set_context`, matching what this file expects to
 * link against with zero renaming needed.
 */
#include <mach/mach_types.h>
#include <mach/kern_return.h>

extern kern_return_t _kernelrpc_mach_port_get_context(mach_port_name_t task,
    mach_port_name_t name, mach_vm_address_t *context);
extern kern_return_t _kernelrpc_mach_port_set_context(mach_port_name_t task,
    mach_port_name_t name, mach_vm_address_t context);

kern_return_t
mach_port_get_context(
	mach_port_name_t task,
	mach_port_name_t name,
	mach_port_context_t *context)
{
	kern_return_t rv;
	mach_vm_address_t wide_context;

	rv = _kernelrpc_mach_port_get_context(task, name, &wide_context);

	if (rv == KERN_SUCCESS) {
		*context = (mach_port_context_t)wide_context;
	}

	return rv;
}

kern_return_t
mach_port_set_context(
	mach_port_name_t task,
	mach_port_name_t name,
	mach_port_context_t context)
{
	return _kernelrpc_mach_port_set_context(task, name, (mach_vm_address_t)context);
}
