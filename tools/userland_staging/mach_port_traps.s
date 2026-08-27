// Real arm64 Mach trap stubs for the 7 "kernelrpc" mach_port_* traps
// third_party/xnu-7195/libsyscall/mach/mach_port.c needs (it tries each
// trap first and only falls back to the non-trap _kernelrpc_mach_port_*()
// MIG path on MACH_SEND_INVALID_DEST/KERN_NOT_SUPPORTED) -- same real
// gap class as libmalloc_vm_traps.s's own mach_vm_* traps (see that
// file's own header comment for the full rationale: not produced by
// this project's BSD-syscall Perl generator, Apple ships these as
// macro-expanded .s not includable userland stub source, so hand-copied
// from the same real osfmk/mach/arm/syscall_sw.h `kernel_trap` macro).
//
// Real trap numbers, cross-checked directly against
// osfmk/mach/syscall_sw.h (not guessed):
//   _kernelrpc_mach_port_allocate_trap        trap -16
//   _kernelrpc_mach_port_insert_member_trap   trap -22
//   _kernelrpc_mach_port_extract_member_trap  trap -23
//   _kernelrpc_mach_port_construct_trap       trap -24
//   _kernelrpc_mach_port_destruct_trap        trap -25
//   _kernelrpc_mach_port_get_attributes_trap  trap -40
//   _kernelrpc_mach_port_type_trap            trap -76
// (mach_port_deallocate_trap/-18 and mach_port_mod_refs_trap/-19 already
// exist in libmalloc_vm_traps.s -- not duplicated here.)
//
// Same real arm64 kernel_trap shape as libmalloc_vm_traps.s: arg-count-
// agnostic (mov x16,#N / svc #0x80 / ret), result returned directly in
// x0, no carry-flag check (Mach-trap convention, not BSD-syscall).

.text
.align 2
.globl __kernelrpc_mach_port_allocate_trap
__kernelrpc_mach_port_allocate_trap:
    mov x16, #-16
    svc #0x80
    ret

.text
.align 2
.globl __kernelrpc_mach_port_insert_member_trap
__kernelrpc_mach_port_insert_member_trap:
    mov x16, #-22
    svc #0x80
    ret

.text
.align 2
.globl __kernelrpc_mach_port_extract_member_trap
__kernelrpc_mach_port_extract_member_trap:
    mov x16, #-23
    svc #0x80
    ret

.text
.align 2
.globl __kernelrpc_mach_port_construct_trap
__kernelrpc_mach_port_construct_trap:
    mov x16, #-24
    svc #0x80
    ret

.text
.align 2
.globl __kernelrpc_mach_port_destruct_trap
__kernelrpc_mach_port_destruct_trap:
    mov x16, #-25
    svc #0x80
    ret

.text
.align 2
.globl __kernelrpc_mach_port_get_attributes_trap
__kernelrpc_mach_port_get_attributes_trap:
    mov x16, #-40
    svc #0x80
    ret

.text
.align 2
.globl __kernelrpc_mach_port_type_trap
__kernelrpc_mach_port_type_trap:
    mov x16, #-76
    svc #0x80
    ret
