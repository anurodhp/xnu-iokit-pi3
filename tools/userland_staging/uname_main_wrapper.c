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
extern int pthread_main_thread_bootstrap(void);
extern int uname_cmd_real_main(int argc, char **argv);

static void die_bootstrap_failed(void) {
    register long x0 asm("x0") = 2;
    static const char msg[] = "uname: real pthread_main_thread_bootstrap() failed\n";
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
    return uname_cmd_real_main(argc, argv);
}
