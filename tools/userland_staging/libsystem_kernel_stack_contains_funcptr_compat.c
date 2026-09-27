/* pthread_current_stack_contains_np() thunk for libsystem_kernel (DAR-423).
 *
 * libsyscall/wrappers/terminate_with_reason.c's __darwin_check_fd_set_overflow
 * calls pthread_current_stack_contains_np(), which lives in libsystem_pthread,
 * a dylib above this one. Apple doesn't link upward. libsystem_kernel instead
 * defines a hidden thunk in libsyscall/wrappers/_libc_funcptr.c:90-100 that
 * calls through the _libkernel_functions table libSystem registers at init
 * (__libkernel_init, _libkernel_init.c). Before registration, or with a
 * table older than version 4, the thunk returns 0.
 *
 * This port builds neither _libc_funcptr.c nor _libkernel_init.c, and its
 * libSystem initializer never calls __libkernel_init (the same situation
 * libsystem_kernel_voucher_funcptr_compat.c handles for vouchers), and
 * libsystem_pthread doesn't export pthread_current_stack_contains_np yet.
 * So this file carries only that one thunk.
 *
 * DEVIATION, deliberate and temporary: with no table, Apple's thunk returns
 * 0 ("not on the stack"). Here it returns 1. __darwin_check_fd_set_overflow
 * (terminate_with_reason.c:34-57) faults on n < 0 regardless. For
 * n >= FD_SETSIZE it asks whether the fd_set is on the current stack: if so,
 * and the caller did not opt into unlimited select, it faults and returns
 * 0, and FD_SET skips the write. Answering 0 would let FD_SET write past a
 * 128-byte stack fd_set, corrupting the stack. Answering 1 means an
 * oversized heap fd_set without _DARWIN_UNLIMITED_SELECT is also refused.
 * That fails safe, and it is no stricter than the libsystem_c stand-in
 * this replaces (DAR-423), which abort()ed on every n >= FD_SETSIZE.
 * DAR-416 step 1c replaces this file with Apple's _libc_funcptr.c and
 * _libkernel_init.c, registering libsystem_pthread's real function.
 */
#include <stddef.h>

/* A private table with only the two fields the thunk reads. It is NOT
 * layout-compatible with Apple's struct _libkernel_functions
 * (_libkernel_init.h, where pthread_current_stack_contains_np is the
 * version-4 field at :69), and nothing ever registers it. When DAR-416 step
 * 1c wires the real __libkernel_init, this file is replaced by Apple's
 * _libc_funcptr.c and _libkernel_init.c. */
struct libkernel_stack_contains_functions {
	unsigned long version;
	int (*pthread_current_stack_contains_np)(const void *, size_t);
};

static const struct libkernel_stack_contains_functions libkernel_stack_contains_empty;
static const struct libkernel_stack_contains_functions *libkernel_stack_contains_fns =
    &libkernel_stack_contains_empty;

/* _libc_funcptr.c:90-100, except the table variable and the fallback
 * return value (1, not 0; see this file's header). */
__attribute__((visibility("hidden")))
int
pthread_current_stack_contains_np(const void *addr, size_t len)
{
	if (libkernel_stack_contains_fns->version >= 4 &&
	    libkernel_stack_contains_fns->pthread_current_stack_contains_np) {
		return libkernel_stack_contains_fns->pthread_current_stack_contains_np(addr, len);
	}

	return 1;
}
