/*
 * Static-link fallback for exit()'s __cxa_finalize(NULL) call (DAR-129).
 *
 * libc_stdio_process_shims.c's exit() references __cxa_finalize weakly, as
 * real Libc's exit() calls it unconditionally
 * (third_party/Libc/stdlib/FreeBSD/exit.c:66-69). The only real provider
 * here is libsystem_c.dylib (libsystem_c_cxa_atexit_compat.c). A weak_import
 * reference does NOT make a fully static link succeed: ld64 still reports
 * `Undefined symbols: ___cxa_finalize` (found linking the libc_printf/
 * libunwind/... smoketests after DAR-129 merged), and a `-U` workaround is
 * refused for the resulting bl ("branch out of range ... to 0x0").
 *
 * This weak DEFINITION lives in its own archive member so it is only pulled
 * when nothing else provides the symbol (static smoketests, which have no
 * atexit registry, so a no-op is exact). A strong definition in the same
 * link (libsystem_c.dylib's own build) wins over it.
 */
__attribute__((weak)) void __cxa_finalize(void *dso) {
    (void)dso;
}
