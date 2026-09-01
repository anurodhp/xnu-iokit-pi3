/* Same real gap/fix shape as getty_main_wrapper.c (see that file's own
 * header for the full original crash writeup: real Apple system_cmds
 * source has zero knowledge of this project's TSD-bootstrap machinery,
 * so TPIDRRO_EL0 is still 0 the moment any pthread/xlocale-touching real
 * libc call runs). Real, unmodified system_cmds/login.tproj/login.c is
 * compiled with -Dmain=login_cmd_real_main (see build_login.sh) so THIS
 * wrapper -- not login.c's own real main() -- is the actual process
 * entry point Csu's crt.c calls.
 */
extern int pthread_main_thread_bootstrap(void);
extern int login_cmd_real_main(int argc, char **argv);

static void die_bootstrap_failed(void) {
    register long x0 asm("x0") = 2;
    static const char msg[] = "login: real pthread_main_thread_bootstrap() failed\n";
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
    return login_cmd_real_main(argc, argv);
}
