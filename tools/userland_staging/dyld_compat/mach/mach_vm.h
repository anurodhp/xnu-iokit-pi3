/* Real, already-established gap: this project's pinned iPhoneOS SDK's
 * own <mach/mach_vm.h> is literally "#error mach_vm.h unsupported"
 * (embedded-only SDKs never shipped the macOS-only mach_vm family) --
 * same real gap build_libpthread.sh's own compat_include/mach/
 * mach_vm.h already documents and works around. Small, hand-typed
 * declaration for the one function libsyscall/wrappers/kdebug_trace.c
 * needs (mach_vm_deallocate, in its typefilter-optimization path) --
 * real signature, not guessed.
 */
#ifndef _COMPAT_MACH_VM_H_
#define _COMPAT_MACH_VM_H_
#include <mach/mach_types.h>
#include <mach/vm_types.h>
kern_return_t mach_vm_deallocate(mach_port_name_t target, mach_vm_address_t address, mach_vm_size_t size);
#endif
