// Real, first-exercise gap found 2026-08-25 debugging a SIGSEGV in a
// dyld-940 (this project's pinned tag) test boot: real dyld's own
// third_party/dyld/dyld/dyldStartup.s __dyld_start never sets up
// TPIDRRO_EL0 (the pthread TSD base register) itself -- on a real Apple
// system that is always already valid by the time dyld makes its first
// MIG-based Mach call (mig_get_reply_port(), libsyscall/mach/
// mig_reply_port.c, real Apple source, confirmed by reading it directly),
// because libSystem.dylib is ALWAYS one of dyld's own dependencies and
// its initializer bootstraps TSD before any other code runs. This
// project's minimal dyld-940 test setup (Phase 6.5 Phase C) has no real
// libSystem.dylib yet (that is a distinct, larger future port), so
// nothing ever calls the "set cthread self" bootstrap before dyld's real
// code reaches _mig_get_reply_port()'s `_os_tsd_get_direct(__TSD_MIG_REPLY)`
// -- confirmed via a temporary bsd/uxkern/ux_exception.c diagnostic plus
// a temporary bsd/kern/mach_loader.c load_dylinker() diagnostic printing
// both images' real slid load ranges: the crash PC (0x100dbcb40 in one
// run) fell inside DYLD's own mapped range, not the test executable's,
// and disassembly of that exact PC in dyld.macho is
// `_mig_get_reply_port`'s `ldr x9, [x9, x10, lsl #3]` with x9=TPIDRRO_EL0
// (masked) == 0 and x10==2 (__TSD_MIG_REPLY slot) -- 2*8 = 0x10, exactly
// matching the kernel's reported fault subcode (0x10). Real NULL+0x10
// dereference, not a random/uninitialized-memory bug.
//
// Fix: this project already has a real, QEMU-verified mechanism for this
// exact bootstrap (tools/userland_staging/pthread_tsd_shim.c's
// platform_syscall_set_cthread_self(), case 2 of
// osfmk/arm64/machine_routines.c's platform_syscall() ->
// osfmk/arm/machdep_call.c's thread_set_cthread_self() ->
// osfmk/arm/pcb.c's machine_thread_set_tsd_base() -- this project's own
// project-specific pthread-bootstrap path, not a real Apple userland ABI,
// used everywhere else in this project's userland because there is no
// real launchd/libSystem-init pipeline to do it for us). Real dyld's own
// pinned source (third_party/dyld) is left completely untouched --
// instead this file provides a NEW entry symbol
// (__dyld_start_tsd_bootstrap) that build_dyld_link.sh points the link's
// `-e` flag at instead of dyld's own `__dyld_start`. It does the
// platform_syscall bootstrap using a small static scratch TSD block (this
// process is single-threaded and dyld itself never touches any TSD slot
// beyond __TSD_MIG_REPLY, so a generously-sized static block is
// sufficient -- no malloc available this early anyway) and then branches
// (not calls -- __dyld_start itself sets lr=0 and never expects to
// return here) straight into the real, unmodified __dyld_start with SP
// untouched, exactly as the kernel's own load_dylinker()/exec handoff
// left it (dyldStartup.s's own __dyld_start computes `mov x0, sp` itself
// as its very first real instruction -- it does not consume any
// incoming register argument, so this trampoline is free to clobber
// x0-x3/x9/x16 beforehand without disturbing anything __dyld_start needs).
    .text
    .align 2
    .globl __dyld_start_tsd_bootstrap
__dyld_start_tsd_bootstrap:
    adrp    x0, _g_dyld_tsd_bootstrap_block@PAGE
    add     x0, x0, _g_dyld_tsd_bootstrap_block@PAGEOFF
    mov     x3, #2                  // platform_syscall case 2: "set cthread self"
    mov     x16, #0x80000000        // PLATFORM_SYSCALL_TRAP_NO (matches
                                     // tsd_bootstrap.c/pthread_tsd_shim.c)
    svc     #0x80
    b       __dyld_start

// Header (matches pthread_tsd_shim.c's TSD_HEADER_SIZE) + a generous
// slot area -- dyld itself only ever indexes __TSD_MIG_REPLY (slot 2),
// but this block must be large enough that a stray direct-TSD read from
// any other reserved slot index Libsyscall might touch during dyld's own
// early bootstrap (e.g. errno, if any linked object still reads it via
// direct TSD this early) lands in real zeroed memory, not past the end
// of this block.
    .zerofill __DATA,__bss,_g_dyld_tsd_bootstrap_block,8192,4
