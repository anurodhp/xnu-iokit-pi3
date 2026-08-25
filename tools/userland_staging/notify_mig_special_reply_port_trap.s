// Real arm64 Mach trap stub for thread_get_special_reply_port (trap -50),
// needed by notify_mig_special_reply_port.c's real
// mig_get_special_reply_port() body (curated from
// third_party/xnu-7195/libsyscall/mach/mach_port.c:453-465). Real trap
// number cross-checked directly against
// third_party/xnu-7195/osfmk/mach/syscall_sw.h:133
// (`kernel_trap(thread_get_special_reply_port,-50,0)`) and
// osfmk/kern/syscall_sw.c:155 (`/* 50 */ MACH_TRAP(thread_get_special_reply_port, 0, 0, NULL)`)
// -- not guessed. Same trivial 3-instruction sequence every other Mach
// trap stub in this project uses (see mach_ipc_traps.s's own header
// comment for the full arm64 kernel_trap-macro reasoning: arg-count-
// agnostic, no carry-flag check, Mach traps return their result directly
// in x0).
.text
.align 2
.globl _thread_get_special_reply_port
_thread_get_special_reply_port:
    mov x16, #-50
    svc #0x80
    ret
