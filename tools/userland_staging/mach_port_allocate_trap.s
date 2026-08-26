// Real arm64 Mach trap stub for _kernelrpc_mach_port_allocate_trap (-16)
// -- same class of gap, same real-macro-copied-by-hand fix, as
// mach_semaphore_traps.s/mach_ipc_traps.s. Real trap number cross-
// checked directly against osfmk/mach/syscall_sw.h:
//   kernel_trap(_kernelrpc_mach_port_allocate_trap,-16,3)
// Needed by third_party/xnu-7195/libsyscall/mach/mach_port.c's real
// mach_port_allocate() (compiled as-is by build_libxpc.sh) -- it tries
// this real fast-path trap first, falling back to the (not yet ported
// here) full MIG routine only were the trap itself to return
// KERN_NOT_SUPPORTED, which real, current xnu never does for this call.
.text
.align 2
.globl __kernelrpc_mach_port_allocate_trap
__kernelrpc_mach_port_allocate_trap:
    mov x16, #-16
    svc #0x80
    ret
