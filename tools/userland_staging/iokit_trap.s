// Real arm64 Mach trap stub for IOKitUser's iokit_user_client_trap
// (the "fast trap" path IOConnectTrap*() used to call directly into an
// IOUserClient subclass's getTargetAndTrapForIndex(), bypassing the
// full io_connect_method() MIG round trip).
//
// third_party/IOKitUser/IOTrap.s (the real, unmodified Apple source for
// this) does NOT compile in this project: its only body is
//   #include <mach/machine/syscall_sw.h>
//   #if defined(__i386__)
//   #undef kernel_trap
//   #define kernel_trap(...) ...   /* i386-only override */
//   #endif
//   kernel_trap(iokit_user_client_trap, -100, 8)
// -- on arm64 the i386 override is skipped, so the bare macro invocation
// needs the REAL system <mach/machine/syscall_sw.h> to expand
// "kernel_trap" for arm64. That header is never installed into this
// project's Xcode-12 iPhoneOS SDK (confirmed: `find` over the whole SDK
// for syscall_sw.h returns nothing -- it's a libsyscall-build-internal
// header on real Apple platforms too, not a public SDK header), so the
// file cannot be compiled as shipped, on any arch, in this environment.
// Same real gap this project already hand-solved for
// libplatform_mach_traps.s/libmalloc_vm_traps.s/mach_ipc_traps.s -- this
// file is the same fix applied to IOKitUser's one trap-based call.
//
// Real trap number, cross-checked (not guessed) against
// third_party/xnu-7195/osfmk/kern/syscall_sw.c:206
//   /* 100 */ MACH_TRAP(iokit_user_client_trap, 8, 8, munge_wwwwwwww),
// and osfmk/mach/mach_traps.h:852-864 (struct iokit_user_client_trap_args
// + prototype) -- kernel-user trap number 100, called from userland as
// trap -100, matching IOTrap.s's own literal
// "kernel_trap(iokit_user_client_trap, -100, 8)" invocation. The
// kernel-side handler is unconditionally compiled into this project's
// real, unmodified xnu-7195 (iokit/Kernel/IOUserClient.cpp:6166,
// iokit_user_client_trap()) -- not behind any CONFIG_EMBEDDED/arch
// gate -- so the trap number and kernel handler are both real and live
// on this project's kernel.
//
// HONEST CAVEAT: osfmk/mach/mach_traps.h:851 carries the real Apple
// comment "/* not published to LP64 clients yet */" directly above this
// struct -- meaning Apple's own SDK policy never exposed this trap to
// 64-bit (arm64/x86_64) userland callers, only i386/armv7. Nothing in
// the kernel-side handler or the trap-table entry is actually arch-
// gated (confirmed by reading both), so this stub is real and callable,
// but its arm64 use was never Apple-supported/exercised end-to-end --
// treat this as an honest, unverified-at-runtime addition, not a proven
// working path. No caller in this project currently invokes it (the
// higher-level IOConnectCallMethod()/io_connect_method() MIG path is
// what IOKitLib.c would use instead, and IOKitLib.c itself cannot build
// here -- see build_iokituser.sh's own header comment).
//
// arm64's kernel_trap macro is arg-count-agnostic (num_args is metadata
// for kernel-side stack unwinding only, never referenced in the arm64
// trap-stub expansion itself) -- same real fact already established by
// libplatform_mach_traps.s/mach_ipc_traps.s's own comments -- so the
// same trivial 3-instruction mov/svc/ret sequence applies unchanged
// regardless of iokit_user_client_trap's real 8-argument signature.
// SWI_SYSCALL = 0x80 (osfmk/mach/arm/vm_param.h), same as every other
// raw trap stub in this project.

.text
.align 2
.globl _iokit_user_client_trap
_iokit_user_client_trap:
    mov x16, #-100
    svc #0x80
    ret
