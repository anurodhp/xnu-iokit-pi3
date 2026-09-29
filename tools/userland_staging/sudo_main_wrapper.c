/* Same shape as su_main_wrapper.c (see its header): real, unmodified
 * third_party/sudo/src/sudo.c is compiled with -Dmain=sudo_cmd_real_main
 * (build_sudo.sh) so THIS wrapper is the process entry point Csu's
 * crt.c calls. Unlike su/pw, sudo's real main() takes THREE arguments
 * (third_party/sudo/src/sudo.c:150 -- `main(int argc, char *argv[],
 * char *envp[])`), and sudo's environment sanitising reads envp, so it
 * must be passed through rather than dropped.
 *
 * DAR-426: no pthread bootstrap here. dyld + libSystem's initializer
 * already set up the main thread.
 */
extern int sudo_cmd_real_main(int argc, char **argv, char **envp);

int main(int argc, char **argv, char **envp) {
    return sudo_cmd_real_main(argc, argv, envp);
}
