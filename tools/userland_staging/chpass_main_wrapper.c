/* Same real gap/fix shape as vipw_main_wrapper.c/passwd_main_wrapper.c/
 * pwd_mkdb_main_wrapper.c (see vipw_main_wrapper.c's own header for the
 * full original crash writeup). Real, unmodified system_cmds/
 * chpass.tproj/chpass.c is compiled with -Dmain=chpass_cmd_real_main
 * (see build_chpass.sh) so THIS wrapper -- not chpass.c's own real
 * main() -- is the actual process entry point Csu's crt.c calls.
 */
extern int chpass_cmd_real_main(int argc, char **argv);

/* DAR-426: no pthread bootstrap here. dyld + libSystem's initializer
 * (Apple's real __pthread_init) already set up the main thread. */
int main(int argc, char **argv) {
    return chpass_cmd_real_main(argc, argv);
}
