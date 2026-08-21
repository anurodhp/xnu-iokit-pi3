/* Same real gap uname_main_wrapper.c's own header documents in detail
 * (see that file): real, unmodified Apple `getty` source (third_party/
 * system_cmds/getty.tproj/main.c) has zero knowledge of this project's
 * TSD-bootstrap machinery -- its own real main() never calls it, so
 * TPIDRRO_EL0 is still 0 when gettable()'s cgetent() -> getent() calls
 * __current_locale() (xlocale_private.h), an immediate, zero-output
 * SIGSEGV -- confirmed via a real QEMU boot-test + bisection (temporary
 * write()-marker instrumentation of a scratch, git-reverted copy of
 * main.c/subr.c/getcap.c, never committed) that narrowed the crash to
 * exactly the cgetent() call inside gettable("default"), before this
 * wrapper was added.
 *
 * Same fix shape: getty.tproj's four real sources are compiled with
 * -Dmain=getty_cmd_real_main (see build_getty.sh) so THIS wrapper -- not
 * main.c's own real main() -- is the actual process entry point Csu's
 * crt.c calls.
 */
extern int pthread_main_thread_bootstrap(void);
extern int getty_cmd_real_main(int argc, char **argv);

static void die_bootstrap_failed(void) {
    register long x0 asm("x0") = 2;
    static const char msg[] = "getty: real pthread_main_thread_bootstrap() failed\n";
    register long x1 asm("x1") = (long)msg;
    register long x2 asm("x2") = sizeof(msg) - 1;
    register long x16 asm("x16") = 4; /* write */
    asm volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x16) : "memory", "cc");
    register long ex0 asm("x0") = 1;
    register long ex16 asm("x16") = 1; /* exit */
    asm volatile("svc #0x80" : "+r"(ex0) : "r"(ex16) : "memory", "cc");
    __builtin_unreachable();
}

int main(int argc, char **argv) {
    if (!pthread_main_thread_bootstrap()) {
        die_bootstrap_failed();
    }
    return getty_cmd_real_main(argc, argv);
}
