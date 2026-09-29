/* Real exit()/__cleanup wiring + a real isatty() (see its own header
 * comment further down for why it's a local raw-ioctl reimplementation
 * rather than compiling real Libc/gen/FreeBSD/isatty.c directly here) -
 * none of this project's earlier tiers needed them (the locale tier's own hand-
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
 * exit(2) syscall. __cleanup's real assignment above means real stdio
 * buffers genuinely get flushed on exit, not skipped.
 *
 * DAR-129: exit() now runs __cxa_finalize(NULL) before __cleanup, in the
 * order of real Libc (third_party/Libc/stdlib/FreeBSD/exit.c:66-69), so
 * atexit()/__cxa_atexit() handlers and C++ static destructors run. The
 * reference is weak because libc_stdio.a is also linked into images that
 * have no __cxa_finalize provider (only libsystem_c.dylib's
 * libsystem_c_cxa_atexit_compat.c defines it here); there the call is
 * skipped, as before. The _tlv_exit() call at exit.c:62 is x86-only.
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
extern void __cxa_finalize(void *) __attribute__((weak_import));

void exit(int status) {
    if (__cxa_finalize) {
        __cxa_finalize(0);
    }
    if (__cleanup) {
        __cleanup();
    }
    raw_exit_syscall(status);
}

/* Real isatty() (2026-08-19, replacing the old honest-`return 0` stand-
 * in) - the "no tty/ioctl layer exists" assumption that stand-in used to
 * document was never actually tested and turned out to be wrong: a
 * disposable ioctl(TIOCGETA/TIOCSETA) probe (see libc_gen_syscalls.c's
 * ioctl/_ioctl header comment for the full transcript-backed writeup)
 * proved third_party/xnu-7195's real BSD tty stack is already fully
 * wired under /dev/console.
 *
 * This mirrors the real third_party/Libc/gen/FreeBSD/isatty.c's own
 * fallback branch exactly (`tcgetattr(fd, &t) != -1`) rather than
 * compiling that file directly here: isatty.c's fast path needs
 * <sys/conf.h>'s D_TTY (a kernel-only header not in the iPhoneOS SDK),
 * and more importantly tcgetattr() itself only exists in libc_gen.a (a
 * LATER tier - see build_libc_gen.sh) which this file's own tier
 * (build_libc_stdio.sh) has to link clean WITHOUT, since its standalone
 * smoketest proves libc_stdio.a is self-contained before libc_gen.a is
 * even built in a from-scratch run. So this calls the same real
 * ioctl(2) TIOCGETA syscall tcgetattr() itself is just a thin wrapper
 * around (see third_party/Libc/gen/FreeBSD/termios.c's own
 * `return (_ioctl(fd, TIOCGETA, t));` body), via a locally-scoped raw
 * syscall helper rather than a public `ioctl` symbol - a second
 * `ioctl`/`_ioctl` definition here would collide with libc_gen.a's real
 * one the moment both link into the same binary (bash, ps). */
#include <termios.h>

static long isatty_raw_tiocgeta(int fd, struct termios *t) {
    register long x0 asm("x0") = fd;
    register long x1 asm("x1") = (long)TIOCGETA;
    register long x2 asm("x2") = (long)t;
    register long x16 asm("x16") = 54; /* #54 AUE_IOCTL, see libc_gen_syscalls.c */
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x2), "r"(x16)
                 : "memory", "cc");
    return is_error ? -1 : x0;
}

int isatty(int fd) {
    struct termios t;
    return isatty_raw_tiocgeta(fd, &t) != -1;
}
