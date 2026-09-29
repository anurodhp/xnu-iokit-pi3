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
extern int ps_cmd_real_main(int argc, char **argv);

/* DAR-426: no pthread bootstrap here. dyld + libSystem's initializer
 * (Apple's real __pthread_init) already set up the main thread. */
int main(int argc, char **argv) {
    return ps_cmd_real_main(argc, argv);
}
