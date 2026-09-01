// Real arm64 Mach trap stubs for the 4 "kernelrpc" VM traps real
// third_party/libmalloc/src/vm.c needs (mach_vm_map/mach_vm_allocate/
// mach_vm_deallocate/mach_vm_protect, via
// third_party/xnu-7195/libsyscall/mach/mach_vm.c's real client wrappers,
// which call straight through to these _trap functions before ever
// falling back to full MIG mach_msg RPC - see that file's own
// mach_vm_allocate()/mach_vm_map() etc, which try the trap first and
// only fall back to the non-trap _kernelrpc_mach_vm_*() MIG path on
// MACH_SEND_INVALID_DEST). Same class of gap as
// libplatform_mach_traps.s's thread_switch/mach_timebase_info_trap: not
// produced by this project's BSD-syscall Perl generator (that only walks
// bsd/kern/syscalls.master), and Apple ships these as macro-expanded .s,
// not plain includable userland stub source - so copied by hand from the
// exact same real macro (osfmk/mach/arm/syscall_sw.h's `kernel_trap`,
// __arm64__ branch), same as libplatform_mach_traps.s already did.
//
// Real trap numbers, cross-checked directly against
// osfmk/mach/syscall_sw.h (not guessed):
//   _kernelrpc_mach_vm_allocate_trap    trap -10
//   _kernelrpc_mach_vm_deallocate_trap  trap -12
//   _kernelrpc_mach_vm_protect_trap     trap -14
//   _kernelrpc_mach_vm_map_trap         trap -15
// (mach_vm_inherit/_read/_write/_copy etc are NOT trapped - real Apple
// mach_vm.c falls straight to full MIG RPC for those; out of scope here,
// this project's magazine allocator build only needs allocate/deallocate/
// protect/map.)
//
// arm64's kernel_trap macro is arg-count-agnostic (see that macro's own
// definition - the num_args parameter is accepted but never referenced
// in the __arm64__ branch): all traps use the same 3-instruction
// mov x16,#N / svc #0x80 / ret sequence regardless of how many of
// x0-x7 the caller populated, so no register-shuffling matters here
// (real arm64 calling convention already places all args directly in
// x0-x7 - unlike the 32-bit ARM branch of the same header, which needs
// the stack-juggling _kernel_trap_6to9 variants because 32-bit registers
// can't hold a 64-bit mach_vm_size_t/mach_vm_address_t in one slot).
// SWI_SYSCALL = 0x80 (osfmk/mach/arm/vm_param.h), same as elsewhere in
// this project. Mach traps return their result directly in x0, no
// carry-flag check (that convention is BSD-syscall-only).

.text
.align 2
.globl __kernelrpc_mach_vm_allocate_trap
__kernelrpc_mach_vm_allocate_trap:
    mov x16, #-10
    svc #0x80
    ret

.text
.align 2
.globl __kernelrpc_mach_vm_deallocate_trap
__kernelrpc_mach_vm_deallocate_trap:
    mov x16, #-12
    svc #0x80
    ret

.text
.align 2
.globl __kernelrpc_mach_vm_protect_trap
__kernelrpc_mach_vm_protect_trap:
    mov x16, #-14
    svc #0x80
    ret

.text
.align 2
.globl __kernelrpc_mach_vm_map_trap
__kernelrpc_mach_vm_map_trap:
    mov x16, #-15
    svc #0x80
    ret

// Two more real traps needed by third_party/xnu-7195/libsyscall/mach/
// mach_init.c's mach_init_doit() -- real Apple source explicitly
// documents "mach_init() must be called explicitly in static executables"
// (this project's exact situation, no dyld), called lazily on first
// malloc by libmalloc_dispatch.c rather than unconditionally from Csu
// (keeps every pre-existing non-malloc smoketest's startup path
// unchanged). Real trap numbers, cross-checked against syscall_sw.h:
//   task_self_trap   trap -28
//   mach_reply_port  trap -26
.text
.align 2
.globl _task_self_trap
_task_self_trap:
    mov x16, #-28
    svc #0x80
    ret

.text
.align 2
.globl _mach_reply_port
_mach_reply_port:
    mov x16, #-26
    svc #0x80
    ret

// mach_vm_purgable_control's own trap - not called by this project's
// magazine-allocator scope (no purgeable-VM use), but real mach_vm.c
// (unmodified, one translation unit) still references the symbol from
// mach_vm_purgable_control()'s body, so it must resolve at static-link
// time regardless of whether that function is ever called at runtime.
// Real trap number -11, cross-checked against syscall_sw.h.
.text
.align 2
.globl __kernelrpc_mach_vm_purgable_control_trap
__kernelrpc_mach_vm_purgable_control_trap:
    mov x16, #-11
    svc #0x80
    ret

// mach_port_deallocate/mach_port_mod_refs traps, needed by real
// third_party/xnu-7195/libsyscall/mach/mig_reply_port.c's
// mig_dealloc_reply_port() (reply-port cleanup after a MIG call this
// project's mach_vm.c trap-fast-path never actually needs to make in
// practice, but the real source still references these symbols
// unconditionally). Written directly (not via mach_port.c's own real
// wrapper, third_party/xnu-7195/libsyscall/mach/mach_port.c) because
// that file's #include <mach/mach_port_internal.h> is the same class of
// genuinely-absent MIG-generated header as mach_vm_internal.h (see
// libmalloc_vm_mig_stubs.c's own comment) and mach_port.c has many more
// functions/dependencies (mach_sync_ipc.h etc) than the 2 this project
// actually needs. Real trap numbers, cross-checked against
// syscall_sw.h:
//   _kernelrpc_mach_port_deallocate_trap  trap -18 (2 args)
//   _kernelrpc_mach_port_mod_refs_trap    trap -19 (4 args)
.text
.align 2
.globl __kernelrpc_mach_port_deallocate_trap
__kernelrpc_mach_port_deallocate_trap:
    mov x16, #-18
    svc #0x80
    ret

.text
.align 2
.globl __kernelrpc_mach_port_mod_refs_trap
__kernelrpc_mach_port_mod_refs_trap:
    mov x16, #-19
    svc #0x80
    ret

// thread_self_trap (DAR-163, real gap closed): the raw trap real Apple's
// mach_legacy.c mach_thread_self() wraps ("return thread_self_trap();").
// build_libmach_ipc.sh's own header previously dropped mach_thread_self()
// on the stated premise "nothing needs mach_thread_self()" -- false as of
// DAR-163's root-cause: libsystem_pthread_atfork_compat.c's real fork-
// child postinit (see that file's own header) needs it to refresh the
// __TSD_MACH_THREAD_SELF slot, same as real _pthread_main_thread_init()
// does. Real trap number -27, cross-checked against syscall_sw.h
// (adjacent to task_self_trap -28 and mach_reply_port -26, both already
// above).
.text
.align 2
.globl _thread_self_trap
_thread_self_trap:
    mov x16, #-27
    svc #0x80
    ret
