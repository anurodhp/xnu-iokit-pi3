// Real arm64 Mach trap stubs for 3 more "kernelrpc" mach_port_* traps,
// same real class as mach_port_traps.s (see that file's own header for
// the full rationale -- not reproduced here). Added 2026-08-27 while
// closing build_libdispatch_dylib.sh's own real remaining-gap list (see
// tools/init_binary/init.c's spawn_syslogd() comment): mach_port_traps.s
// covers 7 of the 10 real kernelrpc mach_port_* traps this project's
// call graph now needs; these 3 were never added because nothing linked
// before now actually called mach_port_insert_right/unguard/
// request_notification for real.
//
// Real trap numbers, cross-checked directly against
// third_party/xnu-7195/osfmk/mach/syscall_sw.h (not guessed):
//   _kernelrpc_mach_port_insert_right_trap         trap -21
//   _kernelrpc_mach_port_unguard_trap              trap -42
//   _kernelrpc_mach_port_request_notification_trap trap -77
//
// Same real arm64 kernel_trap shape as mach_port_traps.s: arg-count-
// agnostic (mov x16,#N / svc #0x80 / ret), result returned directly in
// x0, no carry-flag check (Mach-trap convention, not BSD-syscall).

.text
.align 2
.globl __kernelrpc_mach_port_insert_right_trap
__kernelrpc_mach_port_insert_right_trap:
    mov x16, #-21
    svc #0x80
    ret

.text
.align 2
.globl __kernelrpc_mach_port_unguard_trap
__kernelrpc_mach_port_unguard_trap:
    mov x16, #-42
    svc #0x80
    ret

.text
.align 2
.globl __kernelrpc_mach_port_request_notification_trap
__kernelrpc_mach_port_request_notification_trap:
    mov x16, #-77
    svc #0x80
    ret
