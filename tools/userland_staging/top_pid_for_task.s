// Real Mach trap stub: kernel_trap(pid_for_task,-46,2), from
// third_party/xnu-7195/osfmk/mach/syscall_sw.h:124. Same arm64 shape as
// mach_port_traps.s (mov x16,#N / svc #0x80 / ret). libsystem_kernel.dylib
// does not export it; top's libtop.c calls it (libtop_p_task_update).
.text
.align 2
.globl _pid_for_task
_pid_for_task:
    mov x16, #-46
    svc #0x80
    ret
