/* Real gap: real, unmodified mach_init.c calls pthread_set_self() (this
 * narrowly-scoped dylib doesn't link real libpthread) and real,
 * unmodified mig_reply_port.c's mig_dealloc_reply_port() calls
 * mach_port_deallocate()/mach_port_mod_refs() (real mach_port.c needs a
 * MIG-generated header this project's build doesn't produce -- same
 * class of gap libmalloc_vm_traps.s's own comment documents for the
 * _trap-suffixed versions of these same two calls). Both are copied
 * verbatim from tools/userland_staging/libmalloc_dispatch.c, which
 * already established these exact real, minimal fixes for its own
 * (separate, static-archive) consumers -- not linking that whole file
 * here since it also carries a lot of malloc-specific glue with no
 * business being in the kernel-trap dylib. */
void _pthread_set_self(void *p) {
    (void)p;
}

typedef unsigned int mach_port_name_t;
typedef mach_port_name_t ipc_space_t;
typedef unsigned int mach_port_right_t;
typedef int mach_port_delta_t;
extern int _kernelrpc_mach_port_deallocate_trap(ipc_space_t task, mach_port_name_t name);
extern int _kernelrpc_mach_port_mod_refs_trap(ipc_space_t task, mach_port_name_t name, mach_port_right_t right, mach_port_delta_t delta);

int mach_port_deallocate(ipc_space_t task, mach_port_name_t name) {
    return _kernelrpc_mach_port_deallocate_trap(task, name);
}

int mach_port_mod_refs(ipc_space_t task, mach_port_name_t name, mach_port_right_t right, mach_port_delta_t delta) {
    return _kernelrpc_mach_port_mod_refs_trap(task, name, right, delta);
}
