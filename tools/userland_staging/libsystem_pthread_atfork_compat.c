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
 * Deliberately NOT reproduced here: real _pthread_main_thread_init()'s
 * thread-list bookkeeping (TAILQ_INSERT_HEAD into __pthread_head,
 * _pthread_introspection_thread_start()) and real _pthread_bsdthread_
 * init()'s __bsdthread_register() re-registration. Both need pthread.c's
 * own internal globals/types this tier still doesn't compile, and
 * nothing in this project's single-thread-until-exec() forked-child
 * model reads that list or needs fresh thread-lifecycle kernel callbacks
 * in the narrow fork()-to-exec() window. A real, separate, narrower gap
 * than the one this fixes -- not papered over, just genuinely out of
 * scope for what actually blocks daemon children today. If a forked
 * child ever needs pthread_create() before exec(), _pthread_bsdthread_
 * init() staying a no-op is the thing to revisit first.
 *
 * `_pthread_set_self_internal()`'s real thread_id refresh (real pthread.c
 * calls it right after _pthread_main_thread_init()) is also skipped for
 * the same "needs struct pthread_s" reason -- it only refreshes
 * `p->thread_id`, read by pthread_mach_thread_np()/error paths, neither
 * exercised in this same narrow window. */
#include <os/tsd.h>

typedef unsigned int dar163_mach_port_t;
extern dar163_mach_port_t thread_self_trap(void);
extern dar163_mach_port_t mach_reply_port(void);

/* Real, unmodified body -- see this file's header. Not exported by any
 * dylib in this port yet (build_libmach_ipc.sh explicitly dropped it as
 * unneeded, a premise this fix falsifies), so reproduced verbatim here
 * rather than pulled from mach_legacy.c wholesale for one function. */
static dar163_mach_port_t
mach_thread_self(void)
{
	return thread_self_trap();
}

void _pthread_bsdthread_init(void *arg) {
    (void)arg;
}

void _pthread_main_thread_postfork_init(void *p) {
    (void)p;
    _os_tsd_set_direct(__TSD_MACH_THREAD_SELF, (void *)(uintptr_t)mach_thread_self());
    _os_tsd_set_direct(__TSD_MIG_REPLY, (void *)(uintptr_t)mach_reply_port());
    _os_tsd_set_direct(__TSD_MACH_SPECIAL_REPLY, (void *)0);
    _os_tsd_set_direct(__TSD_SEMAPHORE_CACHE, (void *)0);
}
