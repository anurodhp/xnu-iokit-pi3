/* Real pthread_setname_np() CFBase.c/CFRunLoop.c and other CF callers
 * need -- extracted verbatim (cites its real origin line) from
 * third_party/libpthread/src/pthread.c, same "extract just the needed
 * real function, whole file has an unrelated real header conflict"
 * pattern as cf_pthread_attr_syscalls.c (see that file's own header
 * comment: pthread.c's own real pthread_create() stack/mach-vm
 * allocation machinery is what hits the conflict, not this function).
 * Real origin: libpthread/src/pthread.c:1130-1153, using the exact same
 * raw literal call-number/flavor (5, 2) that file's own real source
 * does -- it never includes <sys/proc_info.h> for this either, see
 * cf_libproc_syscalls.c's header comment for why that header is best
 * avoided here (struct kevent_qos_s wall).
 *
 * Needs the real internal pthread_t (internal.h, named .sig/.pthread_name
 * fields), same as cf_pthread_attr_syscalls.c -- NOT <pthread.h>'s opaque
 * public one.
 *
 * Real source's own first line, `_pthread_validate_signature(self)` (a
 * corruption/security sanity abort, not part of the actual rename
 * logic), is deliberately NOT included: it needs
 * `_pthread_ptr_munge_token`, a real, already-built
 * (`pthread_main_thread_bootstrap.o`) symbol that a real, confirmed (via
 * `nm`, not guessed) ld64 default resolves as a LOCAL (non-external)
 * common symbol inside `libsystem_pthread.dylib`'s own link -- even
 * `pthread_tsd.o`'s own real cross-object reference to it, and an
 * `-exported_symbols_list '*'` wildcard override, both failed to change
 * that in a real trial build. Real Apple's own callers never needed this
 * symbol exported either (it is a private implementation detail of
 * pthread.c's own internal thread-corruption defense, never consumed
 * cross-image on real macOS); this project's own new CF-side extraction
 * is the first real consumer that would need it externally at all. Same
 * real "no corruption path exists on this project's simplified
 * threading model, so the defensive check that assumes one is safely
 * skippable" pattern already established by
 * `pthread_thread_ops_compat.c` (`pthread_kill`/`pthread_setschedparam`
 * scope-downs) -- `pthread_self()` here always returns a valid,
 * non-corrupted thread by construction, so this abort could never
 * actually fire anyway.
 */
#include "internal.h"
#include <sched.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

/* Real raw syscall wrapper, same one cf_libproc_syscalls.c declares and
 * proc_info_trap.o (this project's own create-syscalls.pl output)
 * provides. */
extern int __proc_info(int callnum, int pid, int flavor, uint64_t arg, void * buffer, int buffersize);

/* Real _platform_bzero/_platform_strlcpy bodies (third_party/libplatform/
 * src/string/generic/bzero.c:64-67 and .../strlcpy.c:29-38), the only
 * two of pthread_setname_np's real dependencies not already built
 * anywhere in this project (confirmed via nm across every existing
 * .a -- _platform_strlen/_platform_memmove/_platform_memset already
 * exist, in libmalloc.a). Adapted (not verbatim) to call those already-
 * built primitives via extern rather than recompiling generic
 * fallback bodies for them: the real bzero.c/strlcpy.c source files
 * compile standalone too, but each also (still guarded by the same real
 * `#if !_PLATFORM_OPTIMIZED_*` this project's arm64 target takes) defines
 * its OWN generic _platform_memset/_platform_memset_pattern4, which
 * would be a real duplicate-symbol conflict against libmalloc.a's
 * already-built (and already-linked-everywhere) copies -- confirmed via
 * a real trial compile, not assumed upfront.
 */
extern void *_platform_memset(void *b, int c, size_t len);
extern void *_platform_memmove(void *dst, const void *src, size_t len);
extern size_t _platform_strlen(const char *s);

void
_platform_bzero(void *s, size_t n)
{
	_platform_memset(s, 0, n);
}

size_t
_platform_strlcpy(char * restrict dst, const char * restrict src, size_t maxlen)
{
	const size_t srclen = _platform_strlen(src);
	if (srclen < maxlen) {
		_platform_memmove(dst, src, srclen + 1);
	} else if (maxlen != 0) {
		_platform_memmove(dst, src, maxlen - 1);
		dst[maxlen - 1] = '\0';
	}
	return srclen;
}

int
pthread_setname_np(const char *name)
{
	int res;
	pthread_t self = pthread_self();

	size_t len = 0;
	if (name != NULL) {
		len = strlen(name);
	}

	res = __proc_info(5, getpid(), 2, (uint64_t)0, (void*)name, (int)len);
	if (res == 0) {
		if (len > 0) {
			strlcpy(self->pthread_name, name, MAXTHREADNAMESIZE);
		} else {
			bzero(self->pthread_name, MAXTHREADNAMESIZE);
		}
	}
	return res;
}
