/* Real gaps confirmed absent from every dylib this project's Phase 6.5+
 * dynamic-link migration produces (build_simple_cmds.sh, 2026-08-28) --
 * checked via a real `nm -gU` sweep across libsystem_c/kernel/pthread/
 * malloc/platform.dylib before writing this, not guessed. abort()/
 * __assert_rtn()/reallocf() are real, standard libc entry points real
 * Apple's own Libc.xcodeproj ships; this project's libc_gen.a/libc_
 * stdio.a curated ports never included them (no earlier static-link
 * consumer needed them badly enough to surface the gap -- the dynamic
 * conversion's much broader real-source command set is the first real
 * consumer that does).
 */
#include <sys/types.h>

extern int getpid(void);
extern int kill(int pid, int sig);
#define SIGABRT 6

void
abort(void)
{
	kill(getpid(), SIGABRT);
	for (;;) {
		__asm__ volatile("" ::: "memory");
	}
}

void
__assert_rtn(const char *func, const char *file, int line, const char *expr)
{
	(void)func;
	(void)file;
	(void)line;
	(void)expr;
	abort();
}

extern void *realloc(void *ptr, size_t size);
extern void free(void *ptr);

void *
reallocf(void *ptr, size_t size)
{
	void *r = realloc(ptr, size);
	if (!r && size != 0) {
		free(ptr);
	}
	return r;
}

/* flockfile()/funlockfile(): real per-FILE* mutual-exclusion guard
 * against concurrent access from other threads. This tier's commands
 * only ever run single-threaded (this project's real pthread_create()
 * is functional but nothing in simple_cmds spawns a second thread), so
 * the real correctness requirement (mutual exclusion) is trivially
 * satisfied by a no-op -- same honestly-scoped-gap precedent as this
 * file's own sibling tar_pthread_testcancel_compat.c. */
void
flockfile(void *fp)
{
	(void)fp;
}

void
funlockfile(void *fp)
{
	(void)fp;
}

/* Real gap: third_party/Libc/locale/FreeBSD/setlocale.c's own
 * __open_path_locale() tries three real on-disk locale-data directories
 * (_PathLocale env-configurable path, _PATH_LOCALE, /usr/local/share/
 * locale) and opens <dir>/<subpath> -- none of those directories exist
 * on this target (no locale data files were ever ported; this project
 * is "C"/POSIX-locale-only, see collate.c/table.c's own real "C" locale
 * default tables, which need no on-disk data at all). Compiling the
 * whole of setlocale.c just for this one function would cascade into
 * real environment-variable parsing and the full setlocale() machinery
 * this target has no real use for (same bloat-cascade class libc_
 * locale_globals_compat.c's own header already documents rejecting for
 * xlocale.c) -- this is the same honest "always ENOENT, no locale data
 * on this target" real answer __open_path_locale's own real callers
 * (ldpart.c's __part_load_locale()) already treat as a normal, expected
 * failure path (falls back to the compiled-in "C" locale defaults).
 */
int
__open_path_locale(const char *subpath)
{
	(void)subpath;
	return -1;
}
