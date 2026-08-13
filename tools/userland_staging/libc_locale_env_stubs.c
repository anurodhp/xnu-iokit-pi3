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

char *getenv(const char *name) {
    (void)name;
    return 0;
}

int issetugid(void) {
    return 0;
}

int isascii(int c) {
    return (c & ~0x7F) == 0;
}

int isdigit(int c) {
    return c >= '0' && c <= '9';
}
