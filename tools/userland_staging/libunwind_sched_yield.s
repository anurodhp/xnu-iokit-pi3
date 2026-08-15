// Real arm64 Mach trap stub for swtch_pri (-59), same real macro/pattern
// as libplatform_mach_traps.s's thread_switch/mach_timebase_info_trap
// (see that file's header comment for the full kernel_trap-macro
// rationale - not repeated here). Real trap number cross-checked against
// third_party/xnu-7195/osfmk/mach/syscall_sw.h:140
// (`kernel_trap(swtch_pri,-59,1)`), not guessed.
//
// Needed because real third_party/libpthread/src/pthread_rwlock.c calls
// sched_yield() (pthread.c:1872-1877's real one-liner: `swtch_pri(0);
// return 0;`) in its real contended-lock backoff path - this project's
// libpthread tier (build_libpthread.sh) deliberately didn't compile
// pthread.c itself (see that script's header comment), so this one real
// exported symbol pthread.c would have provided is supplied here instead,
// at the point it's first actually needed (libunwind's RWMutex is this
// project's first real caller of pthread_rwlock_wrlock's contended path).
.text
.align 2
.globl _swtch_pri
_swtch_pri:
    mov x16, #-59
    svc #0x80
    ret
