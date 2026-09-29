/* Same real gap the sibling bash_main_wrapper.c fix found (see that
 * file's header): real, unmodified Apple `uname` source (third_party/
 * shell_cmds/uname/uname.c) has zero knowledge of this project's
 * TSD-bootstrap machinery -- its own real main() never calls it, so
 * TPIDRRO_EL0 is still 0 when setlocale()/get_compat.c's compat_mode()
 * (itself gated behind a real pthread_once()) run, an immediate,
 * zero-output SIGSEGV, confirmed via a real QEMU boot-test before this
 * wrapper was added (docs/roadmap.md Phase 4, uname tier).
 *
 * Same fix shape: uname.c is compiled with -Dmain=uname_cmd_real_main
 * (see build_uname.sh) so THIS wrapper -- not uname.c's own real main()
 * -- is the actual process entry point Csu's crt.c calls. Calling
 * pthread_main_thread_bootstrap() directly (not the older
 * pthread_tsd_shim_bootstrap_for_libc() some earlier smoketests use)
 * also forces libpthread.a's real pthread_main_thread_bootstrap.o to
 * load, which is what makes this binary link the REAL, correct
 * _os_once-based pthread_once() instead of risking picking up
 * pthread_tsd_shim.c's older, naive one via archive-scan order (see
 * build_bash.sh's own "Real archive-member removal" investigation for
 * the full mechanism -- the same hazard applies to any binary linking
 * libc_locale.a alongside libpthread.a without its own direct reference
 * forcing the real implementation to load first).
 */
extern int uname_cmd_real_main(int argc, char **argv);

/* DAR-426: no pthread bootstrap here. dyld + libSystem's initializer
 * (Apple's real __pthread_init) already set up the main thread. */
int main(int argc, char **argv) {
    return uname_cmd_real_main(argc, argv);
}
