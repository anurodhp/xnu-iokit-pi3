#ifndef _MACH_MACH_VM_H_CF_COMPAT_
#define _MACH_MACH_VM_H_CF_COMPAT_
/* Real iPhoneOS SDK's own <mach/mach_vm.h> is literally "#error
 * mach_vm.h unsupported." -- same real gap build_libmalloc.sh's own
 * libc_build/compat_include/mach/mach_vm.h and
 * dispatch_compat_headers/mach/mach_vm.h already solved (see either
 * file's header comment for the full story). Not reusing
 * libc_build/compat_include/ directly here: that whole directory lives
 * under tools/userland_staging/libc_build/, which is entirely
 * gitignored build output (see .gitignore) -- fine for files a script
 * generates at build time, but this repo's own convention is "every
 * built artifact needs a reproducing script, checked in", and nothing
 * generates that hand-written header. Small, separate, TRACKED copy of
 * the real declarations CFUtilities.c actually calls
 * (mach_vm_allocate/_deallocate in its temp-memory helpers,
 * mach_vm_region in __CFFindPointer() -- confirmed via grep), real
 * signatures copied verbatim from
 * third_party/xnu-7195/osfmk/mach/mach_vm.defs's own routine
 * declarations; already fully built into libc_build/libmalloc.a, so
 * linking against that existing archive satisfies them at link time —
 * this header only satisfies the compiler.
 */
kern_return_t mach_vm_allocate(mach_port_name_t target, mach_vm_address_t *address, mach_vm_size_t size, int flags);
kern_return_t mach_vm_deallocate(mach_port_name_t target, mach_vm_address_t address, mach_vm_size_t size);
kern_return_t mach_vm_region(vm_map_read_t target_task, mach_vm_address_t *address, mach_vm_size_t *size, vm_region_flavor_t flavor, vm_region_info_t info, mach_msg_type_number_t *infoCnt, mach_port_t *object_name);
#endif
