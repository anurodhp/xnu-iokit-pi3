/* Real gap found+fixed 2026-08-25 debugging a SIGSEGV right after
 * dyld_test_exec's real main() returned: real dyld's own dyldbootstrap::
 * start() (dyldStartup.s->start(), real Apple source, unmodified) calls
 * main()'s return value into a REAL VIRTUAL DISPATCH through
 * RuntimeState's LibSystemHelpers pointer (state.libSystemHelpers->
 * exit(result)) -- confirmed via a temporary kernel diagnostic (NULL
 * dereference, subcode=0x0) plus a temporary RuntimeState::add()
 * diagnostic used to compute dyld's real runtime base and disassemble
 * the exact crash site.
 *
 * That pointer is only ever populated by libdyld/libdyldGlue.cpp's real
 * _dyld_initializer() (gDyld.apis->_libdyld_initialize(&sHelpers)) --
 * and _dyld_initializer()'s own real comment says exactly when that's
 * supposed to happen: "This is called during libSystem.dylib
 * initialization." On real macOS, libSystem.dylib's real Libc/init.c
 * (_libc_initializer(), registered as libSystem's own real
 * S_MOD_INIT_FUNC_POINTERS constructor, which dyld genuinely runs
 * automatically for every loaded image -- real, unmodified dyld
 * behavior) calls _dyld_initializer() explicitly as one if its own
 * setup steps. This project's libSystem.B.dylib (build_libsystem.sh) is
 * a deliberately simplified bundle of this project's existing curated
 * static libc archives with no such real Libc-side initializer glue at
 * all, so _dyld_initializer() was never called by anything -- a real,
 * structural gap in this port's simplified libSystem, not a dyld or
 * libdyld bug.
 *
 * Real, minimal, correct fix: register a real C constructor (the same
 * genuine Mach-O mechanism -- S_MOD_INIT_FUNC_POINTERS -- real Libc's
 * own init.c uses) that calls the real, already-correct
 * _dyld_initializer() itself.
 *
 * Also calls mach_init() here first. This used to need a second,
 * cross-dylib call (libdyld_mach_init_compat_run()) to separately
 * initialize libdyld.dylib's OWN private, independently-never-
 * initialized copy of mach_task_self_ -- real dyld does not run
 * libdyld.dylib's own per-image constructors at all (confirmed via a
 * temporary diagnostic), so this proven-to-run libSystem.B.dylib
 * constructor was the only place that could reach it. As of the
 * build_libsystem_kernel.sh migration (docs/daemon-roadmap.md's
 * "Static-vs-dylib architecture debt" section), both this dylib and
 * libdyld.dylib link against the same real, separate
 * libsystem_kernel.dylib instead of each bundling a private static
 * copy, so there is only ONE mach_task_self_ process-wide again (real
 * Apple's actual architecture) and a single mach_init() call here
 * suffices for both. */
extern void _dyld_initializer(void);
extern int mach_init(void);
extern int pthread_main_thread_bootstrap(void);

