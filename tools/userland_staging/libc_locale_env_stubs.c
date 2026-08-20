/* Minimal, correct (not faked) stand-ins for a handful of process-
 * identity/environment accessors real Libc's locale-file-loading code
 * calls, needed because this target has no real argv/envp/process-image
 * plumbing yet (same class of gap as tools/userland_staging/cerror_stub.c's
 * errno stand-in, or bsd_pthread_stub.c's kernel-side pthread stand-in -
 * a real missing subsystem, not corner-cutting):
 *   - getenv(): no real environment vector exists yet for this smoketest
 *     (no envp plumbed through Csu's crt.c) - returning NULL for every
 *     name is the correct behavior for "environment variable not set",
 *     not a stub returning wrong data.
 *   - issetugid(): this process is never setuid/setgid (no real
 *     credential model exists yet at all) - 0 is the real, correct
 *     answer, not a placeholder.
 *   - getprogname(): REAL BUG FOUND+FIXED (build_simple_cmds.sh's cp/mv
 *     tier, edge-case error-path testing): this used to unconditionally
 *     return a fixed literal identifying the one original smoketest this
 *     file was written for, on the honest grounds that "no real argv[0]
 *     plumbed through yet" -- true when this file was written, but
 *     stale since Csu/crt.c's real _start() has set a real `__progname`
 *     (from the real argv[0] every one of this tier's binaries actually
 *     gets from the kernel's real execve(2)) all along. Every command in
 *     this tier links this file (via libc_locale_for_stdio.a, unlike
 *     getenv() below this was never given the weak-symbol-override
 *     escape hatch), so every err(3)/warn(3) call in every one of them
 *     was silently printing "libc_locale_smoketest: ..." instead of its
 *     own real name -- confirmed via a real QEMU transcript (`cp
 *     /nonexistent-src-file ...` printing exactly that wrong prefix)
 *     before this fix, not guessed. Real fix: return the real
 *     crt.c-computed global directly. */

extern const char *__progname;

const char *getprogname(void) {
    return __progname ? __progname : "";
}

/* Real Apple private alias - third_party/Libc/locale/FreeBSD/collate.c
 * calls _getprogname() (underscore-prefixed), not the public getprogname()
 * directly. Same pattern as pthread_tsd_shim.c's _pthread_* aliases. */
const char *_getprogname(void) {
    return getprogname();
}

/* Real Phase 4.6 update (docs/roadmap.md "Real environment variable
 * plumbing"): now `__attribute__((weak))`, same real technique the
 * Nineteenth pass's `pthread_tsd_shim.c` fix already established for
 * this exact situation (a default stub inside an archive, overridden by
 * a real strong definition when a consumer also links one directly) --
 * a consumer that provides a real getenv() reading a real `environ`
 * (see `simple_cmds_getenv_compat.c`) gets that instead; every other
 * consumer that doesn't (this stub's original callers, unchanged) still
 * gets this exact fallback, zero regression. */
__attribute__((weak))
char *getenv(const char *name) {
    (void)name;
    return 0;
}

int issetugid(void) {
    return 0;
}

/* isascii()/isdigit() are NOT stubbed here (an earlier version of this
 * file did) - third_party/Libc/locale/isctype.c is the real "externalized"
 * (compiled, non-inline) definition of the whole ctype_l family
 * (isascii/isdigit/tolower_l/toupper_l/etc), which real ctype.h switches
 * to providing as real linkable symbols instead of header-inlines once
 * __LIBC__ is defined (needed anyway for collate.h's real prototypes -
 * see docs/roadmap.md Phase 4). Hand-rolling isascii/isdigit here would
 * now collide (duplicate symbol) with isctype.o's real definitions. */
