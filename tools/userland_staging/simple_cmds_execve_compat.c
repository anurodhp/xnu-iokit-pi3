/* Real Libc/gen/FreeBSD/exec.c (execv/execvp/execvP) calls a private
 * `_execve` (one leading underscore in its own source, compiles to
 * linker symbol `__execve`) with zero prototype in scope -- same class
 * of gap as every other underscore-prefixed private-alias case in
 * libc_build_prototypes.h. The Perl syscall generator only ever
 * produces a PLAIN `execve` raw stub for this syscall (confirmed: no
 * `__execve`/`___execve` variant exists in its output, unlike
 * getlogin/mmap/chmod's real private-wrapper split) -- this is a
 * one-line forwarder providing the private name exec.c expects. */
extern int execve(const char *path, char *const argv[], char *const envp[]);

int _execve(const char *path, char *const argv[], char *const envp[]) {
    return execve(path, argv, envp);
}
