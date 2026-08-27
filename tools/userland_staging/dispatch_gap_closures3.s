// Real arm64 Mach trap stub for semaphore_timedwait_trap (-38), same
// class/shape as this project's own existing mach_semaphore_traps.s
// (semaphore_signal_trap/-33, semaphore_wait_trap/-36 -- see that
// file's own header). Real trap number cross-checked directly against
// osfmk/mach/syscall_sw.h (not guessed):
//   kernel_trap(semaphore_timedwait_trap,-38,3)
// Mach-trap convention: arg-count-agnostic, result returned directly in
// x0, no carry-flag check (unlike a BSD syscall).
.text
.align 2
.globl _semaphore_timedwait_trap
_semaphore_timedwait_trap:
    mov x16, #-38
    svc #0x80
    ret
