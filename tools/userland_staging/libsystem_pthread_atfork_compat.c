/* Real gap, ROOT-CAUSED AND FIXED 2026-09-01 (DAR-163): real, unmodified
 * pthread_atfork.c's __pthread_atfork_child() (the fork-child-side
 * handler _pthread_atfork() registers) calls
 * __pthread_bsdthread_init()/__pthread_main_thread_postfork_init(), both
 * real Apple source but only defined in pthread.c -- this project's own
 * build_libpthread.sh deliberately never compiles that file (its own
 * header comment: real pthread_create()/thread-lifecycle logic was
 * ported piecemeal elsewhere in this project instead, not as a straight
 * port of pthread.c wholesale).
 *
 * ORIGINAL (WRONG) PREMISE, kept here for the record: "No dyld-loaded,
 * dylib-based process in this project ever calls fork(), so this code
 * path is genuinely unreachable today." That was true when this file was
 * written but is FALSE as of launchd.macho's real job_start()/
 * runtime_fork() work (docs/launchd-pid1-postmortem.md) -- launchd IS
 * dyld-loaded (links this dylib) and forks daemon children constantly,
 * and EVERY one of them runs this exact path.
 *
 * REAL BUG this caused, root-caused via live QEMU instrumentation
 * (kernel-side ipc_kmsg_send()/ipc_kobject_server()/
 * task_get_special_port_from_user() kprintfs plus userland mach_port_
 * type() probes -- all since reverted, see DAR-163's Linear writeup for
 * the full trail): with both functions as pure no-ops, a forked child's
 * __TSD_MIG_REPLY slot (read by mig_get_reply_port(), libsyscall/mach/
 * mig_reply_port.c) kept whatever value it held from BEFORE the fork --
 * the PARENT's own private reply-port name, copied byte-for-byte via
 * fork()'s COW memory duplication but meaningless in the child's own,
 * separate IPC space. Confirmed live: mach_port_type() on that name in
 * the child returned KERN_INVALID_NAME (kr=15) -- not merely the wrong
 * right type, no entry AT ALL -- while a fresh mach_reply_port() call
 * moments later succeeded and returned a genuinely different, valid
 * name. That made bootstrap_init()'s task_get_special_port() MIG call
 * (PureDarwin libxpc/libbootstrap.c, the very first real Mach RPC any
 * forked launchd daemon child ever makes) fail immediately with
 * MACH_SEND_INVALID_REPLY (ipc_right_copyin_check_reply() in the kernel,
 * osfmk/ipc/ipc_right.c, requires the named reply port to carry a
 * RECEIVE right -- none existed). bootstrap_init() then called abort(),
 * and this port's abort() -- kill(getpid(), SIGABRT) with no real signal
 * delivery yet, docs/launchd-pid1-postmortem.md Bug D -- spins forever
 * instead of terminating, which is exactly the silent, no-crash,
 * no-further-console-output hang DAR-163/DAR-191 both observed
 * downstream of "child bootstrap_init".
 *
 * THE FIX: do what real pthread.c's _pthread_main_thread_init() actually
 * does for the Mach-port-holding TSD slots that matter here
 * (MACH_THREAD_SELF, MIG_REPLY, MACH_SPECIAL_REPLY, SEMAPHORE_CACHE),
 * written via the TSD-base-relative accessors in <os/tsd.h>
 * (_os_tsd_set_direct()) rather than through a `pthread_t`'s own
 * `->tsd[]` field -- deliberately: this project's libpthread tier still
 * doesn't compile pthread.c's real `struct pthread_s` layout anywhere
 * (see this file's original header, still true), so going through a
 * `pthread_t` would need that struct. _os_tsd_set_direct() instead
 * writes the EXACT SAME TPIDRRO_EL0-relative memory that
 * mig_get_reply_port()/_os_tsd_get_direct() themselves read (see
 * libsyscall/mach/mig_reply_port.c and os/tsd.h) -- the only invariant
 * that actually matters for this bug, and provably correct regardless of
 * `struct pthread_s`'s real layout.
 *
 * Two real, small, previously-missing pieces this fix needed and now
 * has: `mach_thread_self()` itself (real Apple body -- literally `return
 * thread_self_trap();`, verbatim from third_party/xnu-7195/libsyscall/
 * mach/mach_legacy.c -- reproduced inline below since that whole file
 * isn't compiled anywhere in this port) and its underlying raw trap
 * `thread_self_trap` (trap -27, added to libmalloc_vm_traps.s next to
 * its sibling task_self_trap/mach_reply_port traps -- see that file's
 * own comment). build_libmach_ipc.sh's header previously dropped
 * mach_thread_self() on the same now-false "nothing needs it" premise as
 * this file's original no-op stubs.
 *
 * SECOND REAL BUG, ROOT-CAUSED AND FIXED 2026-09-20 (DAR-337): the
 * version of this file that only reset the four TSD slots above left
 * real _pthread_main_thread_init()'s FIRST action undone --
 * `_pthread_lock_init(&p->lock)`, re-initializing the main thread's own
 * lock (third_party/libpthread/src/pthread.c, _pthread_main_thread_init).
 * That lock is taken before every fork, by real pthread_atfork.c's
 * _pthread_atfork_prepare():
 *
 *     _pthread_lock_lock(&globals->psaved_self_global_lock);
 *     globals->psaved_self = pthread_self();
 *     _pthread_lock_lock(&globals->psaved_self->lock);   <-- this one
 *
 * and is released in the PARENT by _pthread_atfork_parent(). The child
 * never releases it; it is supposed to be reset here instead, which is
 * why real _pthread_main_thread_init() re-inits it rather than unlocking
 * it. (pthread_atfork.c's own _pthread_atfork_child() already re-inits
 * the OTHER one, psaved_self_global_lock, itself -- which is exactly why
 * only this one was left held.)
 *
 * A fork child therefore ran with its main-thread lock permanently held.
 * Harmless for a child that only exec()s -- which is why this survived
 * so long -- but FATAL for a child that forks AGAIN: the second fork's
 * _pthread_atfork_prepare() locks an already-held lock and the process
 * dies with SIGTRAP. Reproduced in one boot by
 * tools/userland_staging/fork_alarm_smoketest.c, whose instrumented
 * replay prints the marker before _pthread_atfork_prepare() and never
 * the one after (`atfork: step 5` with no step 6,
 * `status 0x5` == killed by signal 5, qemu/dar336_337_repro.log).
 *
 * Found via the OpenSSH port (DAR-183): sshd forks per connection and
 * then forks again for privilege separation, so EVERY sshd connection
 * hit this. The fix is below -- the real struct pthread_s layout now
 * comes from libpthread's own src/types_internal.h (via internal.h)
 * rather than being avoided, which is what made the original version
 * reach for TSD-base-relative accessors instead.
 *
 * Deliberately STILL not reproduced here: real _pthread_main_thread_init()'s
 * thread-list bookkeeping (TAILQ_INSERT_HEAD into __pthread_head,
 * _pthread_introspection_thread_start(), and _pthread_lock_init of the
 * _pthread_list_lock global) and real _pthread_bsdthread_init()'s
 * __bsdthread_register() re-registration. Those need pthread.c's own
 * globals, which this tier still does not compile -- __pthread_head,
 * _pthread_count and _pthread_list_lock are all defined there. Nothing
 * in this project reads that list today. Note the kernel would reject
 * the re-registration anyway: XNU propagates P_LREGISTER to fork
 * children (bsd/kern/kern_fork.c:1419) and libpthread's kernel side
 * returns EINVAL for a second registration
 * (third_party/libpthread/kern/kern_support.c:563), which real
 * libpthread's userland tolerates by design -- its own
 * _pthread_bsdthread_init() only inspects the result `if (rv > 0)`.
 *
 * `_pthread_set_self_internal()`'s real thread_id refresh (real pthread.c
 * calls it right after _pthread_main_thread_init()) is also skipped for
 * the same "needs struct pthread_s" reason -- it only refreshes
 * `p->thread_id`, read by pthread_mach_thread_np()/error paths, neither
 * exercised in this same narrow window. */
