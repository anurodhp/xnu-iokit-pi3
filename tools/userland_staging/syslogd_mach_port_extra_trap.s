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

// Real trap -25 (third_party/xnu-7195/osfmk/kern/syscall_sw.c:
//   /* 25 */ MACH_TRAP(_kernelrpc_mach_port_destruct_trap, 4, 5, munge_wwwl))
// Already exists as one of 7 traps bundled together in this project's
// own mach_port_traps.s, but that file's OTHER 6 traps (allocate/
// insert_member/extract_member/construct/get_attributes/type) would
// duplicate-conflict with copies already linked elsewhere (e.g.
// libxpc_syscalls.a's own __kernelrpc_mach_port_allocate_trap, found by
// this pass's own trial link) -- compiling that whole file isn't
// possible here. Real, narrow duplicate: just the one trap this pass
// actually needs.
.globl __kernelrpc_mach_port_destruct_trap
__kernelrpc_mach_port_destruct_trap:
    mov x16, #-25
    svc #0x80
    ret
