/* Real Apple sandbox spawn-attributes API (`struct sandbox_spawnattrs`,
 * `sandbox_spawnattrs_init`/`_setprofilename`/`_setcontainer`) --
 * declared by real Apple's actual system `<sandbox.h>` (part of the
 * closed-source Sandbox.framework/libsandbox, never open-sourced by
 * Apple -- confirmed absent from every pinned repo in this project,
 * same class of gap as corecrypto), used by core.c's real
 * `#if TARGET_OS_EMBEDDED` job-spawn path to hand a compiled seatbelt
 * profile name/container id to `posix_spawnattr_setmacpolicyinfo_np()`.
 *
 * third_party/PureDarwin/.../launchd/System-compat/sandbox.h (real,
 * vendored source) already establishes the correct behavior for this
 * target for the OLDER sandbox_check()/sandbox_init() API: real,
 * honest "always allow" stand-ins, since this port has no Sandbox.kext
 * policy engine to consult at all (that header's own comment says so
 * explicitly). This header extends the exact same real, already-
 * established precedent to the newer spawnattrs API: a real, minimal
 * struct large enough to hold what job_start() actually stores into it
 * (profile name + container id, both just remembered, never consulted
 * against a real policy engine that doesn't exist here) and real
 * no-op-bodied setters -- not a functional sandbox implementation
 * stand-in, since this target already has none.
 */
#ifndef _SANDBOX_SPAWNATTRS_COMPAT_H_
#define _SANDBOX_SPAWNATTRS_COMPAT_H_

#ifndef _SANDBOX_SPAWNATTRS_STRUCT_DEFINED_
#define _SANDBOX_SPAWNATTRS_STRUCT_DEFINED_
struct sandbox_spawnattrs {
	const char *sb_profile;
	const char *sb_container;
};

static inline void
sandbox_spawnattrs_init(struct sandbox_spawnattrs *attrs)
{
	attrs->sb_profile = 0;
	attrs->sb_container = 0;
}

static inline void
sandbox_spawnattrs_setprofilename(struct sandbox_spawnattrs *attrs, const char *name)
{
	attrs->sb_profile = name;
}

static inline void
sandbox_spawnattrs_setcontainer(struct sandbox_spawnattrs *attrs, const char *container)
{
	attrs->sb_container = container;
}
#endif

#endif /* _SANDBOX_SPAWNATTRS_COMPAT_H_ */
