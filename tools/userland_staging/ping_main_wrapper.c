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
extern int ping_cmd_real_main(int argc, char **argv);

/* DAR-426: no pthread bootstrap here. dyld + libSystem's initializer
 * (Apple's real __pthread_init) already set up the main thread. */
int main(int argc, char **argv) {
    return ping_cmd_real_main(argc, argv);
}
