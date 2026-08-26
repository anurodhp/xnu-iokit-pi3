// Real arm64 Mach trap stub for thread_get_special_reply_port -- same
// class of gap, same real-macro-copied-by-hand fix, as
// mach_semaphore_traps.s/mach_port_allocate_trap.s/
// task_name_for_pid_trap.s. Real trap number cross-checked directly
// against osfmk/mach/syscall_sw.h:
//   kernel_trap(thread_get_special_reply_port,-50,0)
// Needed by third_party/libdispatch's real src/mach.c (dispatch_mach's
// reply-port fast path).
.text
.align 2
.globl _thread_get_special_reply_port
_thread_get_special_reply_port:
    mov x16, #-50
    svc #0x80
    ret
