/* Real Apple third_party/xnu-7195/libsyscall/mach/mach_vm.c (unmodified)
 * calls these non-trap _kernelrpc_mach_vm_* / _kernelrpc_vm_* MIG fallback
 * functions ONLY when its trap fast-path returns MACH_SEND_INVALID_DEST
 * (see mach_vm_allocate() etc's own bodies) -- i.e. only if the kernel
 * doesn't support the corresponding trap at all. This project's kernel
 * genuinely does support every trap these functions guard (real,
 * verified trap numbers -- see libmalloc_vm_traps.s -- same class
 * already proven working elsewhere in this project's Phase 4 work), so
 * these bodies are real but provably unreachable dead code on this
 * target. Declared via build_libmalloc.sh's real `mig -header` output
 * (from osfmk/mach/mach_vm.defs and osfmk/mach/vm_map.defs -- the exact
 * same tool and source Apple's own build uses to produce the header
 * mach_vm.c expects, genuinely absent from this project's checkout as a
 * committed file -- see build_libmalloc.sh's header comment) rather than
 * a hand-typed prototype list, so the signatures are guaranteed to match
 * exactly. Defined honestly here (KERN_FAILURE, not a silent
 * fake-success) rather than left undefined, since real mach_msg()-based
 * MIG RPC (what a genuine implementation would need) is a materially
 * larger, separate subsystem this project hasn't built. */
#include <mach/kern_return.h>
#include <mach/mach_types.h>
#include <mach/vm_types.h>
#include <mach/memory_object_types.h>

kern_return_t _kernelrpc_mach_vm_allocate(mach_port_name_t target, mach_vm_address_t *address, mach_vm_size_t size, int flags) {
    (void)target; (void)address; (void)size; (void)flags;
    return KERN_FAILURE;
}
kern_return_t _kernelrpc_mach_vm_deallocate(mach_port_name_t target, mach_vm_address_t address, mach_vm_size_t size) {
    (void)target; (void)address; (void)size;
    return KERN_FAILURE;
}
kern_return_t _kernelrpc_mach_vm_protect(mach_port_name_t task, mach_vm_address_t address, mach_vm_size_t size, boolean_t set_maximum, vm_prot_t new_protection) {
    (void)task; (void)address; (void)size; (void)set_maximum; (void)new_protection;
    return KERN_FAILURE;
}
kern_return_t _kernelrpc_mach_vm_map(mach_port_name_t target, mach_vm_address_t *address, mach_vm_size_t size, mach_vm_offset_t mask, int flags, mem_entry_name_port_t object, memory_object_offset_t offset, boolean_t copy, vm_prot_t cur_protection, vm_prot_t max_protection, vm_inherit_t inheritance) {
    (void)target; (void)address; (void)size; (void)mask; (void)flags; (void)object; (void)offset; (void)copy; (void)cur_protection; (void)max_protection; (void)inheritance;
    return KERN_FAILURE;
}
kern_return_t _kernelrpc_mach_vm_remap(mach_port_name_t target, mach_vm_address_t *address, mach_vm_size_t size, mach_vm_offset_t mask, int flags, mach_port_name_t src_task, mach_vm_address_t src_address, boolean_t copy, vm_prot_t *cur_protection, vm_prot_t *max_protection, vm_inherit_t inheritance) {
    (void)target; (void)address; (void)size; (void)mask; (void)flags; (void)src_task; (void)src_address; (void)copy; (void)cur_protection; (void)max_protection; (void)inheritance;
    return KERN_FAILURE;
}
kern_return_t _kernelrpc_mach_vm_read(mach_port_name_t target, mach_vm_address_t address, mach_vm_size_t size, vm_offset_t *data, mach_msg_type_number_t *dataCnt) {
    (void)target; (void)address; (void)size; (void)data; (void)dataCnt;
    return KERN_FAILURE;
}
kern_return_t _kernelrpc_mach_vm_purgable_control(mach_port_name_t target, mach_vm_offset_t address, vm_purgable_t control, int *state) {
    (void)target; (void)address; (void)control; (void)state;
    return KERN_FAILURE;
}
kern_return_t _kernelrpc_vm_map(mach_port_name_t target, vm_address_t *address, vm_size_t size, vm_offset_t mask, int flags, mem_entry_name_port_t object, vm_offset_t offset, boolean_t copy, vm_prot_t cur_protection, vm_prot_t max_protection, vm_inherit_t inheritance) {
    (void)target; (void)address; (void)size; (void)mask; (void)flags; (void)object; (void)offset; (void)copy; (void)cur_protection; (void)max_protection; (void)inheritance;
    return KERN_FAILURE;
}
kern_return_t _kernelrpc_vm_remap(mach_port_name_t target, vm_address_t *address, vm_size_t size, vm_offset_t mask, int flags, mach_port_name_t src_task, vm_address_t src_address, boolean_t copy, vm_prot_t *cur_protection, vm_prot_t *max_protection, vm_inherit_t inheritance) {
    (void)target; (void)address; (void)size; (void)mask; (void)flags; (void)src_task; (void)src_address; (void)copy; (void)cur_protection; (void)max_protection; (void)inheritance;
    return KERN_FAILURE;
}
kern_return_t _kernelrpc_vm_read(mach_port_name_t target, vm_address_t address, vm_size_t size, vm_offset_t *data, mach_msg_type_number_t *dataCnt) {
    (void)target; (void)address; (void)size; (void)data; (void)dataCnt;
    return KERN_FAILURE;
}
