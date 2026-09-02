// Real arm64 Mach trap stub for task_for_pid, the one remaining real
// phantom-libdispatch-bound symbol from launchctl_syscall_gaps.c's own
// header writeup (real launchctl.c direct call, no wrapper anywhere in
// this checkout before this fix).
//
// Real trap number, cross-checked directly against
// osfmk/mach/syscall_sw.h (not guessed): kernel_trap(task_for_pid,-45,3)
// -- same real hand-copied-from-syscall_sw.h shape as
// tools/userland_staging/mach_port_traps.s's own 7 traps (see that
// file's own header comment for why: Apple ships these as
// macro-expanded .s, not includable userland stub source).
.text
.align 2
.globl _task_for_pid
_task_for_pid:
    mov x16, #-45
    svc #0x80
    ret
