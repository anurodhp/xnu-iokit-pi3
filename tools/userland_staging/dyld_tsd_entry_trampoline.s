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
// DAR-416 step 1b: the block is laid out as a real struct pthread_s,
// so that Apple's __pthread_init() can adopt it as the main thread, as it
// adopts dyld's _main_thread on Apple. pthread.c's __pthread_init does
// `_main_thread_ptr = _pthread_self_direct()` and crashes if that is NULL.
// This does what libpthread's _pthread_set_self_dyld() does
// (third_party/libpthread/src/pthread.c:1545-1564):
//   p->thread_id = __thread_selfid(); tsd[PTHREAD_SELF] = p;
//   tsd[ERRNO] = &p->err_no; TSD base = &p->tsd[0].
// __pthread_init() (pthread.c:1736) does not set thread_id itself; left
// at 0 (the "unowned" tid), the main thread's first lock of a recursive
// mutex looked already-owned (pthread_mutex.c:535-551).
// arm64 offsets, from the real headers (libpthread types_internal.h,
// measured with offsetof): sizeof(struct pthread_s) = 0x10e0,
// offsetof(tsd) = 224 (_PTHREAD_STRUCT_DIRECT_TSD_OFFSET),
// offsetof(thread_id) = 216 and offsetof(err_no) = 172. The 8 KiB block
// is large enough.
__dyld_start_tsd_bootstrap:
    mov     x16, #372               // SYS_thread_selfid (syscalls.master:568)
    svc     #0x80
    adrp    x9, _g_dyld_tsd_bootstrap_block@PAGE
    add     x9, x9, _g_dyld_tsd_bootstrap_block@PAGEOFF   // p (struct pthread_s)
    str     x0, [x9, #216]          // p->thread_id
    add     x0, x9, #224            // &p->tsd[0]: the TSD base
    str     x9, [x0]                // tsd[0] = __TSD_THREAD_SELF = p
    add     x1, x9, #172            // &p->err_no
    str     x1, [x0, #8]            // tsd[1] = __TSD_ERRNO
    mov     x3, #2                  // platform_syscall case 2: "set cthread self"
    mov     x16, #0x80000000        // PLATFORM_SYSCALL_TRAP_NO (matches
                                     // tsd_bootstrap.c/pthread_tsd_shim.c)
    svc     #0x80
// __TSD_MACH_THREAD_SELF (slot 3, offset 0x18) -- real gap closed
// 2026-09-01 (DAR-153, syslogd runtime SIGTRAP). This block is the TSD
// every dyld-launched process's MAIN THREAD keeps for its whole life
// (__pthread_init adopts it; only a fork child re-points TPIDRRO_EL0,
// via _pthread_main_thread_postfork_init). Before 2026-09-01 slot 3 was
// never written by anyone, so it read back 0 for every such process, and
// 0 is
// exactly OS_LOCK_NO_OWNER. That is not a benign zero:
//
//   * libplatform's os_unfair_lock stores the caller's slot-3 value as
//     the lock owner, so every acquire was cmpxchg(0 -> 0) on an
//     already-"unowned" word -- i.e. EVERY os_unfair_lock in every
//     dynamically-linked process on this port silently locked nothing.
//   * os_unfair_lock_assert_owner() compares OS_ULOCK_OWNER(value),
//     which is `value | OS_ULOCK_NOWAITERS_BIT` (lock.c:435), against
//     that same slot -- (0|1) != 0, so it fired
//     __LIBPLATFORM_CLIENT_CRASH__ (a bare `brk #1`). That is the real
//     SIGTRAP DAR-153 chased: Libnotify's _notify_lib_init_locked()
//     (notify_client.c:889) is this port's first code to actually assert
//     lock ownership, so syslogd was the first process to notice.
//   * libdispatch's own once-gates read the same slot
//     (_dispatch_tid_self() -> _dispatch_thread_port() -> slot 3), and
//     DLOCK_ONCE_UNLOCKED is also 0, so every dispatch_once() body
//     re-ran on every call instead of once.
//
// The `| NOWAITERS_BIT` above is why any substitute value must be ODD,
// not merely non-zero: real Mach port names always are. XNU's
// ipc_entry_new_rollpoint() seeds the generation at
// IE_BITS_GEN_MASK + IE_BITS_GEN_ONE (osfmk/ipc/ipc_entry.h:157) and
// ipc_entry_new_gen() steps it by IE_BITS_GEN_ONE, so a name's low byte
// is always 3 mod 4 -- bit 0 set, always. That invariant is precisely
// what lets libplatform reuse bit 0 as the has-waiters flag. So use the
// real port name, exactly like real _pthread_main_thread_init()
// (libpthread/src/pthread.c:1853) does, rather than any substitute:
// tools/userland_staging/libplatform_thread_self.c's truncated
// thread_selfid() stand-in is EVEN half the time and would have
// reproduced this same crash nondeterministically.
//
// thread_self_trap is Mach trap -27 (same raw trap
// libmalloc_vm_traps.s already exports for DAR-163's fork-child
// postinit, and the same one real mach_legacy.c's mach_thread_self()
// wraps). Inlined here rather than called: this trampoline runs before
// any relocation/binding, so it must not depend on a symbol.
    mov     x16, #-27               // thread_self_trap
    svc     #0x80
    mov     w0, w0                  // real port name is 32-bit; zero the
                                     // top half so the slot holds exactly
                                     // what _os_tsd_get_direct() returns
    adrp    x1, _g_dyld_tsd_bootstrap_block@PAGE
    add     x1, x1, _g_dyld_tsd_bootstrap_block@PAGEOFF
    add     x1, x1, #224            // TSD base (see above)
    str     x0, [x1, #24]           // __TSD_MACH_THREAD_SELF == slot 3
    b       __dyld_start

// One struct pthread_s (0x10e0 bytes, its tsd[] array included), rounded
// up to 8 KiB.
    .zerofill __DATA,__bss,_g_dyld_tsd_bootstrap_block,8192,6   // 64-byte aligned, like pthread.c's _main_thread
