#ifndef _MACH_MACH_VM_H_DISPATCH_COMPAT_
#define _MACH_MACH_VM_H_DISPATCH_COMPAT_
/* Real iPhoneOS SDK's own <mach/mach_vm.h> is literally "#error
 * mach_vm.h unsupported." -- same real gap build_libmalloc.sh's own
 * libc_build/compat_include/mach/mach_vm.h already solved (see that
 * file's header comment for the full story). Not reusing that file
 * directly here: putting the whole compat_include/ directory on
 * libdispatch's include path would also shadow real
 * third_party/Libc/os/assumes.h with compat_include/os/assumes.h -- a
 * deliberately empty stand-in for a DIFFERENT, unrelated consumer
 * (vfprintf.c) that libdispatch's real eventlink.c genuinely needs
 * (os_assert). Small, separate copy of the same 3 real declarations
 * libdispatch/src/*.c actually calls (mach_vm_allocate/mach_vm_deallocate/
 * mach_vm_map -- confirmed via grep), real signatures copied verbatim
 * from third_party/xnu-7195/libsyscall/mach/mach_vm.c, already fully
 * built into libc_build/libmalloc.a; linking against that existing
 * archive satisfies these at link time, this header only satisfies the
 * compiler. */
kern_return_t mach_vm_allocate(mach_port_name_t target, mach_vm_address_t *address, mach_vm_size_t size, int flags);
kern_return_t mach_vm_deallocate(mach_port_name_t target, mach_vm_address_t address, mach_vm_size_t size);
kern_return_t mach_vm_map(mach_port_name_t target, mach_vm_address_t *address, mach_vm_size_t size, mach_vm_offset_t mask, int flags, mem_entry_name_port_t object, memory_object_offset_t offset, boolean_t copy, vm_prot_t cur_protection, vm_prot_t max_protection, vm_inherit_t inheritance);
#endif
