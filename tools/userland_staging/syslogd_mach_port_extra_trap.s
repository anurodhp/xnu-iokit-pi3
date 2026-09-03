// Real arm64 Mach trap stub for _kernelrpc_mach_port_move_member_trap
// (PureDarwin's real notify_client.c needs the public mach_port_move_
// member() wrapper -- see syslogd_mach_port_compat.c). Real trap number
// -20, cross-checked (not guessed) against
// third_party/xnu-7195/osfmk/kern/syscall_sw.c:
//   /* 20 */ MACH_TRAP(_kernelrpc_mach_port_move_member_trap, 3, 3, munge_www)
// Same real arm64 kernel_trap shape (arg-count-agnostic, SWI_SYSCALL =
// 0x80, direct kern_return_t in x0, no carry-flag check) already
// established by mach_port_traps.s/mach_port_extra_traps.s -- not a new
// pattern, just one more real trap number this project hadn't needed a
// stub for yet.
.text
.align 2
.globl __kernelrpc_mach_port_move_member_trap
__kernelrpc_mach_port_move_member_trap:
    mov x16, #-20
    svc #0x80
    ret

// __kernelrpc_mach_port_destruct_trap (trap -25) REMOVED 2026-09-03
// (DAR-24): libsystem_kernel.dylib now really exports it (confirmed via
// dyld_info -exports, not guessed) -- syslogd.macho already loads that
// dylib, so the reference binds there instead of shadowing it. See
// syslogd_mach_port_compat.c's own updated comment for the C-level half
// of this same removal.
