/* DAR-225: gives the main thread's pthread_s a real stackaddr/stackbottom.
 *
 * pthread_main_thread_bootstrap.c's `_main_thread` is a BSS struct, and
 * that bootstrap never set its stack fields, so pthread_get_stackaddr_np()
 * returned NULL on the main thread. Real libdispatch's
 * _dispatch_clear_stack() (third_party/libdispatch/src/shims.h:270) does
 * `bzero(&a[1], stackaddr - &a[1])`; with a NULL stackaddr that length is
 * negative and the bzero walks off the top of the stack. DAR-222 fixed the
 * same bug for pthread_create()'d threads.
 *
 * Source of the value: real Apple's __pthread_init()
 * (third_party/libpthread/src/pthread.c:1784-1790) reads the kernel's
 * `main_stack=` apple[] string, which xnu-7195 emits for every LC_MAIN/
 * LC_UNIXTHREAD image (bsd/kern/kern_exec.c:5531-5543, exec_add_apple_strings():
 * "stack, size, alloc, alloc_size", guard page excluded from size). dyld-940
 * hands apple[] to every image initializer as the 4th argument
 * (third_party/dyld/dyld/DyldProcessConfig.h:67, Loader.cpp:1273), and
 * libSystem.B.dylib's constructor (libsystem_dyld_initializer_compat.c)
 * passes it here, as real libSystem_initializer passes it to
 * __pthread_init() (third_party/Libsystem/init.c:221).
 *
 * Deviation from real Apple: when main_stack= is missing or malformed, real
 * __pthread_init() falls back to sysctl(KERN_USRSTACK) and then to the
 * compile-time USRSTACK64 with stacksize = DFLSSIZ (pthread.c:1791-1803).
 * No dylib here exports __sysctl, and the constant fallback would be a
 * guess. A wrong-but-mapped stackaddr turns _dispatch_clear_stack() into
 * silent memory corruption, where a NULL one faults loudly. So on any
 * missing, malformed or implausible value this leaves the fields NULL.
 */

#include "internal.h"
#include <_simple.h>

extern struct pthread_s _main_thread; /* pthread_main_thread_bootstrap.c */

/* Verbatim from pthread.c:1638-1662 (static there, and pthread.c does not
 * compile standalone here -- see pthread_main_thread_bootstrap.c). */
static unsigned long
_pthread_strtoul(const char *p, const char **endptr, int base)
{
	uintptr_t val = 0;

	// Expect hex string starting with "0x"
	if ((base == 16 || base == 0) && p && p[0] == '0' && p[1] == 'x') {
		p += 2;
		while (1) {
			char c = *p;
			if ('0' <= c && c <= '9') {
				val = (val << 4) + (c - '0');
			} else if ('a' <= c && c <= 'f') {
				val = (val << 4) + (c - 'a' + 10);
			} else if ('A' <= c && c <= 'F') {
				val = (val << 4) + (c - 'A' + 10);
			} else {
				break;
			}
			++p;
		}
	}

	*endptr = (char *)p;
	return val;
}

/* Verbatim from pthread.c:1664-1692, except bzero/strlen are spelled as
 * the libsystem_platform primitives this dylib already binds them to. */
static int
parse_main_stack_params(const char *apple[],
			void **stackaddr,
			size_t *stacksize,
			void **allocaddr,
			size_t *allocsize)
{
	const char *p = _simple_getenv(apple, "main_stack");
	if (!p) return 0;

	int ret = 0;
	const char *s = p;

	*stackaddr = (void *)_pthread_strtoul(s, &s, 16);
	if (*s != ',') goto out;

	*stacksize = _pthread_strtoul(s + 1, &s, 16);
	if (*s != ',') goto out;

	*allocaddr = (void *)_pthread_strtoul(s + 1, &s, 16);
	if (*s != ',') goto out;

	*allocsize = _pthread_strtoul(s + 1, &s, 16);
	if (*s != ',' && *s != 0) goto out;

	ret = 1;
out:
	_platform_bzero((char *)p, _platform_strlen(p));
	return ret;
}

/* Must run on the main thread, while it is still on the kernel-allocated
 * stack: the sanity check below uses the current frame. */
int
pthread_main_thread_init_stack(const char *apple[])
{
	void *stackaddr = NULL;
	size_t stacksize = 0;
	void *allocaddr = NULL;
	size_t allocsize = 0;

	if (apple == NULL ||
	    !parse_main_stack_params(apple, &stackaddr, &stacksize,
	    &allocaddr, &allocsize) ||
	    stackaddr == NULL || stacksize == 0) {
		return 0;
	}

	/* Not in real Apple (it trusts the kernel). Here the failure mode of a
	 * wrong value is silent corruption, so require: page-aligned top, a
	 * range that contains this frame, and an allocation that contains the
	 * range. PAGE_MIN_MASK rather than vm_page_mask so this does not depend
	 * on mach_init() having run first. The kernel truncates the stack top
	 * to its page size (xnu-7195 bsd/kern/mach_loader.c:2610, 2721), which
	 * is at least PAGE_MIN_SIZE. */
	uintptr_t top = (uintptr_t)stackaddr;
	uintptr_t frame = (uintptr_t)__builtin_frame_address(0);
	if ((top & PAGE_MIN_MASK) != 0 || stacksize > top ||
	    frame >= top || frame < top - stacksize) {
		return 0;
	}
	if (allocaddr != NULL &&
	    ((uintptr_t)allocaddr > top - stacksize ||
	    (uintptr_t)allocaddr + allocsize < top)) {
		return 0;
	}

	/* The stack fields _pthread_struct_init() sets (pthread.c:898-901). */
	_main_thread.stackaddr = stackaddr;
	_main_thread.stackbottom = (char *)stackaddr - stacksize;
	_main_thread.freeaddr = allocaddr;
	_main_thread.freesize = allocsize;
	return 1;
}
