/* Real stand-in for _xpc_runtime_is_app_sandboxed() (real declaration in
 * xpc/private.h, part of libxpc - this project has no XPC/sandbox
 * subsystem at all, no dyld, see architecture.md's userland section).
 * xattr_flags.c's only call site uses this purely to pick between two
 * static property tables (sandboxed vs unsandboxed default xattr
 * visibility rules) - this target has no App Sandbox concept whatsoever,
 * so "not sandboxed" (0) is the real, correct answer here, not a
 * fabricated one, matching the same "stand-in for a subsystem this
 * target genuinely doesn't have" pattern as bsd_pthread_stub.c
 * (pthread.kext) and the from-scratch SHA-256/384/512 (corecrypto).
 */
int _xpc_runtime_is_app_sandboxed(void) {
    return 0;
}
