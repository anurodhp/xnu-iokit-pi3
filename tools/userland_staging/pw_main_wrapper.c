/* Same real gap/fix shape as chpass_main_wrapper.c/vipw_main_wrapper.c/
 * passwd_main_wrapper.c/pwd_mkdb_main_wrapper.c (see vipw_main_wrapper.c's
 * own header for the full original crash writeup). Real, unmodified
 * usr.sbin/pw/pw.c is compiled with -Dmain=pw_cmd_real_main (see
 * build_pw.sh) so THIS wrapper -- not pw.c's own real main() -- is the
 * actual process entry point Csu's crt.c calls.
 */
#include <string.h>

extern int pthread_main_thread_bootstrap(void);
extern int pw_cmd_real_main(int argc, char **argv);

static void die_bootstrap_failed(void) {
    register long x0 asm("x0") = 2;
    static const char msg[] = "pw: real pthread_main_thread_bootstrap() failed\n";
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
    /* Account creation always makes the home directory, matching macOS
     * (/Users/<name>, see /etc/pw.conf's "home"). Real pw(8) only does so
     * when given -m (pw_user.c pw_user_add(): `case 'm': createhome =
     * true;`, consumed at its create_and_populate_homedir() call), so add
     * it for every spelling pw.c's Combo1/Combo2/Modes+Which tables accept
     * (`useradd`, `adduser`, `user add`, `add user`, after any -V/-R dir).
     * Appended rather than inserted: pw.c's main() takes the name as the
     * first bare word after the subcommand and stops at the first dash
     * token, so -m must come after the name; pw_user_add()'s getopt then
     * sees it like any other trailing option (a repeated -m is harmless).
     * pw_user_add()'s own PWALTDIR()/dryrun checks still skip creation
     * for -R and -N. */
    int i = 1, add = 0;
    while (i < argc && argv[i][0] == '-' &&
           (argv[i][1] == 'V' || argv[i][1] == 'R'))
        i += argv[i][2] == '\0' ? 2 : 1;
    if (i < argc && (strcmp(argv[i], "useradd") == 0 ||
                     strcmp(argv[i], "adduser") == 0))
        add = 1;
    else if (i + 1 < argc &&
             ((strcmp(argv[i], "user") == 0 && strcmp(argv[i + 1], "add") == 0) ||
              (strcmp(argv[i], "add") == 0 && strcmp(argv[i + 1], "user") == 0)))
        add = 1;
    if (add) {
        static char dash_m[] = "-m";
        static char *nargv[256];
        if (argc + 1 < (int)(sizeof(nargv) / sizeof(nargv[0]))) {
            for (i = 0; i < argc; i++)
                nargv[i] = argv[i];
            nargv[argc] = dash_m;
            nargv[argc + 1] = 0;
            return pw_cmd_real_main(argc + 1, nargv);
        }
    }
    return pw_cmd_real_main(argc, argv);
}
