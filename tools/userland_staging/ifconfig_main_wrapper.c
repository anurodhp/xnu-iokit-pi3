/* Same real TSD-bootstrap gap ps_main_wrapper.c/uname_main_wrapper.c both
 * fix (see either file's header comment) -- real, unmodified Apple
 * `ifconfig` (third_party/network_cmds/ifconfig.tproj/ifconfig.c) has no
 * knowledge of this project's TSD-bootstrap machinery, so its own real
 * main() never calls pthread_main_thread_bootstrap() and TPIDRRO_EL0 is
 * still 0 when any TSD-backed Libc call runs.
 *
 * ifconfig.c is compiled with -Dmain=ifconfig_cmd_real_main (see
 * build_ifconfig.sh) so THIS wrapper is the actual process entry point
 * Csu's crt.c calls.
 */
extern int ifconfig_cmd_real_main(int argc, char **argv);

/* DAR-426: no pthread bootstrap here. dyld + libSystem's initializer
 * (Apple's real __pthread_init) already set up the main thread. */
int main(int argc, char **argv) {
    return ifconfig_cmd_real_main(argc, argv);
}
