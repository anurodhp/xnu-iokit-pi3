/* Same real gap/fix shape as pw_main_wrapper.c/chpass_main_wrapper.c/
 * vipw_main_wrapper.c (see vipw_main_wrapper.c's own header for the
 * full original crash writeup). Real, unmodified shell_cmds/su/su.c is
 * compiled with -Dmain=su_cmd_real_main (see build_su.sh) so THIS
 * wrapper -- not su.c's own real main() -- is the actual process entry
 * point Csu's crt.c calls.
 */
extern int su_cmd_real_main(int argc, char **argv);

/* DAR-426: no pthread bootstrap here. dyld + libSystem's initializer
 * (Apple's real __pthread_init) already set up the main thread. */
int main(int argc, char **argv) {
    return su_cmd_real_main(argc, argv);
}
