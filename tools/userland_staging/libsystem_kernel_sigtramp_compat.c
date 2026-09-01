/* Real gap found investigating DAR-164 (login post-exec crash): syslogd
 * and pd-console-login (both real, dyld-loaded, non-launchd children --
 * the first ones this port has ever booted via real fork()+posix_spawn
 * (SETEXEC)/exec() since DAR-163's fork-child TSD fix) each abort()
 * immediately post-exec with a real, kernel-captured dyld fatal error
 * (`abort_with_payload`, OS_REASON_DYLD/DYLD_EXIT_REASON_SYMBOL_MISSING):
 *   "Symbol not found: _sigaction ... Expected in: /usr/lib/libSystem.B.dylib"
 *   "Symbol not found: __sigaction_nobind ... Expected in: /usr/lib/system/libdispatch.dylib"
 *
 * Root cause: real Apple `sigaction()`/`_sigaction_nobind()` (Libc/sys/
 * sigaction.c, wired into libsystem_c.dylib by this same fix -- see
 * build_libsystem_c.sh's own updated comment) call real libplatform's
 * `__platform_sigaction()` (libplatform/src/setjmp/generic/sigaction.c,
 * also wired in by this fix), which needs `__sigtramp` -- the real
 * kernel-invoked signal-delivery return trampoline -- as a raw, local
 * symbol libsystem_kernel.dylib itself must export (real Apple ships it
 * there, alongside the raw `__sigaction`/`__sigreturn` traps).
 *
 * This project already has THREE independent, already-verified-working
 * copies of this exact asm body inline in other single-binary compat
 * files (ncurses_extra_syscalls.c, syslogd_misc_libc_compat.c,
 * bash_syscalls.c -- each citing the same real source:
 * third_party/xnu-7195/bsd/dev/arm/unix_signal.c's real
 * sendsig_set_thread_state64(), which documents the exact real stack
 * layout the kernel sets up before jumping to a process's registered
 * signal handler). None of those three files can be linked here without
 * pulling in unrelated symbols that would duplicate-conflict with this
 * dylib's own already-linked objects (same reasoning each of those
 * files' own header comments already gives) -- so this is a small,
 * single-purpose file with the same verbatim asm body, given a real,
 * permanent home (libsystem_kernel.dylib, matching real Apple's actual
 * architecture) instead of being copy-pasted a fourth time into another
 * single-consumer file.
 */
asm(
    ".text\n"
    ".align 2\n"
    ".globl __sigtramp\n"
    "__sigtramp:\n"
    "  stp x19, x20, [sp, #-32]!\n"
    "  stp x21, x22, [sp, #16]\n"
    "  mov x19, x1\n"      /* infostyle */
    "  mov x20, x4\n"      /* uctx */
    "  mov x21, x5\n"      /* token */
    "  mov x22, x0\n"      /* catcher */
    "  mov x0, x2\n"       /* sig */
    "  blr x22\n"
    "  mov x0, x20\n"      /* sigreturn(uctx, infostyle, token) */
    "  mov x1, x19\n"
    "  mov x2, x21\n"
    "  mov x16, #184\n"
    "  svc #0x80\n"
    "  brk #1\n"           /* sigreturn should never return */
);
