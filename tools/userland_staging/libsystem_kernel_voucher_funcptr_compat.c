/* Real Apple libsyscall voucher_mach_msg_* function-pointer indirection,
 * reproduced verbatim from third_party/xnu-7195/libsyscall/wrappers/
 * _libc_funcptr.c (the "mach/mach.h voucher_mach_msg API" block, lines
 * 260-310 there) plus its own `_libkernel_voucher_functions_t` type from
 * libsyscall/wrappers/_libkernel_init.h. Reproduced rather than
 * compiling _libc_funcptr.c wholesale because that file ALSO defines a
 * full malloc/string/pthread function-pointer tier this dylib already
 * gets from real sources elsewhere -- pulling it in whole would be a
 * genuine duplicate-definition link error, not a hypothetical one.
 *
 * WHY THIS IS NEEDED (DAR-153, 2026-09-01 -- real, QEMU-root-caused
 * SIGTRAP in syslogd, symbolicated from a real user frame-pointer dump):
 *
 * Every real `mig`-generated client stub in this project (host_get_
 * special_port, host_create_mach_voucher, ...) emits real Apple's own
 * `if (voucher_mach_msg_set != NULL) voucher_mach_msg_set(&InP->Head);`
 * weak_import guard before its mach_msg() send. On real Apple those
 * stubs live in libsystem_kernel.dylib next to THIS file's real
 * definition, so they bind to it -- and it is inert by default, because
 * `_libkernel_voucher_functions` starts out pointing at the all-NULL
 * `_libkernel_voucher_functions_empty`, and only libdispatch's
 * `_voucher_libkernel_init()` ever swaps in the real table via
 * `__libkernel_voucher_init()`.
 *
 * This project's libsystem_kernel.dylib had no such definition, so it
 * (and libsystem_pthread.dylib, which carries its own copies of those
 * MIG stubs) left the symbol undefined and it bound straight through to
 * libdispatch.dylib's REAL `voucher_mach_msg_set()` instead -- skipping
 * the entire table indirection real Apple relies on. That produced a
 * real, deterministic recursive dispatch_once():
 *
 *   dispatch_once_f(_voucher_task_mach_voucher_pred)
 *     -> _voucher_task_mach_voucher_init          (voucher.c:297)
 *        -> _voucher_create_mach_voucher()
 *           -> _dispatch_get_mach_host_port()     [second once gate]
 *              -> _dispatch_mach_host_port_init()
 *                 -> host_get_special_port()      [MIG stub, mach_msg send]
 *                    -> voucher_mach_msg_set()    [libdispatch's, not libsyscall's]
 *                       -> _voucher_get_task_mach_voucher()
 *                          -> dispatch_once_f(_voucher_task_mach_voucher_pred)
 *                             -> "trying to lock recursively" -> brk #1
 *
 * Note this build's libdispatch compiles the EMPTY `_voucher_init(void)
 * {}` variant (confirmed: the built dylib has no `__libkernel_voucher_
 * init` reference at all), i.e. it never installs a voucher table and
 * considers the subsystem uninitialized -- so its `voucher_mach_msg_set`
 * being reachable from a MIG stub at all was the inconsistency, not the
 * recursion itself. Restoring the real owner of the symbol fixes it at
 * the architecture level rather than special-casing the one call site
 * that happened to crash first.
 *
 * Only the recursion-relevant real bodies are reproduced (set/clear/
 * adopt/revert + __libkernel_voucher_init). They are exported, matching
 * real libsystem_kernel.dylib, which really does export all four as
 * public <mach/mach.h> API.
 */

#include <mach/mach_types.h>
#include <mach/message.h>

/* Real declaration from libsyscall/mach/mach/mach.h. */
typedef unsigned long voucher_mach_msg_state_t;
#define VOUCHER_MACH_MSG_STATE_UNCHANGED ((voucher_mach_msg_state_t)0)

/* Real type from libsyscall/wrappers/_libkernel_init.h, verbatim. */
typedef const struct _libkernel_voucher_functions {
	/* The following functions are included in version 1 of this structure */
	unsigned long version;
	boolean_t (*voucher_mach_msg_set)(mach_msg_header_t*);
	void (*voucher_mach_msg_clear)(mach_msg_header_t*);
	voucher_mach_msg_state_t (*voucher_mach_msg_adopt)(mach_msg_header_t*);
	void (*voucher_mach_msg_revert)(voucher_mach_msg_state_t);

	/* Subsequent versions must only add pointers! */
} *_libkernel_voucher_functions_t;

static const struct _libkernel_voucher_functions
    _libkernel_voucher_functions_empty;
static _libkernel_voucher_functions_t _libkernel_voucher_functions =
    &_libkernel_voucher_functions_empty;

kern_return_t
__libkernel_voucher_init(_libkernel_voucher_functions_t fns)
{
	_libkernel_voucher_functions = fns;
	return KERN_SUCCESS;
}

boolean_t
voucher_mach_msg_set(mach_msg_header_t *msg)
{
	if (_libkernel_voucher_functions->voucher_mach_msg_set) {
		return _libkernel_voucher_functions->voucher_mach_msg_set(msg);
	}
	return 0;
}

void
voucher_mach_msg_clear(mach_msg_header_t *msg)
{
	if (_libkernel_voucher_functions->voucher_mach_msg_clear) {
		return _libkernel_voucher_functions->voucher_mach_msg_clear(msg);
	}
}

voucher_mach_msg_state_t
voucher_mach_msg_adopt(mach_msg_header_t *msg)
{
	if (_libkernel_voucher_functions->voucher_mach_msg_adopt) {
		return _libkernel_voucher_functions->voucher_mach_msg_adopt(msg);
	}
	return VOUCHER_MACH_MSG_STATE_UNCHANGED;
}

void
voucher_mach_msg_revert(voucher_mach_msg_state_t state)
{
	if (_libkernel_voucher_functions->voucher_mach_msg_revert) {
		return _libkernel_voucher_functions->voucher_mach_msg_revert(state);
	}
}