/* Real gap found booting launchd.macho (a real, dynamically-linked
 * executable) as PID 1, 2026-08-28: nothing ever called
 * pthread_main_thread_bootstrap() (pthread_main_thread_bootstrap.c) for a
 * dylib-linked process. That function is the real, already-established
 * single entry point installing the main thread's TPIDRRO_EL0/TSD base +
 * errno slot + real pthread_t signature -- every STATIC consumer
 * (bash_main_wrapper.c, ifconfig/ping/ps/uname/getty's own wrappers, the
 * llvm_libcxx smoketest) already calls it explicitly from its own
 * hand-rolled entry point, but a real dynamic executable has no such
 * wrapper -- dyld jumps straight to real Csu start.s/crt.c, which calls
 * main() directly.
 *
 * First attempt (reverted) put this call in a NEW constructor inside
 * libsystem_pthread.dylib's own build instead -- that never actually ran:
 * dyld's real runAllInitializersForMain() (DyldAPIs.cpp) treats every
 * "/usr/lib/system/lib*"-installed sub-dylib as already-initialized
 * (matching real macOS's shared-cache architecture, where these are all
 * really one umbrella image) and only calls beginInitializers() on them,
 * never runInitializers() -- confirmed via real dyld-level tracing
 * (Loader::runInitializersBottomUp's own early-return on an
 * already-"inited" Loader). This project's dylibs are real, SEPARATE
 * files though, so that assumption doesn't hold for them the way it does
 * on real macOS -- a real, structural mismatch, not something to fix
 * here. This constructor (libSystem.B.dylib's own local
 * S_MOD_INIT_FUNC_POINTERS entry, not a separate reexported dylib file)
 * is the one place already confirmed to actually run (see this file's
 * own _dyld_initializer() writeup above, same class of gap). Ordering:
 * matches real Apple's own mach_init()-then-__pthread_init() sequence
 * (mach_init "sets the bare minimum TSD setup... __pthread_init will
 * finish the initialization during library init" -- pthread.c's own real
 * comment on _pthread_set_self_dyld()) -- call right after mach_init(),
 * before _dyld_initializer() (which itself may touch TSD-dependent
 * os_alloc_once/allocator paths). Real, concrete symptom this fixes:
 * libc++abi's cxa_guard.cpp (the compiler-generated guard for any real
 * C++ function-local static, e.g. iostream.cpp's
 * `static DoIOSInit init_the_streams;`) calls
 * pthread_mach_thread_np(pthread_self()) the first time ANY guarded
 * static initializes anywhere in the process -- without this bootstrap
 * having run first, pthread_self() reads a never-installed TPIDRRO_EL0
 * and the process dies silently (exit(1), no signal, no dyld/libc++abi
 * error message -- root-caused via a real multi-layer dyld/libc++abi/
 * libpthread instrumentation pass, see docs/backlog.md item d10). */

/* TEMP DIAGNOSTIC: bisecting a silent exit(1)/signal-0 death with zero
 * console output anywhere, when launchd.macho (full real dep graph:
 * libdispatch/ICU/libc++) boots as PID 1. A minimal test binary linking
 * only libdyld.dylib+libSystem.B.dylib reaches main() fine (this ctor
 * runs), so mach_init()/_dyld_initializer() work in that configuration --
 * these probes bisect whether they still complete cleanly once the full
 * dep graph is present. Remove once root-caused. */
static long iokit_diag3_syscall3(long num, long a0, long a1, long a2) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x16 asm("x16") = num;
    asm volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x16) : "memory", "cc");
    return x0;
}
static void iokit_diag3(const char *msg) {
    long len = 0;
    while (msg[len] != '\0') { len++; }
    long fd = iokit_diag3_syscall3(5, (long)"/dev/console", 0x0001 | 0x20000, 0);
    if (fd < 0) { return; }
    iokit_diag3_syscall3(4, fd, (long)msg, len);
    iokit_diag3_syscall3(6, fd, 0, 0);
}

__attribute__((constructor))
static void libsystem_call_dyld_initializer(void) {
    /* Order matters: mach_init() must run before _dyld_initializer(),
     * not after -- _dyld_initializer() itself (real, unmodified dyld
     * source) does real VM/allocator work while registering the
     * LibSystemHelpers vtable, which can reach os_alloc_once/
     * mach_vm_map before a call placed after it ever gets a turn. */
    iokit_diag3("DIAG3: libsystem_call_dyld_initializer entry\n");
    mach_init();
    iokit_diag3("DIAG3: mach_init() returned\n");
    pthread_main_thread_bootstrap();
    iokit_diag3("DIAG3: pthread_main_thread_bootstrap() returned\n");
    _dyld_initializer();
    iokit_diag3("DIAG3: _dyld_initializer() returned\n");
}
