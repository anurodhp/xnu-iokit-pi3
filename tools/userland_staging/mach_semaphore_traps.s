// Real arm64 Mach trap stubs for semaphore_signal_trap (-33) and
// semaphore_wait_trap (-36) -- same class of gap, same real-macro-
// copied-by-hand fix, as mach_ipc_traps.s (mach_msg_trap/host_self_trap)
// and libmalloc_vm_traps.s. Real trap numbers cross-checked directly
// against osfmk/mach/syscall_sw.h (not guessed):
//   semaphore_signal_trap   trap -33  (kernel_trap(semaphore_signal_trap,-33,1))
//   semaphore_wait_trap     trap -36  (kernel_trap(semaphore_wait_trap,-36,1))
// Needed by third_party/xnu-7195/libsyscall/mach/semaphore.c's own real
// semaphore_signal()/semaphore_wait() bodies (compiled as-is by
// build_libxpc.sh, see that script's own header comment) -- this
// project's existing semaphore_create() support
// (mach_ipc_clock_init.c) never needed these two traps since it only
// creates a semaphore, never waits on or signals one directly itself.
.text
.align 2
.globl _semaphore_signal_trap
_semaphore_signal_trap:
    mov x16, #-33
    svc #0x80
    ret

.globl _semaphore_wait_trap
_semaphore_wait_trap:
    mov x16, #-36
    svc #0x80
    ret
