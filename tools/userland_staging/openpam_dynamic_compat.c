/* Real, honestly-documented replacement for exactly one function that
 * real, unmodified third_party/OpenPAM/openpam/lib/openpam_dynamic.c
 * would otherwise provide: `pam_module_t *openpam_dynamic(const char *)`.
 *
 * CORRECTED 2026-09-16 (DAR-226 - stale-assumption sweep): this file's
 * own justification used to claim "no dyld anywhere in this project" and
 * "dlopen() itself can never succeed". That has been false since Phase
 * 6.5 (2026-08-25) - real dyld-940 exists and boots, and
 * `libc_build/system/libdyld.dylib` today exports real `_dlopen`/
 * `_dlsym`/`_dlerror`/`_dlclose`/`_dlopen_preflight`/`_dlopen_from`/
 * `_dlopen_audited` (confirmed via `nm -m`, not assumed). CLAUDE.md
 * repeated the same stale claim in its build-conventions section until
 * this same pass corrected it there too - it had become documented
 * project doctrine, not just a stale comment in one file.
 *
 * WHY THIS STILL RETURNS NULL TODAY, for a genuinely different and still
 * real reason: openpam_dynamic()'s job is to dlopen() a service module
 * (e.g. pam_unix.so) by path. This project builds no PAM service module
 * .so files at all (confirmed: no pam_unix/pam_deny/pam_permit build
 * script anywhere under tools/userland_staging/, and build_openpam.sh's
 * own header already documents excluding OPENPAM_STATIC_MODULES too).
 * So even with a real, working dlopen() now available, every real path
 * this function could try genuinely does not exist on disk - dlopen()
 * would correctly return NULL with "file not found", which is exactly
 * what this stand-in already returns directly, just without an actual
 * dlopen() call (and its transitive dispatch_once()/codesign-bypass
 * dependencies, real Apple source for openpam_dynamic.c:181-206/:82-98,
 * which are only worth pulling in once there is a real .so to find).
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
 * REAL FUTURE FIX, not attempted here (out of scope for this pass) - two
 * real options now that dyld is real, not one:
 *   1. Compile a real PAM service module (pam_deny.c/pam_permit.c are
 *      the smallest real OpenPAM-shipped modules) into a real .so via
 *      build_libdyld_dylib.sh's own dylib recipe, install it where
 *      openpam_dynamic()'s real PAM_MODULES_DIR search expects it, and
 *      let the now-real dlopen()/dlsym() actually load it - the
 *      previously-blocking reason (no dyld) is gone; only the module
 *      itself is missing now.
 *   2. OPENPAM_STATIC_MODULES support (openpam_static.c's real
 *      SET_DECLARE/SET_FOREACH linker-set mechanism), letting real PAM
 *      modules be statically linked into the same binary as libpam and
 *      found via a linear search instead of dlopen() at all.
 * Whoever picks this up next: option 1 is now the more direct path,
 * since it needs no OpenPAM-internal linker-set work, just one more real
 * dylib build script.
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
 * link against.
 *
 * DYLIB BUILD: omitted entirely (see OPENPAM_DYNAMIC_COMPAT_DYLIB_VARIANT
 * below). Real dyld now exists (Phase 6.5) and libdyld.dylib's own
 * DyldAPIs.cpp (third_party/dyld/dyld/DyldAPIs.cpp, APIs::dlclose) is a
 * genuine, real implementation of this exact symbol -- there is no
 * reason for a second, honestly-fake copy to live in libopenpam.dylib
 * once it can link the real one back as a sibling (confirmed no link
 * cycle: libdyld.dylib does not depend on libopenpam.dylib, and every
 * real consumer of libopenpam.dylib in this project -- syslogd.macho --
 * already loads libdyld.dylib directly regardless, so this adds no new
 * dylib to any process's load set). The dead-code status above is
 * unchanged either way (module->dlh is always NULL here), so linking the
 * real dlclose() back in is a pure single-owner cleanup, not a behavior
 * change. build_openpam.sh's OWN static-archive build (consumed by
 * dyld.macho, which runs before any dylib -- including libdyld.dylib --
 * exists, so it cannot link one back) keeps this local stub, unchanged. */
#ifndef OPENPAM_DYNAMIC_COMPAT_DYLIB_VARIANT
int
dlclose(void *handle)
{
	(void)handle;
	return (-1);
}
#endif
