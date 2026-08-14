// Real arm64 Mach trap stubs for the two traps real libplatform/src/os/lock.c
// needs (thread_switch, mach_timebase_info_trap) that this project's
// libsyscall Perl generator (build_libsyscall_smoketest.sh) doesn't cover -
// that generator only walks bsd/kern/syscalls.master (BSD syscalls); Mach
// traps come from a separate table (osfmk/kern/syscall_sw.c) and Apple's
// own libsyscall ships them as hand/macro-written .s, not generated.
//
// This is the EXACT real macro Apple's own
// third_party/xnu-7195/osfmk/mach/arm/syscall_sw.h expands to for arm64
// (see that file's `kernel_trap` macro, __arm64__ branch) - copied by hand
// here rather than pulled in via the macro/header machinery because that
// header is guarded for kernel-internal trap-table generation use, not a
// plain includable userland stub source. Real trap numbers, cross-checked
// directly against osfmk/mach/syscall_sw.h (not guessed):
//   thread_switch            trap -61  (kernel_trap(syscall_thread_switch,-61,3))
//   mach_timebase_info_trap  trap -89  (kernel_trap(mach_timebase_info_trap,-89,1))
// SWI_SYSCALL = 0x80 (osfmk/mach/arm/vm_param.h) - same `svc #0x80` this
// project's raw BSD-syscall wrappers already use elsewhere (init.c,
// libc_gen_syscalls.c etc); Mach traps use the SAME svc instruction and
// trap-number-in-x16 dispatch, but unlike BSD syscalls, return their
// result directly in x0 with NO carry-flag error convention (that
// convention is BSD-only - osfmk/arm64/sleh.c's handle_svc() branches on
// trap_no sign before ever reaching cerror-style handling for negative/
// Mach trap numbers).

.text
.align 2
.globl _thread_switch
_thread_switch:
    mov x16, #-61
    svc #0x80
    ret

.text
.align 2
.globl _mach_timebase_info_trap
_mach_timebase_info_trap:
    mov x16, #-89
    svc #0x80
    ret
