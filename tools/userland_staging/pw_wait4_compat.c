/* DAR-292: popen.c (real Libc/gen/FreeBSD/popen.c, compiled into pw)
 * calls the namespace-protected `_wait4` (linker symbol `__wait4`), which
 * no dylib exports. build_pw.sh used to satisfy it with
 * `-Wl,-alias,_wait4,__wait4` against a LOCAL _wait4 (wait4_compat.o from
 * liblaunchd_syscalls.a). _wait4 is now canonically owned by
 * libsystem_c.dylib (not local), and an alias whose base is not locally
 * compiled in the same link is the known ld64 silent-bad-bind hazard, so
 * define the private name here as a forwarder to the dylib's public
 * wait4(). */
extern int wait4(int pid, int *status, int options, void *rusage);

int _wait4(int pid, int *status, int options, void *rusage) {
    return wait4(pid, status, options, rusage);
}
