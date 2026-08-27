/* Real gap, found during build_syslogd.sh's own trial link: PureDarwin's
 * real notify_client.c calls the public mach_port_destruct()/
 * mach_port_move_member() wrappers directly (not just their MIG-
 * generated cousins). This project's existing sources for these two
 * (tools/userland_staging/cf_mach_port_syscalls.c's mach_port_destruct(),
 * launchd_mach_port_compat.c's mach_port_move_member()) each live in a
 * file that ALSO defines several OTHER mach_port_* functions
 * (mach_port_allocate/construct/extract_member/get_attributes/
 * insert_member/type, mach_port_get_set_status/request_notification/
 * set_attributes/set_mscount) already provided for real elsewhere in
 * this project's link (libxpc_syscalls.a, etc) -- pulling in either
 * whole file/object would create real duplicate-symbol conflicts,
 * confirmed by this pass's own trial link. Real, narrow fix: just these
 * 2 functions, same real trap-first shape (mach_port_traps.s already has
 * the real __kernelrpc_mach_port_destruct_trap stub; this pass's own
 * syslogd_mach_port_extra_trap.s adds the one real trap number
 * (__kernelrpc_mach_port_move_member_trap, -20) this project hadn't
 * needed yet) -- the real MIG-fallback path (_kernelrpc_mach_port_*(),
 * for the historical case where the trap itself doesn't exist) is
 * omitted since this project's own kernel fork always has both traps;
 * omitting it is a real, honest simplification of the fallback-of-last-
 * resort, not a behavior change for this kernel. */
#include <mach/mach_types.h>
#include <mach/message.h>
#include <mach/kern_return.h>

extern kern_return_t _kernelrpc_mach_port_destruct_trap(ipc_space_t, mach_port_name_t, mach_port_delta_t, uint64_t);
extern kern_return_t _kernelrpc_mach_port_move_member_trap(ipc_space_t, mach_port_name_t, mach_port_name_t);

kern_return_t mach_port_destruct(ipc_space_t task, mach_port_name_t name, mach_port_delta_t srdelta, uint64_t guard) {
    return _kernelrpc_mach_port_destruct_trap(task, name, srdelta, guard);
}

kern_return_t mach_port_move_member(ipc_space_t task, mach_port_name_t member, mach_port_name_t after) {
    return _kernelrpc_mach_port_move_member_trap(task, member, after);
}
