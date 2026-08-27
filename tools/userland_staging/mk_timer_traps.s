// Real arm64 Mach trap stubs for the mk_timer_* family CFRunLoop.c's
// timer sources need -- plain traps, no MIG fallback at all (unlike
// mach_port_*/mach_vm_*, real Apple's own libsyscall/mach/mk_timer.c
// has no _kernelrpc_ variant to fall back to; confirmed absent by grep).
// Same real gap class and same real recipe as libmalloc_vm_traps.s/
// mach_port_traps.s (see either file's own header comment for the full
// rationale): not produced by this project's BSD-syscall Perl generator,
// Apple ships these as macro-expanded .s, hand-copied from the same
// real osfmk/mach/arm/syscall_sw.h `kernel_trap` macro.
//
// Real trap numbers, cross-checked directly against
// osfmk/mach/syscall_sw.h (not guessed), kernel implementation confirmed
// real (not a stub) in osfmk/kern/mk_timer.c:
//   mk_timer_create   trap -91
//   mk_timer_destroy  trap -92
//   mk_timer_arm      trap -93
//   mk_timer_cancel   trap -94
// (mk_timer_arm_leeway, trap -95, not needed -- confirmed absent from
// CF's own real trial-link undefined-symbol list.)

.text
.align 2
.globl _mk_timer_create
_mk_timer_create:
    mov x16, #-91
    svc #0x80
    ret

.text
.align 2
.globl _mk_timer_destroy
_mk_timer_destroy:
    mov x16, #-92
    svc #0x80
    ret

.text
.align 2
.globl _mk_timer_arm
_mk_timer_arm:
    mov x16, #-93
    svc #0x80
    ret

.text
.align 2
.globl _mk_timer_cancel
_mk_timer_cancel:
    mov x16, #-94
    svc #0x80
    ret
