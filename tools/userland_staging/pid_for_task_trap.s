// Real arm64 Mach trap stub for pid_for_task (-46, 2 args), same real
// kernel_trap macro pattern as libunwind_sched_yield.s's own swtch_pri
// (-59) -- see that file's header for the full kernel_trap-macro
// rationale, not repeated here. Real trap number cross-checked against
// third_party/xnu-7195/osfmk/mach/syscall_sw.h:124
// (`kernel_trap(pid_for_task,-46,2)`), not guessed. Mach traps return
// their real kern_return_t directly in x0 -- no cerror/errno
// translation (that convention is BSD-syscall-specific, not used here).
.text
.align 2
.globl _pid_for_task
_pid_for_task:
    mov x16, #-46
    svc #0x80
    ret
