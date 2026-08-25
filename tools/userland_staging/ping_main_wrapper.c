/* Same real TSD-bootstrap gap ps_main_wrapper.c/ifconfig_main_wrapper.c/
 * uname_main_wrapper.c all fix (see any of those files' header comment):
 * real, unmodified Apple `ping` (third_party/network_cmds/ping.tproj/
 * ping.c) has zero knowledge of this project's TSD-bootstrap machinery
 * -- its own real main() never calls pthread_main_thread_bootstrap(),
 * so TPIDRRO_EL0 is still 0 when any TSD-backed Libc call runs.
 *
 * ping.c is compiled with -Dmain=ping_cmd_real_main (see build_ping.sh)
 * so THIS wrapper -- not ping.c's own real main() -- is the actual
 * process entry point Csu's crt.c calls.
 */
extern int pthread_main_thread_bootstrap(void);
extern int ping_cmd_real_main(int argc, char **argv);

static void die_bootstrap_failed(void) {
    register long x0 asm("x0") = 2;
    static const char msg[] = "ping: real pthread_main_thread_bootstrap() failed\n";
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
    return ping_cmd_real_main(argc, argv);
}
