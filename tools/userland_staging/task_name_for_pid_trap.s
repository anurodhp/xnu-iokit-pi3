// Real arm64 Mach trap stub for task_name_for_pid -- same class of gap,
// same real-macro-copied-by-hand fix, as mach_semaphore_traps.s/
// mach_port_allocate_trap.s. Real trap number cross-checked directly
// against osfmk/mach/syscall_sw.h:
//   kernel_trap(task_name_for_pid,-44,3)
// Real signature (osfmk/mach/mach_traps.h):
//   kern_return_t task_name_for_pid(mach_port_name_t target_tport, int pid, mach_port_name_t *tn);
// task_for_pid (trap -45) is deliberately NOT stubbed here: this
// project's real, current call-site set (third_party/PureDarwin's
// libvproc.c/SystemStarter/IPC.c) only ever calls task_name_for_pid, not
// task_for_pid.
.text
.align 2
.globl _task_name_for_pid
_task_name_for_pid:
    mov x16, #-44
    svc #0x80
    ret
