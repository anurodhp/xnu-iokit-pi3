/* Real Apple libsyscall/mach/mach_port.c logic for these 6 routines,
 * curated: real mach_port_set_mscount()/get_set_status()/
 * set_attributes() call their real MIG client stub
 * (_kernelrpc_mach_port_*, from mach_port.defs) directly with no fast
 * path at all -- verbatim here. Real mach_port_move_member()/
 * request_notification()/get_attributes() try a real kernel fast-path
 * trap (_kernelrpc_mach_port_*_trap) first, falling back to the same
 * MIG stub only on MACH_SEND_INVALID_DEST -- this project skips
 * generating those raw traps (a real, bounded scope decision, not a
 * correctness gap: the MIG fallback path is unconditionally available
 * and functionally identical, real Apple's own fast-path trap exists
 * purely to avoid one extra Mach IPC round-trip). Confirmed via source
 * inspection (third_party/xnu-7195/libsyscall/mach/mach_port.c), not
 * guessed. The real MIG client stubs themselves come from this
 * project's own real `mig` regen against the real mach_port.defs (see
 * build_launchd_core.sh).
 */
#include <mach/mach.h>

extern kern_return_t _kernelrpc_mach_port_set_mscount(ipc_space_t task, mach_port_name_t name, mach_port_mscount_t mscount);
extern kern_return_t _kernelrpc_mach_port_get_set_status(ipc_space_t task, mach_port_name_t name, mach_port_name_array_t *members, mach_msg_type_number_t *membersCnt);
extern kern_return_t _kernelrpc_mach_port_move_member(ipc_space_t task, mach_port_name_t member, mach_port_name_t after);
extern kern_return_t _kernelrpc_mach_port_request_notification(ipc_space_t task, mach_port_name_t name, mach_msg_id_t msgid, mach_port_mscount_t sync, mach_port_t notify, mach_msg_type_name_t notifyPoly, mach_port_t *previous);
extern kern_return_t _kernelrpc_mach_port_set_attributes(ipc_space_t task, mach_port_name_t name, mach_port_flavor_t flavor, mach_port_info_t port_info, mach_msg_type_number_t port_infoCnt);

kern_return_t mach_port_set_mscount(ipc_space_t task, mach_port_name_t name, mach_port_mscount_t mscount) {
	return _kernelrpc_mach_port_set_mscount(task, name, mscount);
}

kern_return_t mach_port_get_set_status(ipc_space_t task, mach_port_name_t name, mach_port_name_array_t *members, mach_msg_type_number_t *membersCnt) {
	return _kernelrpc_mach_port_get_set_status(task, name, members, membersCnt);
}

/* TEMP DIAGNOSTIC: bisecting a silent exit(1) inside real
 * _kernelrpc_mach_port_move_member's MIG round-trip during launchd's own
 * runtime_add_mport(). Remove once root-caused. */
static long iokit_diag9_syscall3(long num, long a0, long a1, long a2) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x16 asm("x16") = num;
    asm volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x16) : "memory", "cc");
    return x0;
}
static void iokit_diag9(const char *msg) {
    long len = 0;
    while (msg[len] != '\0') { len++; }
    long fd = iokit_diag9_syscall3(5, (long)"/dev/console", 0x0001 | 0x20000, 0);
    if (fd < 0) { return; }
    iokit_diag9_syscall3(4, fd, (long)msg, len);
    iokit_diag9_syscall3(6, fd, 0, 0);
}

kern_return_t mach_port_move_member(ipc_space_t task, mach_port_name_t member, mach_port_name_t after) {
	iokit_diag9("IOKITDIAG9: mach_port_move_member entry, about to call _kernelrpc_mach_port_move_member\n");
	kern_return_t r = _kernelrpc_mach_port_move_member(task, member, after);
	iokit_diag9("IOKITDIAG9: _kernelrpc_mach_port_move_member returned\n");
	return r;
}

/* Shared with mig_reply_port.c's own TEMP DIAGNOSTIC (real, unmodified
 * XNU source, temporarily instrumented same as everywhere else this
 * session) -- same raw-syscall pattern, just not static so it's callable
 * from that separate translation unit. Remove both together once
 * root-caused. */
void iokit_diag10(const char *msg) {
	iokit_diag9(msg);
}

void iokit_diag11(const char *msg) {
	iokit_diag9(msg);
}

void iokit_diag14(const char *msg) {
	iokit_diag9(msg);
}

kern_return_t mach_port_request_notification(ipc_space_t task, mach_port_name_t name, mach_msg_id_t msgid, mach_port_mscount_t sync, mach_port_t notify, mach_msg_type_name_t notifyPoly, mach_port_t *previous) {
	return _kernelrpc_mach_port_request_notification(task, name, msgid, sync, notify, notifyPoly, previous);
}

/* mach_port_get_attributes() used to be forwarded here too -- real
 * cf_mach_port_syscalls.c (CoreFoundation's own tier) already provides
 * a more complete version (real kernel fast-path trap first, MIG
 * fallback only on MACH_SEND_INVALID_DEST, matching real Apple's
 * mach_port.c:309 exactly) -- a real trial link found both, as a hard
 * duplicate-symbol conflict once CoreFoundation is linked alongside
 * this tier. Dropped here in favor of the more complete one. */

kern_return_t mach_port_set_attributes(ipc_space_t task, mach_port_name_t name, mach_port_flavor_t flavor, mach_port_info_t port_info, mach_msg_type_number_t port_infoCnt) {
	return _kernelrpc_mach_port_set_attributes(task, name, flavor, port_info, port_infoCnt);
}
