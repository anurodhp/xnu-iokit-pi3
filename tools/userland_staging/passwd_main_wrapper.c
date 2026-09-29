/* Same real gap/fix shape as getty_main_wrapper.c/login_main_wrapper.c/
 * vipw_main_wrapper.c (see getty_main_wrapper.c's own header for the
 * full original crash writeup: real Apple system_cmds source has zero
 * knowledge of this project's TSD-bootstrap machinery, so TPIDRRO_EL0
 * is still 0 the moment any pthread/xlocale-touching real libc call
 * runs). Real, unmodified system_cmds/passwd.tproj/passwd.c is compiled
 * with -Dmain=passwd_cmd_real_main (see build_passwd.sh) so THIS
 * wrapper -- not passwd.c's own real main() -- is the actual process
 * entry point Csu's crt.c calls.
 */
extern int passwd_cmd_real_main(int argc, char **argv);

/* DAR-426: no pthread bootstrap here. dyld + libSystem's initializer
 * (Apple's real __pthread_init) already set up the main thread. */
int main(int argc, char **argv) {
    return passwd_cmd_real_main(argc, argv);
}
