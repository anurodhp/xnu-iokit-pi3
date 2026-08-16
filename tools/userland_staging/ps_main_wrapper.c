/* Same real gap bash_main_wrapper.c/uname_main_wrapper.c both fix (see
 * either file's header): real, unmodified Apple `ps` source
 * (third_party/adv_cmds/ps/ps.c) has zero knowledge of this project's
 * TSD-bootstrap machinery -- its own real main() never calls
 * pthread_main_thread_bootstrap(), so TPIDRRO_EL0 is still 0 when any
 * TSD-backed Libc call runs (locale/printf/etc), a real SIGSEGV --
 * confirmed via a real QEMU boot-test before this wrapper was added
 * (`exec /bin/ps` and `exec /bin/ps -aux` both `terminated by signal
 * 11`, immediately, zero output).
 *
 * Same fix shape: ps.c is compiled with -Dmain=ps_cmd_real_main (see
 * build_ps.sh) so THIS wrapper -- not ps.c's own real main() -- is the
 * actual process entry point Csu's crt.c calls.
 */
extern int pthread_main_thread_bootstrap(void);
extern int ps_cmd_real_main(int argc, char **argv);

static void die_bootstrap_failed(void) {
    register long x0 asm("x0") = 2;
    static const char msg[] = "ps: real pthread_main_thread_bootstrap() failed\n";
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
    return ps_cmd_real_main(argc, argv);
}
