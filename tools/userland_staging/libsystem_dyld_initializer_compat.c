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

/* Real gap found+fixed 2026-09-02 (DAR-217), root-causing syslogd's
 * exit(1) loop: NOTHING in this project ever initialized the process's
 * `bootstrap_port` global, so every dyld-loaded process reached main()
 * with bootstrap_port == MACH_PORT_NULL and every bootstrap_*() call in
 * it failed MACH_SEND_INVALID_DEST (0x10000003). Concretely, syslogd's
 * launch_config() (third_party/syslog/syslogd.tproj/syslogd.c) does a
 * real launchd check-in whose LAUNCH_JOBKEY_MACHSERVICES leg runs
 * libxpc.dylib's launch_mach_checkin_service() ->
 * bootstrap_check_in(bootstrap_port, "com.apple.system.logger", &p);
 * with bootstrap_port == 0 that returns MACH_SEND_INVALID_DEST, the
 * service's launch_data_t keeps its default 0 machport,
 * global.server_port stays 0, and the next mach_port_move_member() on
 * port 0 fails -> exit(1) -> launchd respawn loop. Confirmed by a real
 * QEMU boot with a temporary diagnostic in launch_mach_checkin_service:
 *   "bootstrap_check_in(bp=0x0, com.apple.system.logger) -> kr=0x10000003"
 *
 * The kernel side was never the problem: launchd's runtime_fork()
 * (PureDarwin launchd/runtime.c) really does launchd_set_bport(
 * j->mgr->jm_port) before fork(), XNU's exec path really does preserve
 * task->itk_bootstrap across execve (bsd/kern/kern_exec.c only calls
 * ipc_task_reset() on a sugid exec, and ipc_task_reset() does not touch
 * itk_bootstrap at all), and launchd's own forked child really does
 * call libxpc's bootstrap_init() before job_start_child() -- but that
 * child then execs, and the FRESH image has a fresh, zeroed
 * bootstrap_port that nothing ever fills in again.
 *
 * On real macOS this is libSystem.dylib's job: libSystem_initializer()
 * calls _libxpc_initializer(), which reads TASK_BOOTSTRAP_PORT into
 * bootstrap_port (the same two lines libxpc's own bootstrap_init(),
 * PureDarwin libxpc/libbootstrap.c, still implements verbatim). This
 * project's libSystem.B.dylib does not link libxpc.dylib, and a
 * constructor placed inside libxpc.dylib itself would never run at all
 * (see this file's own pthread_main_thread_bootstrap() writeup above:
 * dyld-940's runAllInitializersForMain() treats every
 * "/usr/lib/system/lib*" image as already-initialized), so the real
 * equivalent is to do it here, inline, in the one constructor that is
 * confirmed to run -- writing the ONE real bootstrap_port, the
 * libsystem_kernel.dylib copy that libxpc.dylib's own <bind> resolves
 * to (verified: `dyld_info -fixups libxpc.dylib` shows
 * "bind libsystem_kernel/_bootstrap_port").
 *
 * Deliberately NOT a copy of bootstrap_init()'s abort()-on-failure: PID
 * 1 (launchd) legitimately has no bootstrap port -- task_get_special_port
 * succeeds and hands back MACH_PORT_NULL there -- and launchd's own
 * launchd.c/core.c already handle bootstrap_port == 0 as the "I am the
 * root bootstrap" case. A hard failure here is a real error but not one
 * worth killing every process over, so it just leaves the port null,
 * exactly as before this fix.
 *
 * Deliberately NOT placed in libsyscall's own mach_init_doit() (where
 * the bootstrap_port global actually lives, and where Apple's much older
 * Libc mach_init_ports() really did do this): mach_init_doit() runs
 * before pthread_main_thread_bootstrap() installs TPIDRRO_EL0, and
 * task_get_special_port() is a real MIG call whose mig_get_reply_port()
 * reads the direct TSD slot -- it would fault. mach_init_doit() is also
 * what _mach_fork_child() calls, and adding a MIG round-trip to the
 * fork-child path is exactly the hazard
 * libsystem_pthread_atfork_compat.c already documents. */
typedef unsigned int __iokit_mach_port_t;   /* mach_port_t */
extern __iokit_mach_port_t bootstrap_port;  /* libsystem_kernel.dylib */
extern __iokit_mach_port_t task_self_trap(void);
extern int task_get_special_port(__iokit_mach_port_t task, int which_port,
                                 __iokit_mach_port_t *special_port);
#define __IOKIT_TASK_BOOTSTRAP_PORT 4   /* osfmk/mach/task_special_ports.h */

/* DAR-225: the main thread's stackaddr/stackbottom come from apple[]'s
 * main_stack= string. dyld-940 passes apple[] as every initializer's 4th
 * argument (third_party/dyld/dyld/DyldProcessConfig.h:67,
 * Loader.cpp:1273), and real libSystem_initializer hands it to
 * __pthread_init() (third_party/Libsystem/init.c:221). It has to be done
 * here, not in libsystem_pthread.dylib: dyld marks every
 * /usr/lib/system/lib* dylib initialized without running its initializers
 * (third_party/dyld/dyld/DyldAPIs.cpp:3325-3333). */
extern int pthread_main_thread_init_stack(const char *apple[]);

__attribute__((constructor))
static void libsystem_call_dyld_initializer(int argc, const char *argv[],
                                            const char *envp[],
                                            const char *apple[]) {
    (void)argc; (void)argv; (void)envp;
    /* Order matters: mach_init() must run before _dyld_initializer(),
     * not after -- _dyld_initializer() itself (real, unmodified dyld
     * source) does real VM/allocator work while registering the
     * LibSystemHelpers vtable, which can reach os_alloc_once/
     * mach_vm_map before a call placed after it ever gets a turn. */
    mach_init();
    pthread_main_thread_bootstrap();
    pthread_main_thread_init_stack(apple);
    _dyld_initializer();

    /* Last: needs mach_init()'s mach_task_self_/_task_reply_port AND
     * pthread_main_thread_bootstrap()'s TSD base already installed,
     * because this is a real MIG round-trip to the task port. */
    (void)task_get_special_port(task_self_trap(),
                                __IOKIT_TASK_BOOTSTRAP_PORT,
                                &bootstrap_port);
}
