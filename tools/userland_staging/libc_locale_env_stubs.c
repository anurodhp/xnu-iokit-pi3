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
 *   - getprogname(): no real argv[0] plumbed through yet either; returns
 *     a fixed literal identifying this specific smoketest rather than
 *     silently returning something misleading.
 */

const char *getprogname(void) {
    return "libc_locale_smoketest";
}

/* Real Apple private alias - third_party/Libc/locale/FreeBSD/collate.c
 * calls _getprogname() (underscore-prefixed), not the public getprogname()
 * directly. Same pattern as pthread_tsd_shim.c's _pthread_* aliases. */
const char *_getprogname(void) {
    return getprogname();
}

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
