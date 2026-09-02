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
 * kernel_trap(-27)) returning a real mach port name.
 *
 * SUBSTITUTION REMOVED 2026-09-01 (DAR-153). This file used to store a
 * truncated thread_selfid() (BSD syscall 372) instead, on the stated
 * premise that "os_unfair_lock/os_once never dereference this value as
 * an actual Mach port ... so any real, stable, non-zero,
 * unique-per-thread value satisfies the real algorithm's correctness
 * requirement". That premise was WRONG, and provably so from real
 * libplatform source: lock.c:435 defines
 * `OS_ULOCK_OWNER(value) ((value) | OS_ULOCK_NOWAITERS_BIT)` -- bit 0 of
 * the lock word is the has-waiters flag, and the owner comparison
 * re-sets it before comparing against this slot. So the value must be
 * ODD, not merely non-zero. Real Mach port names always are (XNU's
 * ipc_entry_new_rollpoint() seeds the generation at IE_BITS_GEN_MASK +
 * IE_BITS_GEN_ONE and ipc_entry_new_gen() steps by IE_BITS_GEN_ONE, so a
 * name's low byte is always 3 mod 4 -- osfmk/ipc/ipc_entry.h:135-175);
 * a thread id is even half the time, which would make
 * os_unfair_lock_assert_owner() fire __LIBPLATFORM_CLIENT_CRASH__ on a
 * lock this thread really does hold. That exact crash (as a bare
 * `brk #1`/SIGTRAP out of Libnotify's _notify_lib_init_locked()) is what
 * DAR-153 root-caused, via the sibling zero-valued case -- see
 * dyld_tsd_entry_trampoline.s's own header for the full writeup.
 *
 * Now uses the real trap, matching real _pthread_main_thread_init()
 * (libpthread/src/pthread.c:1853). thread_self_trap is Mach trap -27,
 * the same raw trap libmalloc_vm_traps.s already exports and real
 * mach_legacy.c's mach_thread_self() wraps; inlined here rather than
 * called, so this file keeps its existing zero-undefined-symbol
 * property (libplatform.a has consumers that do not link libmalloc.a).
 *
 * Call libplatform_thread_self_bootstrap() exactly once, after
 * pthread_tsd_shim_bootstrap() (TPIDRRO_EL0 must already be live) and
 * before the first os_unfair_lock_lock()/os_once() call.
 */

typedef unsigned long size_t;

#include "../../third_party/xnu-7195/libsyscall/os/tsd.h"

/* Mach trap -27, real mach_thread_self()'s underlying trap. Mach traps
 * use x16 = negative trap number and return in x0; unlike a BSD syscall
 * there is no carry-flag error convention to check. */
static unsigned int real_mach_thread_self(void) {
    register long x0 asm("x0");
    register long x16 asm("x16") = -27;
    asm volatile("svc #0x80" : "=r"(x0) : "r"(x16) : "memory", "cc",
	    /* Mach traps preserve callee-saved registers only; x1-x15/x17 are
	     * caller-saved and the kernel's trap return makes no promise about
	     * them. Declared explicitly so this stays correct if these files
	     * are ever built above -O0 (today they are -O0, and a real
	     * disassembly confirms everything spills across the svc -- this is
	     * defensive, not a fix for a live bug). x16 is an input operand
	     * and x18 is platform-reserved, so neither may be listed. */
	    "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9",
	    "x10", "x11", "x12", "x13", "x14", "x15", "x17");
    return (unsigned int)x0;
}

int libplatform_thread_self_bootstrap(void) {
    unsigned int self32 = real_mach_thread_self();
    if (self32 == 0) {
        /* MACH_PORT_NULL. A live thread always has a real name for
         * itself, so this can only mean the trap failed - and 0 is
         * exactly OS_LOCK_NO_OWNER, the one value that must never be
         * stored here (see this file's header). */
        return 0;
    }
    _os_tsd_set_direct(__TSD_MACH_THREAD_SELF, (void *)(unsigned long)self32);
    return 1;
}
