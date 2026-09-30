/* mach_port_space_basic_info() -- libsystem_kernel.dylib does not export it.
 * Body is verbatim third_party/xnu-7195/libsyscall/mach/mach_port.c:428-439
 * (it forwards to the MIG routine _kernelrpc_mach_port_space_basic_info,
 * generated from osfmk/mach/mach_port.defs:645). Local to top. */
#include <mach/mach.h>
#include <mach_debug/ipc_info.h>
extern kern_return_t _kernelrpc_mach_port_space_basic_info(ipc_space_t, ipc_info_space_basic_t *);
kern_return_t
mach_port_space_basic_info(ipc_space_t task, ipc_info_space_basic_t *space_basic_info)
{
	kern_return_t rv;
	rv = _kernelrpc_mach_port_space_basic_info(task, space_basic_info);
	return rv;
}
