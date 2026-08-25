/* Real gap found 2026-08-25 chasing the SIGTRAP right after the
 * _dyld_initializer() fix (see libsystem_dyld_initializer_compat.c):
 * libdyld.dylib's real, unmodified libplatform os/alloc_once.c
 * (_os_alloc_slow(), linked in per build_libdyld.sh's own header comment)
 * calls mach_vm_map(mach_task_self(), ...) the first time anything in
 * this dylib needs os_alloc_once-backed storage, and mach_task_self()
 * read back MACH_PORT_NULL -- root-caused via a temporary kernel
 * ux_exception.c PC/register diagnostic (crash PC landed exactly on
 * __os_alloc_slow's own __LIBPLATFORM_INTERNAL_CRASH__ brk, x10 held
 * kr=KERN_INVALID_ADDRESS) plus a temporary printf in
 * osfmk/ipc/mach_kernelrpc.c's _kernelrpc_mach_vm_map_trap showing this
 * exact call never even reached the kernel trap -- both reverted after
 * use.
 *
 * Real Apple explanation (libsyscall/mach/mach_init.c's own comment):
 * "mach_init() must be called explicitly in static executables
 * (including dyld)." On real macOS this happens once, process-wide,
 * because libsystem_kernel.dylib is a single shared image and dyld
 * itself calls __libkernel_init() (libsyscall/wrappers/_libkernel_init.c)
 * during real libSystem bootstrap. This project's libdyld.dylib instead
 * bundles its OWN private static copy of mach_init.c (see
 * build_libdyld.sh's own header comment on why -- this project has never
 * built libSystem as Apple's real separate libsystem_*.dylib set), so it
 * has its own private, never-initialized mach_task_self_ global separate
 * from every other image's copy (dyld.macho's own, libSystem.B.dylib's
 * own, etc). This is the "Static-vs-dylib architecture debt" pattern
 * (docs/daemon-roadmap.md) playing out again, one level deeper than the
 * _dyld_initializer() gap: real Apple's modular libsystem_kernel.dylib
 * shares ONE mach_task_self_ across the whole process; this project's
 * flattening means every dylib needs this call independently.
 *
 * First attempt (reverted): a plain __attribute__((constructor)) in this
 * file, relying on libdyld.dylib's own real __TEXT,__init_offsets
 * initializer-running mechanism (the same real, modern chained-fixups
 * scheme that already correctly runs libSystem.B.dylib's own
 * constructor, confirmed via otool -s __TEXT __init_offsets showing our
 * function's exact address as the sole entry). Diagnostic writes (raw
 * SYS_write, no allocator dependency) placed at function entry/exit
 * proved this constructor NEVER RAN -- real dyld does not run ordinary
 * per-image constructors for /usr/lib/system/libdyld.dylib itself (it's
 * handled as a special, dyld-adjacent loader via RuntimeState::
 * setDyldLoader(), not a normal client dependency -- consistent with
 * real Apple's own libdyld.dylib shipping with no C++ static
 * initializers of its own). The one proven-working cross-dylib call
 * path is the opposite direction: libSystem.B.dylib's OWN constructor
 * (which DOES run) calling an exported libdyld.dylib function by name,
 * the same real mechanism it already uses for _dyld_initializer(). This
 * function is exported (not static, no constructor attribute) so
 * libsystem_dyld_initializer_compat.c can do exactly that. */
extern int mach_init(void);

void libdyld_mach_init_compat_run(void) {
    mach_init();
}
