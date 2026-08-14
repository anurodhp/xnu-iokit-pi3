/* Separate translation unit, deliberately: see pthread_tsd_shim.c's own
 * comment for why __xlocale_init's dependency lives here and not there
 * (keeps the base TSD shim usable with zero Libc-locale dependency).
 *
 * pthread_tsd_shim_bootstrap_for_libc() is the one function real
 * programs linking against BOTH pthread_tsd_shim.c AND any of the
 * locale-tier Libc files should call - exactly once, as the first thing
 * main() does. Does pthread_tsd_shim.c's platform_syscall bootstrap
 * first, THEN __xlocale_init() (which needs TSD already live, so the
 * order matters - real, small, safe to call directly: extern, declared
 * in third_party/Libc/locale/xlocale_private.h, defined in xlocale.c,
 * just claims a fixed reserved TSD key via pthread_key_init_np()).
 *
 * REAL BUG THIS FIXES (found by adversarial review): before this,
 * nothing in this project ever called __xlocale_init() at all -
 * third_party/Libc/locale/xlocale.c's __locale_key starts at
 * (pthread_key_t)-1 and is ONLY ever assigned by __xlocale_init(), whose
 * only real caller in the apple-oss-distributions tree is
 * sys/_libc_init.c (never compiled here). Without it,
 * __current_locale() indexed TSD slot -1 (8 bytes *before* the TSD
 * block's start) - happened to read as NULL only because that memory
 * was zero padding, not because anything was actually initialized.
 */

extern int pthread_tsd_shim_bootstrap(void);
extern void __xlocale_init(void);

int pthread_tsd_shim_bootstrap_for_libc(void) {
    int ok = pthread_tsd_shim_bootstrap();
    if (ok) {
        __xlocale_init();
    }
    return ok;
}
