/* Real, honestly-documented replacement for exactly one function that
 * real, unmodified third_party/OpenPAM/openpam/lib/openpam_dynamic.c
 * would otherwise provide: `pam_module_t *openpam_dynamic(const char *)`.
 *
 * WHY THE REAL FILE ISN'T COMPILED HERE (this is the honest module-
 * loading architectural gap build_openpam.sh's header comment and the
 * project docs point at -- read this before assuming it's a missing
 * syscall that just needs a stub):
 *
 * openpam_dynamic.c is OpenPAM's actual dynamic PAM-module loader. Its
 * real job is to dlopen() a service module (e.g. pam_unix.so), dlsym()
 * the real pam_sm_* entry points out of it, and hand back a populated
 * pam_module_t. Every one of its real dependencies is a piece of dyld's
 * runtime that structurally does not exist on this target (no dyld
 * anywhere in this project -- see architecture.md and docs/roadmap.md's
 * "Bash's own remaining honest failure stand-ins" writeup, which already
 * documents dlopen/dlclose/dlsym/dlerror as permanent honest-failure
 * stubs for the exact same reason):
 *   - dlopen()/dlsym()/dlerror() (openpam_dynamic.c:68,74,94 dlopen;
 *     :123 dlsym; :125 dlerror) -- no dyld to ask.
 *   - _dyld_shared_cache_contains_path() (:67) -- dyld shared-cache
 *     lookup API, meaningless without a shared cache.
 *   - <dispatch/dispatch.h>'s dispatch_once()/blocks (:181-206, the
 *     PAM_MODULES_DIR search-path split) -- a real GCD facility this
 *     project has never ported (a per-consumer compat shim exists,
 *     copyfile_dispatch_once_compat.h, but pulling it in here just to
 *     reach a dlopen() call that would always fail anyway isn't worth
 *     the extra surface).
 *   - <System/sys/codesign.h>'s csops()/CS_OPS_CLEAR_LV library-
 *     validation dance (:82-98) -- codesigning enforcement bypass logic
 *     that is moot when dlopen() itself can never succeed.
 *
 * Rather than fabricate a fake dlopen() that pretends to succeed, or pull
 * in three unrelated dyld/GCD/codesign subsystems just to reach a
 * function whose real-world behavior on this target is "always returns
 * NULL", this file provides that one real, honest outcome directly:
 * dynamic module loading is impossible here, full stop, so no module is
 * ever found. This is NOT a shim pretending dlopen works -- it is the
 * textually-accurate terminal state of what openpam_dynamic.c's own real
 * logic reduces to once dlopen() is (honestly) always NULL.
 *
 * openpam_load.c (real, unmodified, DOES compile in this build) calls
 * this function unconditionally at its line 73
 * (`module = openpam_dynamic(path);`) as the first thing
 * openpam_load_module() tries, before falling through to the
 * OPENPAM_STATIC_MODULES-gated openpam_static() path (not compiled in
 * either -- see build_openpam.sh's own comment on why
 * OPENPAM_STATIC_MODULES is deliberately left undefined, matching
 * Apple's own real Xcode build).
 *
 * REAL FUTURE FIX, not attempted here (out of scope for this pass):
 * OPENPAM_STATIC_MODULES support (openpam_static.c's real SET_DECLARE/
 * SET_FOREACH linker-set mechanism) would let real PAM modules
 * (pam_unix.a/pam_deny.a-equivalent, statically linked into the same
 * binary as libpam itself and registered into the _openpam_static_modules
 * linker set at link time) be found WITHOUT dlopen -- the module lookup
 * would become a real linear search over statically-registered entries
 * instead of a dlopen() call. That is a real, buildable path forward
 * (unlike dlopen, which needs dyld this project doesn't have), but it
 * needs at least one real static module (a `pam_deny`/`pam_permit`-
 * equivalent compiled the same way) to prove end-to-end, which hasn't
 * been attempted yet. Whoever picks this up next: don't reach for a fake
 * dlopen shim -- reach for OPENPAM_STATIC_MODULES + a real static module
 * archive instead.
 */

#include <security/pam_appl.h>
#include "openpam_impl.h"

pam_module_t *
openpam_dynamic(const char *path)
{
	(void)path;
	return (NULL);
}

/* openpam_load.c's openpam_release_module() also references dlclose()
 * directly (its line 106, `dlclose(module->dlh);`) inside a branch
 * guarded by `if (module->dlh == NULL) return;` immediately above it.
 * Since openpam_dynamic() above never returns a module with a non-NULL
 * dlh (it never returns a module at all), and openpam_static() is never
 * compiled in either, that guard is always true in this build -- the
 * dlclose() call is real, present, unmodified source, but genuinely
 * unreachable dead code, exactly the same "kept only to satisfy the
 * linker, real behavior never invoked" pattern already used elsewhere in
 * this project (see libmalloc_vm_mig_stubs.c, docs/roadmap.md). The real
 * SDK's <dlfcn.h> (already included by openpam_load.c) declares dlclose()
 * with the correct prototype; this just supplies the missing symbol
 * definition so the link succeeds. Matches this project's existing
 * bash_syscalls.c dlopen-family stub exactly in spirit (an honest
 * always-fails stand-in, not a fabricated success) -- reimplemented here
 * rather than shared, because bash_syscalls.c's stubs are compiled
 * directly into bash.macho, not exported from any archive this tier can
 * link against. */
int
dlclose(void *handle)
{
	(void)handle;
	return (-1);
}