/* internal.h brings in libpthread's own real struct pthread_s
 * (src/types_internal.h) and the real _pthread_lock_init() inline
 * (src/inline_internal.h) -- the same two headers real pthread.c uses
 * for this exact assignment. This file is compiled with libpthread's own
 * CFLAGS/include set for that reason (see build_libsystem_pthread.sh);
 * pthread_atfork.c, which this file exists to complete, is compiled with
 * the same set and includes the same header. */
#include "internal.h"
#include <os/tsd.h>

typedef unsigned int dar163_mach_port_t;
extern dar163_mach_port_t thread_self_trap(void);
extern dar163_mach_port_t mach_reply_port(void);

/* Real, unmodified body -- see this file's header. Not exported by any
 * dylib in this port yet (build_libmach_ipc.sh explicitly dropped it as
 * unneeded, a premise this fix falsifies), so reproduced verbatim here
 * rather than pulled from mach_legacy.c wholesale for one function. */
/* Renamed (DAR-337): including libpthread's internal.h now also brings in
 * <mach/mach_init.h>'s real mach_thread_self() declaration, which this
 * file's own local copy would otherwise conflict with. Still defined
 * here rather than called through: no dylib in this port exports it
 * (see this file's header). */
static dar163_mach_port_t
dar163_mach_thread_self(void)
{
	return thread_self_trap();
}

/* Real signature now that internal.h declares it (prototypes_internal.h).
 * Still a no-op, and deliberately so -- see this file's header for why
 * the kernel would reject the re-registration in a fork child anyway. */
void _pthread_bsdthread_init(struct _pthread_registration_data *data) {
    (void)data;
}

void _pthread_main_thread_postfork_init(pthread_t p) {
    /* Real _pthread_main_thread_init()'s own first act, and the whole
     * point of DAR-337: the main thread's lock is held across the fork
     * by _pthread_atfork_prepare() and is released in the parent only.
     * Re-initializing (not unlocking) is what the real function does. */
    if (p != NULL) {
        _pthread_lock_init(&p->lock);
    }
    _os_tsd_set_direct(__TSD_MACH_THREAD_SELF, (void *)(uintptr_t)dar163_mach_thread_self());
    _os_tsd_set_direct(__TSD_MIG_REPLY, (void *)(uintptr_t)mach_reply_port());
    _os_tsd_set_direct(__TSD_MACH_SPECIAL_REPLY, (void *)0);
    _os_tsd_set_direct(__TSD_SEMAPHORE_CACHE, (void *)0);
}
