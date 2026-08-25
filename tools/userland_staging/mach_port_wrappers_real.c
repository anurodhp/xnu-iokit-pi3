/* Real, verbatim-copied wrapper bodies for the mach_port_*() calls
 * IODataQueueClient.c's real IODataQueueAllocateNotificationPort() needs
 * that this project's existing libc_build/libmalloc.a doesn't already
 * provide: mach_port_allocate, mach_port_get_attributes,
 * mach_port_set_attributes. (mach_port_deallocate and mach_port_mod_refs
 * are NOT redefined here -- build_libmalloc.sh's libmalloc_dispatch.o
 * already provides real bodies for both, already part of
 * libc_build/libmalloc.a; a first version of this file duplicated them
 * and hit a real "duplicate symbol" link error, confirming they were
 * already there rather than assuming it.)
 *
 * Curated (not the full file) from
 * third_party/xnu-7195/libsyscall/mach/mach_port.c, same "curated down
 * to just what's needed" precedent already established twice in this
 * project (build_libmach_ipc.sh's mach_host_self_real.c from
 * mach_legacy.c, mach_msg_real.c from mach_msg.c): the full real
 * mach_port.c (795 lines, ~35 routines) needs <mach/mach_port_internal.h>
 * (a mig-"-header"-generated file under a private install name -- not
 * committed source, see build_libmalloc.sh's mach_vm_internal.h
 * precedent for the exact same pattern), <mach/mach_vm.h>, <mach/
 * mach_sync_ipc.h>, and "tsd.h" for routines this project doesn't call
 * at all yet; pulling all of that in just to reach these functions was a
 * real rabbit hole not worth taking for this pass. Every line of actual
 * logic below is character-for-character identical to the real file
 * (confirmed by direct comparison while writing this) -- only the
 * surrounding #includes/file scope changed.
 *
 * Real behavior preserved exactly, including the real trap-then-MIG-
 * fallback pattern every one of Apple's "kernelrpc" wrappers uses:
 * mach_port_allocate tries a fast Mach trap first (_kernelrpc_*_trap,
 * hand-written stubs in mach_port_extra_traps.s), falling back to the
 * full MIG call (_kernelrpc_mach_port_*, from mach_portUser.c, this
 * project's own fresh mig-regeneration of osfmk/mach/mach_port.defs)
 * only if the trap returns MACH_SEND_INVALID_DEST (meaning the kernel
 * doesn't implement that trap number -- an old-OS compatibility path,
 * not expected to trigger on this project's real from-source kernel,
 * which does implement this trap number unconditionally -- confirmed
 * present in osfmk/kern/syscall_sw.c). mach_port_set_attributes has no
 * trap variant in real Apple source either -- MIG-only, matching the
 * real file exactly.
 */
#include <mach/mach_traps.h>
#include "mach_port.h" /* this project's own mig-regenerated MIG stub
                         * prototypes (_kernelrpc_mach_port_allocate,
                         * _kernelrpc_mach_port_get_attributes,
                         * _kernelrpc_mach_port_set_attributes) -- built
                         * by build_iokituser.sh from
                         * osfmk/mach/mach_port.defs, same file
                         * mach_portUser.c itself is generated from. */

kern_return_t
mach_port_allocate(
	ipc_space_t task,
	mach_port_right_t right,
	mach_port_name_t *name)
{
	kern_return_t rv;

	rv = _kernelrpc_mach_port_allocate_trap(task, right, name);

	if (rv == MACH_SEND_INVALID_DEST) {
		rv = _kernelrpc_mach_port_allocate(task, right, name);
	}

	return rv;
}

kern_return_t
mach_port_get_attributes(
	ipc_space_t task,
	mach_port_name_t name,
	mach_port_flavor_t flavor,
	mach_port_info_t port_info_out,
	mach_msg_type_number_t *port_info_outCnt)
{
	kern_return_t rv;

	rv = _kernelrpc_mach_port_get_attributes_trap(task, name, flavor,
	    port_info_out, port_info_outCnt);

	/* Real file's #ifdef __x86_64__/__i386__ legacy-trap-number-quirk
	 * block omitted here -- this project is arm64-only, and that block
	 * is itself arch-gated out on arm64 in the real file too. */

	if (rv == MACH_SEND_INVALID_DEST) {
		rv = _kernelrpc_mach_port_get_attributes(task, name, flavor,
		    port_info_out, port_info_outCnt);
	}

	return rv;
}

kern_return_t
mach_port_set_attributes(
	ipc_space_t task,
	mach_port_name_t name,
	mach_port_flavor_t flavor,
	mach_port_info_t port_info,
	mach_msg_type_number_t port_infoCnt)
{
	kern_return_t rv;

	rv = _kernelrpc_mach_port_set_attributes(task, name, flavor,
	    port_info, port_infoCnt);

	return rv;
}
