/* CORRECTED (DAR-225, 2026-09-25): this constructor never runs. dyld-940
 * marks every /usr/lib/system/lib* dylib initialized without running its
 * initializers (third_party/dyld/dyld/DyldAPIs.cpp:3325-3333), confirmed
 * with a temporary status global that stayed 0 on a QEMU boot. The
 * bootstrap described below actually happens in libSystem.B.dylib's
 * constructor (libsystem_dyld_initializer_compat.c). */
/* Real gap found booting launchd.macho (a real, dynamically-linked
 * executable) as PID 1: nothing ever calls pthread_main_thread_bootstrap()
 * (pthread_main_thread_bootstrap.c) for a dylib-linked process. That
 * function is the real, already-established single entry point for
 * installing the main thread's TPIDRRO_EL0/TSD base + errno slot + real
 * pthread_t signature (see its own header comment) -- every STATIC
 * consumer (bash_main_wrapper.c, ifconfig_main_wrapper.c, ping/ps/uname/
 * getty's own wrappers, the llvm_libcxx smoketest via
 * libcxx_tsd_shim_bootstrap_compat.c's header) already calls it explicitly
 * from its own hand-rolled entry point, but a real dynamic executable has
 * no such wrapper -- dyld jumps straight to real Csu start.s/crt.c, which
 * calls main() directly. Real Apple's own equivalent (__pthread_init(),
 * libsystem_pthread.dylib's own real dyld-run image initializer) does this
 * automatically as part of loading the dylib; this project's
 * libsystem_pthread.dylib (build_libsystem_pthread.sh) never compiled
 * pthread.c's own __pthread_init() (that whole file doesn't compile
 * standalone here, see pthread_main_thread_bootstrap.c's own header), so
 * nothing ever replicated that automatic call.
 *
 * Real, concrete symptom this fixes: libc++abi's cxa_guard.cpp (the
 * compiler-generated guard for any real C++ function-local static, e.g.
 * iostream.cpp's `static DoIOSInit init_the_streams;`) calls
 * pthread_mach_thread_np(pthread_self()) the first time ANY guarded static
 * initializes anywhere in the process. Without this bootstrap having run
 * first, pthread_self() reads an never-installed TPIDRRO_EL0 and the
 * process dies silently (exit(1), no signal, no dyld/libc++abi error
 * message -- root-caused via a real multi-layer dyld/libc++abi/libpthread
 * instrumentation pass, see docs/backlog.md and project memory for the
 * full trace). This is a real dylib-initializer constructor -- runs once,
 * automatically, the first time libsystem_pthread.dylib's own real
 * dependency-order initializer walk reaches it (before libc++/ICU, which
 * both depend on libsystem_pthread.dylib and therefore initialize after
 * it -- confirmed via the same instrumentation pass), which is exactly
 * the "as early as possible, before any real pthread call" requirement
 * pthread_main_thread_bootstrap()'s own header documents. Idempotent per
 * that same header, so a redundant future caller (if one is ever added)
 * is harmless. */
extern int pthread_main_thread_bootstrap(void);

__attribute__((constructor))
static void iokit_pthread_dylib_bootstrap_ctor(void) {
    pthread_main_thread_bootstrap();
}
