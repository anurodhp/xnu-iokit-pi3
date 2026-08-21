/* Real signatures, honestly-unreachable-for-this-project bodies -- same
 * "real signature, KERN_FAILURE body, not hidden" pattern
 * libmalloc_vm_mig_stubs.c already established in this project for
 * mach_vm.c's own provably-dead MIG-fallback paths.
 *
 * mach_msg_real.c's real, verbatim mach_msg_destroy_port()/
 * mach_msg_destroy_memory() (curated from third_party/xnu-7195/
 * libsyscall/mach/mach_msg.c) reference these 4 symbols unconditionally
 * (real Apple source, not modified), but only reach them via reply-
 * message shapes that this project's own real, correct kernel never
 * produces for the 3 real MIG routines Phase 4.6 item 5 actually calls
 * (clock_get_time/host_get_clock_service/semaphore_create):
 *   - mach_port_insert_right / mach_port_extract_right: only reached by
 *     mach_msg_destroy_port()'s MAKE_SEND/MAKE_SEND_ONCE cases. Every
 *     port descriptor this project's 3 routines can ever receive has
 *     disposition MACH_MSG_TYPE_MOVE_SEND (17) -- confirmed directly
 *     from each routine's own real generated __MIG_check__Reply__*_t
 *     disposition check (`Out0P->clock_serv.disposition != 17` /
 *     `...semaphore.disposition != 17`, real `mig`-generated code, not
 *     guessed) -- so these MAKE_SEND(20)/MAKE_SEND_ONCE(21) cases are
 *     provably unreachable here.
 *   - mach_port_unguard: only reached by the MACH_MSG_GUARDED_PORT_DESCRIPTOR
 *     case, a descriptor type none of this project's 3 routines' real
 *     .defs (clock.defs/mach_host.defs/task.defs's semaphore_create)
 *     ever produce.
 *
 * (vm_deallocate itself is NOT stubbed here -- real, unmodified
 * third_party/xnu-7195/libsyscall/mach/mach_vm.c, already compiled into
 * libc_build/libmalloc.a by build_libmalloc.sh, already defines a real,
 * working vm_deallocate() -- confirmed via `nm`, not guessed. Providing
 * a second definition here would be a real duplicate-symbol link error,
 * not an improvement; mach_msg_destroy_memory()'s call just resolves to
 * that already-real implementation instead, which is strictly better
 * than a KERN_FAILURE stand-in on the vanishingly-unlikely chance this
 * dead path is ever actually reached.)
 *
 * All 3 remaining stand-ins here are reached ONLY from mach_msg_destroy(), itself only called
 * from a MIG reply-validation FAILURE path (a malformed/corrupted reply)
 * -- never exercised in real operation against this project's own
 * kernel. Kept honest (KERN_FAILURE / no-op, matching real Apple's own
 * error-return convention) rather than silently faked, same standard as
 * every other stand-in in this project.
 */
#include <mach/mach_types.h>
#include <mach/kern_return.h>
#include <mach/vm_types.h>
#include <mach/message.h>
#include <mach/boolean.h>

kern_return_t
mach_port_insert_right(ipc_space_t task, mach_port_name_t name,
    mach_port_t poly, mach_msg_type_name_t polyPoly)
{
	(void)task; (void)name; (void)poly; (void)polyPoly;
	return KERN_FAILURE;
}

kern_return_t
mach_port_extract_right(ipc_space_t task, mach_port_name_t name,
    mach_msg_type_name_t msgt_name, mach_port_t *poly,
    mach_msg_type_name_t *polyPoly)
{
	(void)task; (void)name; (void)msgt_name;
	if (poly) {
		*poly = MACH_PORT_NULL;
	}
	if (polyPoly) {
		*polyPoly = 0;
	}
	return KERN_FAILURE;
}

kern_return_t
mach_port_unguard(ipc_space_t task, mach_port_name_t name, uint64_t context)
{
	(void)task; (void)name; (void)context;
	return KERN_FAILURE;
}

/* voucher_mach_msg_set: every real `mig`-generated client stub this
 * project compiles (clockUser.c/mach_hostUser.c/taskUser.c) references
 * this, weak_import-declared, gated behind
 * `__has_include(<mach/mig_voucher_support.h>)` -- which resolves TRUE
 * against the Xcode-12 SDK's own copy of that header even though this
 * project's real xnu-7195 checkout has no voucher subsystem wired up
 * for any of its real routines (host_get_clock_service/semaphore_create/
 * clock_get_time carry no voucher). Confirmed by testing that ld64's
 * static linker does NOT leave a weak_import symbol unresolved the way
 * dyld's lazy binding normally would -- needs a real definition to
 * static-link at all. Honest `FALSE` (real Apple's own "no voucher to
 * attach" return value, not a fabricated success) -- this project has no
 * real Mach voucher port to offer regardless, same "not yet built" scope
 * decision as INET/INET6 networking. */
boolean_t
voucher_mach_msg_set(mach_msg_header_t *msg)
{
	(void)msg;
	return FALSE;
}
