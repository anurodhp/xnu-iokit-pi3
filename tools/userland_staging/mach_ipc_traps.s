// Real arm64 Mach trap stubs for the two NEW traps Phase 4.6 item 5's
// real Mach IPC work needs (mach_msg_trap, host_self_trap) that no prior
// tier's build script generated yet -- same class of gap, and the exact
// same real-macro-copied-by-hand fix, as libplatform_mach_traps.s
// (thread_switch/mach_timebase_info_trap) and libmalloc_vm_traps.s (the
// _kernelrpc_mach_vm_*/_kernelrpc_mach_port_*/task_self_trap/
// mach_reply_port traps) already established: Apple's own libsyscall
// build generates these via a Perl/macro pipeline over
// osfmk/mach/syscall_sw.h that this project doesn't run for Mach traps
// (only for BSD syscalls, via create-syscalls.pl); real trap numbers
// cross-checked directly against osfmk/mach/syscall_sw.h (not guessed):
//   mach_msg_trap    trap -31  (kernel_trap(mach_msg_trap,-31,7))
//   host_self_trap   trap -29  (kernel_trap(host_self_trap,-29,0))
// task_self_trap (-28) and mach_reply_port (-26) are NOT redefined here
// -- both already exist in libmalloc_vm_traps.s, already linked into
// libc_build/libmalloc.a (and, via that static archive, pulled into any
// consumer -- ps.macho, simple_cmds -- that references them), reused
// as-is rather than duplicated (a duplicate-symbol link error otherwise).
//
// arm64's kernel_trap macro is arg-count-agnostic (num_args accepted but
// never referenced in the __arm64__ branch -- see libmalloc_vm_traps.s's
// own comment for the full detail): mach_msg_trap's real 7 arguments
// (msg, option, send_size, rcv_size, rcv_name, timeout, notify) all fit
// directly in x0-x6, well within arm64 AAPCS's x0-x7 integer/pointer
// argument registers, so the same trivial 3-instruction
// mov x16,#N / svc #0x80 / ret sequence used by every other trap in this
// project applies unchanged. SWI_SYSCALL = 0x80 (osfmk/mach/arm/
// vm_param.h), same as elsewhere in this project. Mach traps return
// their result directly in x0, no carry-flag check (that convention is
// BSD-syscall-only -- osfmk/arm64/sleh.c's handle_svc() branches on
// trap_no sign before ever reaching cerror-style handling).

.text
.align 2
.globl _mach_msg_trap
_mach_msg_trap:
    mov x16, #-31
    svc #0x80
    ret

.text
.align 2
.globl _host_self_trap
_host_self_trap:
    mov x16, #-29
    svc #0x80
    ret

// mach_msg_overwrite_trap: trap -32 (kernel_trap(mach_msg_overwrite_trap,-32,8),
// osfmk/mach/syscall_sw.c) -- added 2026-08-25 for real dyld-940's
// DyldAPIs.cpp (RemoteNotificationResponder::sendMessage(), the first
// real consumer of mach_msg_overwrite() this project has had -- see
// mach_msg_real.c's own header comment). Real signature has 9 C
// parameters (one more than fits in x0-x7), but per this file's own
// header comment arm64's kernel_trap macro never reads num_args in the
// __arm64__ branch -- the caller's own AAPCS64 argument setup (registers
// x0-x7 plus one stack slot) is already correct before control reaches
// this trivial stub, so the same unmodified 3-instruction shape applies
// regardless of argument count.
.text
.align 2
.globl _mach_msg_overwrite_trap
_mach_msg_overwrite_trap:
    mov x16, #-32
    svc #0x80
    ret

// task_for_pid: trap -45 (kernel_trap(task_for_pid,-45,3), osfmk/mach/
// syscall_sw.h) -- added 2026-09-04 for DAR-143 (real `ps` CPU%/RSS/
// VSZ/thread columns, via real adv_cmds/ps/tasks.c's get_task_info()).
// Real 3-arg signature (target_tport, pid, *t) fits directly in x0-x2;
// same trivial 3-instruction shape as every other trap in this file --
// confirmed real and already wired into this exact kernel fork's trap
// table (osfmk/kern/syscall_sw.c line 150: `MACH_TRAP(task_for_pid, 3,
// 3, munge_www)`), so no kernel change needed, pure userland wiring.
.text
.align 2
.globl _task_for_pid
_task_for_pid:
    mov x16, #-45
    svc #0x80
    ret
