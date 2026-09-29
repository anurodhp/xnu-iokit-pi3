/* Same real gap/fix shape as vipw_main_wrapper.c/passwd_main_wrapper.c
 * (see vipw_main_wrapper.c's own header for the full original crash
 * writeup: real Apple system_cmds source has zero knowledge of this
 * project's TSD-bootstrap machinery). Real, unmodified system_cmds/
 * pwd_mkdb.tproj/pwd_mkdb.c is compiled with
 * -Dmain=pwd_mkdb_cmd_real_main (see build_pwd_mkdb.sh) so THIS wrapper
 * -- not pwd_mkdb.c's own real main() -- is the actual process entry
 * point Csu's crt.c calls.
 */
extern int pwd_mkdb_cmd_real_main(int argc, char **argv);

/* DAR-426: no pthread bootstrap here. dyld + libSystem's initializer
 * (Apple's real __pthread_init) already set up the main thread. */
int main(int argc, char **argv) {
    return pwd_mkdb_cmd_real_main(argc, argv);
}
