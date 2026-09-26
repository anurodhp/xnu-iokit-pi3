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
 * hidden symbol. */
struct ProgramVars;
extern void _program_vars_init(const struct ProgramVars *vars);

void
__libc_program_vars_init(const struct ProgramVars *vars)
{
	_program_vars_init(vars);
}
