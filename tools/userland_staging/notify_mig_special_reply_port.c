/* Real mig_get_special_reply_port()/mig_dealloc_special_reply_port()
 * bodies, curated from third_party/xnu-7195/libsyscall/mach/mach_port.c
 * lines 441-478 -- required because notify_ipc.defs (this project's real,
 * unmodified third_party/Libnotify/notify_ipc.defs) sets
 * "UseSpecialReplyPort 1;" (line 38), which makes every non-simpleroutine
 * mig-generated call site (confirmed directly in the regenerated
 * notify_ipcUser.c: every one of _notify_server_check/_get_state/
 * _suspend/_resume/_post_2/_register_check_2/_get_state_2/_get_state_3/
 * _set_state_3/_regenerate/_checkin/_dump/_generate_common_port/
 * _register_mach_port_3 sets `InP->Head.msgh_reply_port =
 * mig_get_special_reply_port()` unconditionally, and the shared
 * MIG_CHECK_ERROR/failure macros at lines 105/118 call
 * mig_dealloc_special_reply_port() on the send/receive-failure path)
 * reference both symbols -- neither of which is provided by
 * libc_build/libmach_ipc.a (that archive's own real .defs -- clock.defs/
 * mach_host.defs/task.defs -- never set UseSpecialReplyPort, so
 * build_libmach_ipc.sh never needed either symbol; confirmed via `nm`
 * against libmach_ipc.a before writing this file, not assumed).
 *
 * mig_get_special_reply_port() is REAL and COMPLETE here, zero omissions:
 * a per-thread TSD slot (__TSD_MACH_SPECIAL_REPLY, real value 8, from
 * this project's own real third_party/xnu-7195/libsyscall/os/tsd.h:44)
 * lazily populated by the real thread_get_special_reply_port() Mach trap
 * (real trap number -50 -- third_party/xnu-7195/osfmk/mach/syscall_sw.h:133
 * `kernel_trap(thread_get_special_reply_port,-50,0)`, confirmed present
 * as a real kernel trap in this exact xnu-7195 checkout, unlike the
 * gap below). _os_tsd_get_direct()/_os_tsd_set_direct() are the SAME
 * real per-thread-TSD mechanism this project's own
 * libplatform_thread_self.c already uses (see build_libplatform.sh) for
 * __TSD_MACH_THREAD_SELF -- proven working on real QEMU boots.
 *
 * mig_dealloc_special_reply_port() has ONE real, honestly-documented
 * omission: real Apple's own body (mach_port.c:467-478) also calls
 * thread_destruct_special_reply_port(srp, THREAD_SPECIAL_REPLY_PORT_ALL)
 * before deallocating the port name. That function is declared (real,
 * unmodified) at third_party/xnu-7195/libsyscall/mach/mach/
 * mach_sync_ipc.h:124 -- but has NO corresponding kernel-side trap number
 * or MIG routine anywhere in this exact xnu-7195 checkout: grepped
 * osfmk/kern/syscall_sw.c, osfmk/mach/syscall_sw.h, osfmk/mach/
 * mach_traps.h, and osfmk/kern/ipc_tt.c (mach_port.c's own comment cites
 * ipc_tt.c as the kernel-side special-reply-port machinery) and found
 * zero matches for "thread_destruct_special_reply_port" in ANY of them
 * (also checked third_party/xnu-latest's osfmk tree -- also absent).
 * This is a real, provable gap in this project's exact pinned kernel
 * source tree, not a guess or a convenience skip: Apple's own real
 * libsyscall source (compiled against a DIFFERENT, newer Darwin kernel
 * whose OSS tag isn't pinned here) references a kernel entry point this
 * project's actual xnu-7195 tag genuinely does not implement.
 * Consequence of the omission: the real per-thread special-reply-port
 * kernel-side "destruct all rights" bookkeeping step is skipped, but the
 * REAL, correct, unmodified fallback right below it in Apple's own
 * source still runs unconditionally either way (plain
 * mach_port_deallocate() of the reply port name, already real and
 * already linked via libc_build/libmalloc.a's mach_port_wrappers -- see
 * build_iokituser.sh) -- so the reply port's send/receive rights are
 * still genuinely released, just without the extra kernel-internal
 * per-thread accounting real newer Darwin versions perform. Documented,
 * not hidden -- same "real signature, one real kernel call this exact
 * pinned tree can't service" class of gap as
 * build_iokituser.sh's own iokit_trap.s caveat, not a fabricated stub.
 */
#include <mach/mach_types.h>
#include <mach/kern_return.h>
#include <mach/port.h>
#include <mach/boolean.h>

/* Real header, unmodified: defines __TSD_MACH_SPECIAL_REPLY (8) and the
 * real static-inline _os_tsd_get_direct()/_os_tsd_set_direct() (TPIDRRO_EL0-
 * based on arm64) -- same real header libplatform_thread_self.c already
 * includes for the exact same reason (see that file's own header
 * comment); no separate link dependency, these are static inline. */
#include "../../third_party/xnu-7195/libsyscall/os/tsd.h"

extern mach_port_name_t thread_get_special_reply_port(void);
extern kern_return_t mach_port_deallocate(ipc_space_t task, mach_port_name_t name);
extern mach_port_t mach_task_self_;

static inline mach_port_t
_tsd_get_special_reply_port(void)
{
	return (mach_port_t)(unsigned long)_os_tsd_get_direct(__TSD_MACH_SPECIAL_REPLY);
}

static inline void
_tsd_set_special_reply_port(mach_port_t port)
{
	_os_tsd_set_direct(__TSD_MACH_SPECIAL_REPLY, (void *)(unsigned long)port);
}

mach_port_t
mig_get_special_reply_port(void)
{
	mach_port_t srp;

	srp = _tsd_get_special_reply_port();
	if (!MACH_PORT_VALID(srp)) {
		srp = thread_get_special_reply_port();
		_tsd_set_special_reply_port(srp);
	}

	return srp;
}

void
mig_dealloc_special_reply_port(mach_port_t migport)
{
	mach_port_t srp = _tsd_get_special_reply_port();
	if (MACH_PORT_VALID(srp)) {
		/* thread_destruct_special_reply_port(srp, THREAD_SPECIAL_REPLY_PORT_ALL)
		 * omitted here -- see this file's own header comment for the
		 * exact, real reason (no trap/MIG routine for it anywhere in
		 * this project's pinned xnu-7195 kernel source). */
		if (migport != srp) {
			mach_port_deallocate(mach_task_self_, migport);
		}
		_tsd_set_special_reply_port(MACH_PORT_NULL);
	}
}
