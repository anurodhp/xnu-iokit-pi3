/* Real stand-in for _xpc_runtime_is_app_sandboxed() (real declaration in
 * xpc/private.h, part of libxpc's private API surface).
 *
 * UPDATE 2026-08-31 (static-linking cleanup, libcopyfile-vs-libxpc
 * ownership investigation): this comment used to say "this project has
 * no XPC/sandbox subsystem at all, no dyld" -- stale as of Phase 6.5,
 * real dyld and a real libxpc.dylib (build_libxpc_dylib.sh) both exist
 * now, and libxpc.dylib's own xpc_private.o (compiled from real
 * third_party/PureDarwin source) genuinely defines this exact same
 * symbol -- confirmed via dup_symbol_audit.sh --explain and nm -m, a
 * real dylib-vs-dylib duplicate, DAR-9/DAR-12.
 *
 * NOT consolidated onto libxpc.dylib despite that: libcopyfile.dylib is
 * reexported wholesale by libSystem.B.dylib (build_libsystem.sh), which
 * every one of this project's ~70 simple commands links -- adding
 * libxpc.dylib as a new sibling dependency here would force real dyld
 * to load the entire libxpc/libdispatch dependency graph into every
 * one of those processes at startup, not just XPC-using ones (launchd/
 * syslogd), a real behavior/architecture change wildly disproportionate
 * to what this fixes (both copies are the same one-line `return 0;`,
 * pure code, zero shared state -- unlike the __progname/arc4random/errno
 * class of duplicate this sweep otherwise prioritizes). Kept as a
 * consciously-documented structural exception, same shape as
 * libsystem_malloc.dylib's own memcpy/memset precedent (DAR-13) and
 * libunwind.dylib's dyld-unwind trio (DAR-18) -- not an oversight.
 *
 * Real PureDarwin's own xpc_private.c (third_party/PureDarwin/src/
 * Libraries/XPC/libxpc/xpc_private.c) independently arrives at the same
 * unconditional `return 0;` -- real Apple never open-sourced the actual
 * sandbox-aware implementation, so this project's existing stand-in
 * already matches the best available real reference, not a guess.
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
