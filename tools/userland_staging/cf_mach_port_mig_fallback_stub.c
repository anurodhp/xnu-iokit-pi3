/* Honest, documented stub for the real MIG fallback half of
 * cf_mach_port_syscalls.c's trap-first/MIG-fallback functions
 * (_kernelrpc_mach_port_allocate/construct/destruct/extract_member/
 * get_attributes/insert_member/type -- the non-trap names, real Apple
 * generates these from osfmk/mach/mach_port.defs via `mig`).
 *
 * Real, verified reasoning for stubbing rather than generating the real
 * MIG client code here: every one of the 7 real trap entry points these
 * functions fall back FROM (mach_port_traps.s) is confirmed backed by a
 * real kernel-side implementation this project's own from-source kernel
 * actually builds (osfmk/ipc/mach_kernelrpc.c, not a stub) -- so on a
 * real working boot, the trap always succeeds and this fallback path is
 * dead code, only reached if a trap call unexpectedly returns
 * MACH_SEND_INVALID_DEST. The real MIG marshaling code (generated via
 * `mig` from mach_port.defs, same real mechanism
 * build_corefoundation.sh's own mach_vm_region already uses) needs a
 * real mach_msg()/mach_msg_destroy() to actually send the RPC -- and
 * this project has no real mach_msg() anywhere yet (docs/backlog.md
 * item 7, "No real Mach IPC (mach_msg() trap + MIG RPC) in userland" --
 * a real, substantial, already-tracked separate task, not something to
 * casually pull in as a side effect of wiring up mach_port_allocate).
 *
 * KERN_NOT_SUPPORTED (46) is a real, standard Mach kern_return_t value
 * (osfmk/mach/kern_return.h) -- an honest "this path isn't implemented"
 * signal to any real caller, not a fabricated success. Real fix, when
 * mach_msg() itself eventually lands: replace this whole file with the
 * real `mig`-generated mach_portUser.c (already proven to compile clean
 * against this project's headers -- see build_corefoundation.sh's own
 * mach_vm_region precedent for the exact recipe).
 */
#include <mach/mach_types.h>
#include <mach/message.h>
#include <mach/kern_return.h>

kern_return_t _kernelrpc_mach_port_allocate(ipc_space_t task, mach_port_right_t right, mach_port_name_t *name) {
	(void)task; (void)right; (void)name;
	return KERN_NOT_SUPPORTED;
}

kern_return_t _kernelrpc_mach_port_construct(ipc_space_t task, mach_port_options_t *options, uint64_t context, mach_port_name_t *name) {
	(void)task; (void)options; (void)context; (void)name;
	return KERN_NOT_SUPPORTED;
}

kern_return_t _kernelrpc_mach_port_destruct(ipc_space_t task, mach_port_name_t name, mach_port_delta_t srdelta, uint64_t guard) {
	(void)task; (void)name; (void)srdelta; (void)guard;
	return KERN_NOT_SUPPORTED;
}

kern_return_t _kernelrpc_mach_port_extract_member(ipc_space_t task, mach_port_name_t name, mach_port_name_t pset) {
	(void)task; (void)name; (void)pset;
	return KERN_NOT_SUPPORTED;
}

kern_return_t _kernelrpc_mach_port_get_attributes(ipc_space_t task, mach_port_name_t name, mach_port_flavor_t flavor, mach_port_info_t port_info_out, mach_msg_type_number_t *port_info_outCnt) {
	(void)task; (void)name; (void)flavor; (void)port_info_out; (void)port_info_outCnt;
	return KERN_NOT_SUPPORTED;
}

kern_return_t _kernelrpc_mach_port_insert_member(ipc_space_t task, mach_port_name_t name, mach_port_name_t pset) {
	(void)task; (void)name; (void)pset;
	return KERN_NOT_SUPPORTED;
}

kern_return_t _kernelrpc_mach_port_type(ipc_space_t task, mach_port_name_t name, mach_port_type_t *ptype) {
	(void)task; (void)name; (void)ptype;
	return KERN_NOT_SUPPORTED;
}
