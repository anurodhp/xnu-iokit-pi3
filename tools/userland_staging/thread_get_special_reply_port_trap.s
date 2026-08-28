// Real arm64 Mach trap stub for thread_get_special_reply_port (-50, 0
// args) -- same real kernel_trap macro pattern as pid_for_task_trap.s
// (-46, 2 args), see that file's header for the full rationale. Real
// trap number cross-checked against
// third_party/xnu-7195/osfmk/mach/syscall_sw.h:133
// (`kernel_trap(thread_get_special_reply_port,-50,0)`), not guessed.
// Needed by libsyscall/mach/mach_port.c's real
// mig_get_special_reply_port() -- launchd's real MIG client calls hit
// this via the regenerated MIG _user.c stubs (build_launchd_core.sh).
.text
.align 2
.globl _thread_get_special_reply_port
_thread_get_special_reply_port:
    mov x16, #-50
    svc #0x80
    ret
