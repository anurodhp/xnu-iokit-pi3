/* Exported entry to real Libc's _program_vars_init() (DAR-251).
 *
 * Real Apple fills libsystem_c's _NSGetArgv()/_NSGetArgc()/_NSGetProgname()/
 * _NSGetMachExecuteHeader() from dyld's ProgramVars during
 * libSystem_initializer: Libsystem/init.c:224 passes `vars` to
 * _libc_initializer(), which calls _program_vars_init(vars)
 * (Libc/sys/_libc_init.c:55). That function is visibility("hidden") in
 * Libc/sys/crt_externs.c:92 because its only caller lives in the same
 * dylib. This port has no _libc_initializer(), so libSystem.B.dylib's own
 * constructor (libsystem_dyld_initializer_compat.c) has to reach it across
 * the image boundary; this one-line forwarder is linked in the same
 * `ld -r` unit as crt_externs.o (build_libsystem_c.sh) so it can see the
 * hidden symbol.
 *
 * DAR-416: also runs __xlocale_init(), the other _libc_initializer() step
 * (Libc/sys/_libc_init.c:61) this port needs. It sets __locale_key to
 * __LIBC_PTHREAD_KEY_XLOCALE (TSD slot 10) and registers it
 * (Libc/locale/xlocale.c:491-498). xlocale.c:530's constructor was meant
 * to do that, but dyld never runs /usr/lib/system/lib* initializers, so
 * __locale_key stayed (pthread_key_t)-1 and __current_locale() read
 * tsd[-1]: zero in dyld's main-thread block (so the main thread got the
 * global locale by accident), but a live struct pthread_s field on every
 * real pthread_create()d thread -- printf() on a second thread faulted in
 * localeconv_l. Must run after __pthread_init() (pthread_key_init_np). */
struct ProgramVars;
extern void _program_vars_init(const struct ProgramVars *vars);
extern void __xlocale_init(void);

void
__libc_program_vars_init(const struct ProgramVars *vars)
{
	_program_vars_init(vars);
	__xlocale_init();
}
