/* Same real TSD-bootstrap gap ps_main_wrapper.c/uname_main_wrapper.c both
 * fix (see either file's header comment) -- real, unmodified Apple
 * `netstat` (third_party/network_cmds/netstat.tproj/main.c) has no
 * knowledge of this project's TSD-bootstrap machinery, so its own real
 * main() never calls pthread_main_thread_bootstrap() and TPIDRRO_EL0 is
 * still 0 when any TSD-backed Libc call runs.
 *
 * main.c is compiled with -Dmain=netstat_cmd_real_main (see
 * build_netstat.sh) so THIS wrapper is the actual process entry point
 * Csu's crt.c calls.
 */
extern int pthread_main_thread_bootstrap(void);
extern int netstat_cmd_real_main(int argc, char **argv);

static void die_bootstrap_failed(void) {
    register long x0 asm("x0") = 2;
    static const char msg[] = "netstat: real pthread_main_thread_bootstrap() failed\n";
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
    return netstat_cmd_real_main(argc, argv);
}
