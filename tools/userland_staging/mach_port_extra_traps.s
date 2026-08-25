// Real arm64 Mach trap stubs for the 2 NEW traps IODataQueueClient.c's
// real IODataQueueAllocateNotificationPort() needs
// (_kernelrpc_mach_port_allocate_trap, _kernelrpc_mach_port_get_
// attributes_trap) that no prior tier's build script generated yet.
// Same class of gap, same real-macro-copied-by-hand fix, as
// libplatform_mach_traps.s/libmalloc_vm_traps.s/mach_ipc_traps.s
// (already established precedent -- see those files' own header
// comments) and this project's own iokit_trap.s (same pass).
// libmalloc_vm_traps.s already provides the sibling
// _kernelrpc_mach_port_deallocate_trap (-18) and _kernelrpc_mach_port_
// mod_refs_trap (-19), already part of libc_build/libmalloc.a -- NOT
// redefined here (duplicate-symbol link error otherwise).
//
// Real trap numbers, cross-checked (not guessed) against
// third_party/xnu-7195/osfmk/kern/syscall_sw.c:
//   /* 16 */ MACH_TRAP(_kernelrpc_mach_port_allocate_trap, 3, 3, munge_www)
//   /* 40 */ MACH_TRAP(_kernelrpc_mach_port_get_attributes_trap, 5, 5, munge_wwwww)
// i.e. userland trap numbers -16 and -40, matching real
// third_party/xnu-7195/osfmk/mach/mach_traps.h's own prototypes for both
// (used directly by mach_port_wrappers_real.c's curated
// mach_port_allocate()/mach_port_get_attributes() bodies -- see that
// file's own header comment).
//
// Same real facts already established elsewhere in this project: arm64's
// kernel_trap macro is arg-count-agnostic (num_args is kernel-side stack-
// unwinding metadata only), SWI_SYSCALL = 0x80, Mach traps return their
// result directly in x0 with no BSD-style carry-flag error convention.

.text
.align 2
.globl __kernelrpc_mach_port_allocate_trap
__kernelrpc_mach_port_allocate_trap:
    mov x16, #-16
    svc #0x80
    ret

.text
.align 2
.globl __kernelrpc_mach_port_get_attributes_trap
__kernelrpc_mach_port_get_attributes_trap:
    mov x16, #-40
    svc #0x80
    ret
