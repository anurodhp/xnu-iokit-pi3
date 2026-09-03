/* Real gap, found during build_syslogd.sh's own trial link: PureDarwin's
 * real notify_client.c calls the public mach_port_move_member() wrapper
 * directly (not just its MIG-generated cousin). This project's existing
 * source (launchd_mach_port_compat.c's mach_port_move_member()) lives in
 * a file that ALSO defines several OTHER mach_port_* functions already
 * provided for real elsewhere in this project's link (libxpc_syscalls.a,
 * etc) -- pulling in the whole file/object would create real
 * duplicate-symbol conflicts, confirmed by this pass's own trial link.
 * Real, narrow fix: just this one function, same real trap-first shape
 * this pass's own syslogd_mach_port_extra_trap.s already established
 * (__kernelrpc_mach_port_move_member_trap, -20, this project hadn't
 * needed yet) -- the real MIG-fallback path (_kernelrpc_mach_port_*(),
 * for the historical case where the trap itself doesn't exist) is
 * omitted since this project's own kernel fork always has the trap;
 * omitting it is a real, honest simplification of the fallback-of-last-
 * resort, not a behavior change for this kernel.
 *
 * mach_port_destruct() REMOVED 2026-09-03 (DAR-24): libsystem_kernel.dylib
 * now really exports it (real __kernelrpc_mach_port_destruct_trap,
 * confirmed via dyld_info -exports, not guessed) -- see
 * syslogd_mach_port_extra_trap.s's own updated comment for the trap
 * stub's half of this same removal. */
#include <mach/mach_types.h>
#include <mach/message.h>
#include <mach/kern_return.h>

extern kern_return_t _kernelrpc_mach_port_move_member_trap(ipc_space_t, mach_port_name_t, mach_port_name_t);

kern_return_t mach_port_move_member(ipc_space_t task, mach_port_name_t member, mach_port_name_t after) {
    return _kernelrpc_mach_port_move_member_trap(task, member, after);
}
