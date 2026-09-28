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
 * Also runs mach_init() here first (now via __libkernel_init(), as
 * Apple does). This used to need a second,
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

/* Main-thread setup. Until DAR-416 this port had no real libpthread and
 * called its own pthread_main_thread_bootstrap() here to install the TSD
 * base, errno slot and pthread_t (without it, libc++abi's cxa_guard.cpp
 * pthread_self() read a never-installed TPIDRRO_EL0 and the process
 * exited silently; docs/backlog.md item d10). Now dyld's entry
 * trampoline (dyld_tsd_entry_trampoline.s) lays out a struct pthread_s
 * and points TSD slot 0 at it, as Apple's _pthread_set_self_dyld() does,
 * and the real __pthread_init() below adopts it
 * (third_party/libpthread/src/pthread.c:1753-1757, __pthread_init:
 * _main_thread_ptr = _pthread_self_direct()).
 *
 * It must be called from here, not from a constructor in
 * libsystem_pthread.dylib: dyld-940's runAllInitializersForMain()
 * (DyldAPIs.cpp) treats every "/usr/lib/system/lib*" image as already
 * initialized and never runs its initializers. That matches Apple's
 * shared-cache layout, where they are one image; here they are separate
 * files. This constructor, in libSystem.B.dylib itself, is the one that
 * is confirmed to run. */

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
 * (see this file's own main-thread setup note above:
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
 * before __pthread_init() has adopted the main thread's TSD, and
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

/* DAR-310 (2026-09-25): every dynamically-linked process saw an EMPTY
 * environment. dyld does write the kernel's envp -- dyldMain.cpp:674
 * stores it through state.vars->environPtr, which is libdyld.dylib's own
 * `environ` -- but getenv()/setenv()/_NSGetEnviron() live in
 * libsystem_c.dylib and use THAT dylib's `environ` (build_libsystem_c.sh,
 * environ_storage_compat.c: a static empty array). Nothing copied one
 * into the other. Real symptom: launchd put __LAUNCHD_FD into the spawned
 * job's envp, syslogd's launch_msg(LAUNCH_KEY_CHECKIN) could not find it,
 * and syslogd exited "launchd checkin failed" on every start.
 *
 * Real Apple does this in libSystem_initializer, which dyld calls with
 * (argc, argv, envp, apple, vars) (third_party/dyld/dyld/Loader.cpp:1273):
 * Libsystem/init.c:224 passes envp/vars to _libc_initializer()
 * (Libc/sys/_libc_init.c:50), whose _program_vars_init()
 * (Libc/sys/crt_externs.c:94) points libc's environ at the process's
 * real environment. Same effect here via libsystem_c's exported
 * _NSGetEnviron() (== &its environ). It cannot be a constructor inside
 * libsystem_c.dylib: see the main-thread setup note above --
 * dyld never runs /usr/lib/system/lib* initializers
 * (DyldAPIs.cpp:3316, runAllInitializersForMain). Done first, so
 * everything later in this initializer and every other image's
 * initializer sees the real environment. Complements DAR-251 below:
 * _program_vars_init() fills _NSGetArgv()/_NSGetProgname() but, in this
 * port, not _NSGetEnviron(), which build_libsystem_c.sh keeps on
 * libsystem_c's own `environ` storage. */
extern char ***_NSGetEnviron(void);

/* DAR-251: dyld-940 passes its ProgramVars as every initializer's 5th
 * argument (third_party/dyld/dyld/DyldProcessConfig.h:67); real
 * libSystem_initializer hands it to _libc_initializer() ->
 * _program_vars_init() (Libsystem/init.c:224, Libc/sys/_libc_init.c:55),
 * which is what makes libsystem_c's _NSGetArgv()/_NSGetProgname()/etc
 * return the real values. See libsystem_c_program_vars_compat.c. */
struct ProgramVars;
extern void __libc_program_vars_init(const struct ProgramVars *vars);

/* DAR-225: the main thread's stackaddr/stackbottom come from apple[]'s
 * main_stack= string. dyld-940 passes apple[] as every initializer's 4th
 * argument (third_party/dyld/dyld/DyldProcessConfig.h:67,
 * Loader.cpp:1273), and real libSystem_initializer hands it to
 * __pthread_init() (third_party/Libsystem/init.c:221). It has to be done
 * here, not in libsystem_pthread.dylib: dyld marks every
 * /usr/lib/system/lib* dylib initialized without running its initializers
 * (third_party/dyld/dyld/DyldAPIs.cpp:3325-3333). */
/* DAR-416 step 1b: Apple's libSystem_initializer sequence
 * (third_party/Libsystem/init.c:170-221). __libkernel_init() stores the
 * function table libsystem_kernel's _libc_funcptr.c thunks call through
 * (malloc, _pthread_exit_if_canceled, ...), then runs mach_init(). Then
 * __pthread_init() adopts the main thread that dyld's entry trampoline laid
 * out (dyld_tsd_entry_trampoline.s), reads the main stack from apple[], and
 * registers with the kernel (bsdthread_register). This replaces this
 * port's own mach_init() + pthread_main_thread_bootstrap() +
 * pthread_main_thread_init_stack() calls. The two table types are copied
 * field for field from xnu libsyscall/wrappers/_libkernel_init.h
 * (struct _libkernel_functions, version 4) and libpthread
 * private/pthread/private.h:34-39 (struct _libpthread_functions, version
 * 2); this file is compiled without those headers, like the rest of it. */
struct ProgramVars;
struct libsystem_libkernel_functions {
    unsigned long version;
    void *(*dlsym)(void *, const char *);
    void *(*malloc)(unsigned long);
    void (*free)(void *);
    void *(*realloc)(void *, unsigned long);
    void (*_pthread_exit_if_canceled)(int);
    void *reserved1, *reserved2, *reserved3, *reserved4, *reserved5;
    void (*pthread_clear_qos_tsd)(unsigned int);
    int (*pthread_current_stack_contains_np)(const void *, unsigned long);
};
struct libsystem_libpthread_functions {
    unsigned long version;
    void (*exit)(int);
    void *(*malloc)(unsigned long);
    void (*free)(void *);
};
extern void __libkernel_init(const struct libsystem_libkernel_functions *fns,
    const char *envp[], const char *apple[], const struct ProgramVars *vars);
extern void __pthread_init(const struct libsystem_libpthread_functions *fns,
    const char *envp[], const char *apple[], const struct ProgramVars *vars);
extern void *dlsym(void *, const char *);
extern void *malloc(unsigned long);
extern void free(void *);
extern void *realloc(void *, unsigned long);
extern void exit(int);
extern void _pthread_exit_if_canceled(int);
extern void _pthread_clear_qos_tsd(unsigned int);
extern int pthread_current_stack_contains_np(const void *, unsigned long);

__attribute__((constructor))
static void libsystem_call_dyld_initializer(int argc, const char *argv[],
                                            const char *envp[],
                                            const char *apple[],
                                            const struct ProgramVars *vars) {
    (void)argc; (void)argv;
    static const struct libsystem_libkernel_functions libkernel_funcs = {
        .version = 4,
        .dlsym = dlsym,
        .malloc = malloc,
        .free = free,
        .realloc = realloc,
        ._pthread_exit_if_canceled = _pthread_exit_if_canceled,
        .pthread_clear_qos_tsd = _pthread_clear_qos_tsd,
        .pthread_current_stack_contains_np = pthread_current_stack_contains_np,
    };
    static const struct libsystem_libpthread_functions libpthread_funcs = {
        .version = 2,
        .exit = exit,
        .malloc = malloc,
        .free = free,
    };

    if (envp != 0)
        *_NSGetEnviron() = (char **)envp;
    /* Apple's order (init.c:215-238): kernel (incl. mach_init), pthread,
     * libc, then dyld. mach_init() must still precede _dyld_initializer(),
     * which does VM/allocator work while registering its LibSystemHelpers
     * vtable. */
    __libkernel_init(&libkernel_funcs, envp, apple, vars);
    __pthread_init(&libpthread_funcs, envp, apple, vars);
    __libc_program_vars_init(vars);
    _dyld_initializer();

    /* Last: needs mach_init()'s mach_task_self_/_task_reply_port AND
     * the main thread's TSD (dyld trampoline + __pthread_init) in place,
     * because this is a real MIG round-trip to the task port. */
    (void)task_get_special_port(task_self_trap(),
                                __IOKIT_TASK_BOOTSTRAP_PORT,
                                &bootstrap_port);
}
