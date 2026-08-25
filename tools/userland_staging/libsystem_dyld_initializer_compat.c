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
 * Also calls mach_init() (for THIS dylib's own private mach_task_self_
 * copy, same real gap, see libdyld_mach_init_compat.c) and, across the
 * dylib boundary, libdyld_mach_init_compat_run() (for libdyld.dylib's
 * separate private copy -- real dyld does not run libdyld.dylib's own
 * per-image constructors at all, confirmed via a temporary diagnostic,
 * so this proven-to-run libSystem.B.dylib constructor is the real,
 * working place to reach across and initialize it too, the same
 * -Wl,-U dynamic-lookup mechanism already used for _dyld_initializer). */
extern void _dyld_initializer(void);
extern int mach_init(void);
extern void libdyld_mach_init_compat_run(void);

__attribute__((constructor))
static void libsystem_call_dyld_initializer(void) {
    /* Order matters: mach_init() must run before _dyld_initializer(),
     * not after -- _dyld_initializer() itself (real, unmodified dyld
     * source) does real VM/allocator work while registering the
     * LibSystemHelpers vtable, which can reach os_alloc_once/
     * mach_vm_map before a call placed after it ever gets a turn. */
    mach_init();
    libdyld_mach_init_compat_run();
    _dyld_initializer();
}
