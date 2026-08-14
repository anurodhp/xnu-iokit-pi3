/* Real exit()/__cleanup wiring + a documented isatty() stand-in - none of
 * this project's earlier tiers needed them (the locale tier's own hand-
 * rolled libc_locale_stdio.c FILE* shim never called real fopen(), and
 * nothing before this tier called exit() through real Libc code paths),
 * but real fopen()-family FILE*s do: third_party/Libc/stdio/FreeBSD/
 * findfp.c's __sinit() sets `__cleanup = _cleanup` (both real, already
 * compiled into libc_stdio.a - _cleanup() calls _fwalk(__sflush) to flush
 * every still-open stream), and Apple's Csu crt.c plus real Libc error
 * paths (e.g. locale/FreeBSD/collate.c's ___collate_err) call exit()
 * directly - real Apple's own body for exit() lives in Libc's stdlib/
 * (not stdio/, out of this tier's file list), so this is a real,
 * minimal, from-scratch implementation of the same contract: run the
 * cleanup hook if one is registered, then a real, never-returning
 * exit(2) syscall. Not a stub - this IS the correct exit() behavior for
 * a process with no atexit()/atfork chain (neither is ported yet
 * either), and __cleanup's real assignment above means real stdio
 * buffers genuinely get flushed on exit, not skipped.
 */

/* Real exit(2) syscall number (bsd/kern/syscalls.master #1, AUE_EXIT),
 * cross-checked the same way every other raw syscall in this project is
 * - see tools/init_binary/init.c's own header comment for the carry-flag
 * ABI this doesn't even need to check (exit never returns on success,
 * and can't meaningfully fail). */
static void raw_exit_syscall(int code) __attribute__((noreturn));
static void raw_exit_syscall(int code) {
    register long x0 asm("x0") = code;
    register long x16 asm("x16") = 1;
    asm volatile("svc #0x80" ::"r"(x0), "r"(x16) : "memory");
    for (;;) {
        /* real exit(2) never returns; this is unreachable in practice,
         * kept only so the compiler doesn't need to trust an infinite
         * asm block has no fallthrough. */
    }
}

/* Storage for local.h's `extern void (*__cleanup)(void);` - real Apple
 * ships this in a different stdlib/ file (crt-glue adjacent, not
 * findfp.c), which this tier's file list doesn't include; findfp.c's
 * __sinit() assigns it for real (see this file's header comment). */
void (*__cleanup)(void) = 0;

void exit(int status) __attribute__((noreturn));
void exit(int status) {
    if (__cleanup) {
        __cleanup();
    }
    raw_exit_syscall(status);
}

/* No tty/ioctl layer exists anywhere in this project's userland yet (see
 * docs/roadmap.md Phase 4) - real, documented "not a tty" answer, not a
 * guess: third_party/Libc/stdio/FreeBSD/makebuf.c's __smakebuf() only
 * uses isatty() to choose line-buffered vs fully-buffered, and fully-
 * buffered (the real non-tty answer) is always a SAFE, correct choice
 * for any real fd - just less convenient for interactive line-by-line
 * flushing until a real ioctl/tty subsystem is ported. */
int isatty(int fd) {
    (void)fd;
    return 0;
}
