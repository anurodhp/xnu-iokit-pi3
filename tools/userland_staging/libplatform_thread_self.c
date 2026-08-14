/* Populates the one real per-thread value real libplatform/src/os/lock.c
 * needs that this target has never had a source for: the TSD slot real
 * pthread_start() fills in on every real Darwin process before any lock
 * is ever taken (__TSD_MACH_THREAD_SELF, slot 3 -
 * third_party/xnu-7195/libsyscall/os/tsd.h; also
 * third_party/libpthread/private/pthread/tsd_private.h's
 * _PTHREAD_TSD_SLOT_MACH_THREAD_SELF, same slot, type mach_port_t).
 * os_unfair_lock_lock()/os_once() (real, unmodified libplatform source -
 * see build_libplatform.sh) read this slot via _os_tsd_get_direct() as
 * their lock "owner" identity; on this target it has never been written,
 * so it read as zero == OS_LOCK_NO_OWNER, which would make every lock
 * acquire silently corrupt (cmpxchg from "unowned" to "owned by 0" is a
 * no-op, so two different callers holding a lock would both see
 * themselves as already the owner).
 *
 * Real Darwin's value here comes from a Mach trap (mach_thread_self,
 * kernel_trap(-27)) returning a real mach port name. This project
 * substitutes a different, ALSO real and ALSO non-zero-for-any-live-
 * thread value instead: thread_selfid() (BSD syscall 372, NO_SYSCALL_STUB
 * - real number cross-checked against
 * third_party/xnu-7195/bsd/kern/syscalls.master), truncated to the low
 * 32 bits to fit mach_port_name_t's width. This is a deliberate,
 * documented substitution, not a guess: os_unfair_lock/os_once never
 * dereference this value as an actual Mach port (no msg send, no
 * deallocate) - they only ever compare it for equality against "am I the
 * current owner", so any real, stable, non-zero, unique-per-thread value
 * satisfies the real algorithm's correctness requirement. thread_selfid()
 * needed no new Mach-trap ABI investigation (it's a plain BSD syscall,
 * same carry-flag convention as every other raw syscall in this project),
 * unlike mach_thread_self's Mach-trap ABI (see libplatform_mach_traps.s
 * for the two Mach traps this build DID need to add - thread_switch and
 * mach_timebase_info_trap, only used by lock.c's OSSpinLock/handoff-lock
 * legacy paths, not by os_unfair_lock/os_once).
 *
 * Call libplatform_thread_self_bootstrap() exactly once, after
 * pthread_tsd_shim_bootstrap() (TPIDRRO_EL0 must already be live) and
 * before the first os_unfair_lock_lock()/os_once() call.
 */

typedef unsigned long size_t;

#include "../../third_party/xnu-7195/libsyscall/os/tsd.h"

static inline long syscall0(long num) {
    register long x0 asm("x0");
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "=r"(x0), "=r"(is_error)
                 : "r"(x16)
                 : "memory", "cc");
    (void)is_error; /* thread_selfid() cannot fail per its real contract */
    return x0;
}

/* #372 AUE_NULL - real number, cross-checked against syscalls.master
 * (see this file's header comment). */
static unsigned long long real_thread_selfid(void) {
    return (unsigned long long)syscall0(372);
}

int libplatform_thread_self_bootstrap(void) {
    unsigned int self32 = (unsigned int)real_thread_selfid();
    if (self32 == 0) {
        /* Real thread IDs are allocated starting well above zero and
         * never recycle to zero for a live kernel - if this ever
         * triggers it means thread_selfid() itself failed silently,
         * not a value we can safely treat as OS_LOCK_NO_OWNER. */
        return 0;
    }
    _os_tsd_set_direct(__TSD_MACH_THREAD_SELF, (void *)(unsigned long)self32);
    return 1;
}
