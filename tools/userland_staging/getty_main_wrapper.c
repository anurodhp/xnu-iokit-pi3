/* Same real gap uname_main_wrapper.c's own header documents in detail
 * (see that file): real, unmodified Apple `getty` source (third_party/
 * system_cmds/getty.tproj/main.c) has zero knowledge of this project's
 * TSD-bootstrap machinery -- its own real main() never calls it, so
 * TPIDRRO_EL0 is still 0 when gettable()'s cgetent() -> getent() calls
 * __current_locale() (xlocale_private.h), an immediate, zero-output
 * SIGSEGV -- confirmed via a real QEMU boot-test + bisection (temporary
 * write()-marker instrumentation of a scratch, git-reverted copy of
 * main.c/subr.c/getcap.c, never committed) that narrowed the crash to
 * exactly the cgetent() call inside gettable("default"), before this
 * wrapper was added.
 *
 * Same fix shape: getty.tproj's four real sources are compiled with
 * -Dmain=getty_cmd_real_main (see build_getty.sh) so THIS wrapper -- not
 * main.c's own real main() -- is the actual process entry point Csu's
 * crt.c calls.
 */
extern int getty_cmd_real_main(int argc, char **argv);

/* DAR-426: no pthread bootstrap here. dyld + libSystem's initializer
 * (Apple's real __pthread_init) already set up the main thread. */
int main(int argc, char **argv) {
    return getty_cmd_real_main(argc, argv);
}
