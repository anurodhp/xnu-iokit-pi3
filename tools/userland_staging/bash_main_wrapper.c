/* REAL BUG found while root-causing the Eighteenth pass's real bash
 * SIGSEGV (docs/roadmap.md): real bash's shell.c:main() never calls this
 * project's pthread_main_thread_bootstrap() (it's real, unmodified Apple
 * source with zero knowledge of this project's TSD-bootstrap machinery) --
 * so pthread_main_thread_bootstrap.o (containing this project's REAL,
 * QEMU-verified pthread_once/pthread_equal/pthread_main_np/pthread_create,
 * written to replace pthread_tsd_shim.c's naive, non-_os_once-based
 * stand-ins) was never pulled into bash's link at all, since nothing
 * referenced any symbol unique to that object file. Confirmed via
 * `ld -why_load`: nothing in bash's build forces its load. Marking
 * pthread_tsd_shim.c's 4 public POSIX functions __attribute__((weak))
 * (see that file's own header) only helps once a STRONG definition is
 * actually present in the link -- if pthread_main_thread_bootstrap.o is
 * never loaded, there's nothing to override with, and the weak (naive)
 * ones win by default. This wrapper does two things real bash's own
 * source can't do for itself:
 *   1. Forces pthread_main_thread_bootstrap.o into the link for real, by
 *      calling the one function unique to it.
 *   2. Actually RUNS that real bootstrap before any Libc/locale/TSD-backed
 *      code executes -- necessary because, unlike pthread_tsd_shim.c's old
 *      pthread_getspecific()/pthread_setspecific()/pthread_key_create()
 *      (each self-bootstrapping defensively on first call, see that
 *      file's header), REAL Apple pthread_tsd.c's pthread_getspecific()
 *      calls _pthread_getspecific_direct() directly -- a raw
 *      TPIDRRO_EL0-relative read with NO defensive self-bootstrap. If
 *      TPIDRRO_EL0 is still 0 (a freshly exec'd thread, never
 *      bootstrapped), that's a NULL-based TSD read -- real Libc's own
 *      xlocale_private.h fast path documents exactly this hazard (see
 *      pthread_tsd_shim.c's header comment on pthread_tsd_shim_bootstrap()).
 *
 * real bash's shell.c is compiled with -Dmain=bash_shell_real_main (see
 * build_bash.sh) specifically so this wrapper -- not shell.c's own
 * real main() -- is the actual process entry point Csu's crt.c calls.
 * Real bash's own argc/argv/envp are passed through unchanged.
 */
extern int pthread_main_thread_bootstrap(void);
extern int bash_shell_real_main(int argc, char **argv, char **envp);

static void die_bootstrap_failed(void) {
    register long x0 asm("x0") = 2;
    static const char msg[] = "bash: real pthread_main_thread_bootstrap() failed\n";
    register long x1 asm("x1") = (long)msg;
    register long x2 asm("x2") = sizeof(msg) - 1;
    register long x16 asm("x16") = 4; /* write */
    asm volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x16) : "memory", "cc");
    register long ex0 asm("x0") = 1;
    register long ex16 asm("x16") = 1; /* exit */
    asm volatile("svc #0x80" : "+r"(ex0) : "r"(ex16) : "memory", "cc");
    __builtin_unreachable();
}

int main(int argc, char **argv, char **envp) {
    if (!pthread_main_thread_bootstrap()) {
        die_bootstrap_failed();
    }
    return bash_shell_real_main(argc, argv, envp);
}
