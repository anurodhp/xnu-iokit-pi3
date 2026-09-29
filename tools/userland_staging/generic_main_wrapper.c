/* Generic real-command entry-point wrapper -- same real gap
 * bash_main_wrapper.c/uname_main_wrapper.c/ps_main_wrapper.c each fix
 * individually (see any of those files' headers for the full mechanism):
 * real, unmodified Apple command sources have zero knowledge of this
 * project's TSD-bootstrap machinery, so a real process entry point that
 * calls pthread_main_thread_bootstrap() before any TSD-backed Libc call
 * runs is mandatory, not optional -- skipping it is a real, reproducible
 * SIGSEGV (confirmed three separate times now, once per binary above,
 * before each got its own wrapper).
 *
 * This file is the SAME wrapper body reused for every simple, single-
 * main real command from here on (see build_simple_cmds.sh) instead of
 * hand-duplicating a near-identical 30-line file per binary -- the real
 * command's own main() is renamed via -Dmain=<real_main_name> at compile
 * time (matches the exact pattern bash/uname/ps already use), and this
 * file is compiled once per binary with -DREAL_MAIN=<real_main_name> to
 * match, via -include (see build_simple_cmds.sh's CFLAGS). CMD_NAME is
 * used only for the bootstrap-failure error message.
 */
#ifndef REAL_MAIN
#error "REAL_MAIN must be defined (matches -Dmain=... on the real command source)"
#endif
#ifndef CMD_NAME
#define CMD_NAME "cmd"
#endif

extern int REAL_MAIN(int argc, char **argv);

/* DAR-426: no pthread bootstrap here. dyld + libSystem's initializer
 * (Apple's real __pthread_init) already set up the main thread. */
int main(int argc, char **argv) {
    return REAL_MAIN(argc, argv);
}
