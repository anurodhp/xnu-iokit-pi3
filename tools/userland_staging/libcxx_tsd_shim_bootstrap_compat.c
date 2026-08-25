/* Real, scoped resolution of a genuine link-time collision: this
 * smoketest is the first consumer in this project to link BOTH real
 * libpthread.a (its own real pthread_tsd.c provides pthread_key_create/
 * pthread_setspecific/pthread_getspecific, pulled in by libc++abi's
 * cxa_exception_storage.o) AND libc_locale_for_stdio.a (needed for its
 * real mbrtowc_l/wcrtomb_l wide-char conversion, pulled in by
 * libc_printf.a's vfwprintf.o). libc_locale_for_stdio.a's own
 * pthread_tsd_shim.o defines a NOW-SUPERSEDED pthread_key_init_np (see
 * pthread_tsd_platform_bootstrap.c's header comment -- this exact class
 * of duplicate was already anticipated and already fixed once for
 * libmalloc.a via `ar d`) -- this smoketest's own build step (see
 * build_llvm_libcxx.sh) does the same `ar d` surgery on a LOCAL COPY of
 * libc_locale_for_stdio.a (never the shared, committed archive -- other
 * scripts still need that member's pthread_key_init_np until the
 * broader tier reconciliation flagged in that header comment happens).
 *
 * Removing that member also removes the ONE other real symbol it
 * provided that libmalloc.a's real _get_zone() still calls: the plain-
 * named pthread_tsd_shim_bootstrap() (legacy, pre-real-libpthread
 * entry point). This file supplies that plain name back as a real,
 * correct (not fabricated) no-op: this smoketest's own main() calls
 * pthread_main_thread_bootstrap() first, which already performs the
 * real TPIDRRO_EL0 install via pthread_tsd_shim_bootstrap_with_base()
 * before any pthread_key_create() ever runs -- and that primitive is
 * documented as idempotent/safe to call any number of times (same
 * header comment). So by the time _get_zone() calls this legacy name,
 * the real work it used to do has already genuinely happened; treating
 * the redundant second call as an already-succeeded no-op is the
 * correct answer for this call site, not a stand-in for missing
 * behavior.
 */
int pthread_tsd_shim_bootstrap(void) {
    return 1;
}
