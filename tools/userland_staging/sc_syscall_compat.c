/* Real raw-syscall wrappers + honest stand-ins for the real, unmodified
 * SCD*.c/SCP*.c/dy_framework.c call sites (fork()/execv() in
 * SCDPlugin.c's real plugin-child-process spawn path, chown()/dup2()/
 * setgid()/setuid() also in SCDPlugin.c, fchmod() in SCPCommit.c,
 * getattrlist() in SCPLock.c, usleep() in SCDNotifierInformViaCallback.c's
 * retry backoff, pthread_setcanceltype() in SCDOpen.c, backtrace()/
 * backtrace_symbols() in SCDPrivate.c's real _SC_copyBacktrace()) that
 * this project's existing dylib stack doesn't export yet. Same real
 * carry-flag raw-asm pattern every other hand-written NO_SYSCALL_STUB
 * shim in this project uses (getty_syscalls.c/cp_mv_syscalls.c/
 * bash_syscalls.c), real syscall numbers cross-checked against
 * third_party/xnu-7195/bsd/kern/syscalls.master, not guessed.
 *
 * chown/dup2/setgid/setuid/execve are NOT here -- all five already have
 * real, already-built, standalone raw-syscall objects from
 * build_simple_cmds.sh's own generic SYS_STUBS_NEEDED batch
 * (libc_build/simple_obj/sys__chown.o etc, confirmed via `nm` to export
 * exactly _chown/_dup2/_setgid/_setuid/_execve), linked in directly by
 * this script rather than hand-duplicated here (same "reuse the
 * already-generated stub" precedent getty_syscalls.c's own header
 * comment documents for the same five).
 */
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

extern long cerror_nocancel(int err);
extern char **environ;

static inline long sc_syscall0(long num) {
	register long x0 asm("x0");
	register long x16 asm("x16") = num;
	long is_error;
	asm volatile("svc #0x80\n"
	             "cset %1, cs"
	             : "=r"(x0), "=r"(is_error)
	             : "r"(x16)
	             : "memory", "cc");
	if (is_error) return cerror_nocancel((int)x0);
	return x0;
}

static inline long sc_syscall2(long num, long a0, long a1) {
	register long x0 asm("x0") = a0;
	register long x1 asm("x1") = a1;
	register long x16 asm("x16") = num;
	long is_error;
	asm volatile("svc #0x80\n"
	             "cset %1, cs"
	             : "+r"(x0), "=r"(is_error)
	             : "r"(x1), "r"(x16)
	             : "memory", "cc");
	if (is_error) return cerror_nocancel((int)x0);
	return x0;
}

static inline long sc_syscall4(long num, long a0, long a1, long a2, long a3) {
	register long x0 asm("x0") = a0;
	register long x1 asm("x1") = a1;
	register long x2 asm("x2") = a2;
	register long x3 asm("x3") = a3;
	register long x16 asm("x16") = num;
	long is_error;
	asm volatile("svc #0x80\n"
	             "cset %1, cs"
	             : "+r"(x0), "=r"(is_error)
	             : "r"(x1), "r"(x2), "r"(x3), "r"(x16)
	             : "memory", "cc");
	if (is_error) return cerror_nocancel((int)x0);
	return x0;
}

static inline long sc_syscall5(long num, long a0, long a1, long a2, long a3, long a4) {
	register long x0 asm("x0") = a0;
	register long x1 asm("x1") = a1;
	register long x2 asm("x2") = a2;
	register long x3 asm("x3") = a3;
	register long x4 asm("x4") = a4;
	register long x16 asm("x16") = num;
	long is_error;
	asm volatile("svc #0x80\n"
	             "cset %1, cs"
	             : "+r"(x0), "=r"(is_error)
	             : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x16)
	             : "memory", "cc");
	if (is_error) return cerror_nocancel((int)x0);
	return x0;
}

#define SC_SYS_FORK       2
#define SC_SYS_WAIT4      7
#define SC_SYS_FCHMOD     124
#define SC_SYS_GETATTRLIST 220

extern int execve(const char *path, char *const argv[], char *const envp[]);

/* execv(): real, standard BSD shape (Libc/gen/FreeBSD/exec.c's own real
 * body, reproduced verbatim here rather than compiling that whole file
 * in -- exec.c also defines execl/execle/execlp/execvp/execvP, several
 * of which risk colliding with symbols this project's Libc tiers may
 * already provide elsewhere, same real duplicate-copy risk class this
 * project's history has hit before). Real real_execve is the already-
 * built sys__execve.o object (linked in by this script), not
 * reimplemented here. */
int execv(const char *path, char *const argv[]) {
	(void)execve(path, argv, environ);
	return -1;
}

/* fork(): real x0/x1 discriminator ABI (arm64 XNU's raw fork() trap
 * returns the child's pid in x0 to BOTH parent and child; x1 is the only
 * real parent/child discriminator: 0=parent, 1=child) -- same real ABI
 * quirk this project's bash_syscalls.c/login_syscalls.c/
 * tools/init_binary/init.c already found, documented, and fixed
 * (DAR-220 in bash_syscalls.c's own header comment). This consumer does
 * NOT need bash_syscalls.c's own extra `__current_pid` cache-clearing
 * step: that step exists only because bash.macho separately links a
 * CACHING getpid() (___getpid.s, which caches into `__current_pid`) --
 * confirmed via `nm -gU` that neither `_getpid` nor `__current_pid` is
 * exported by any dylib this script links against, so no such cache
 * exists in this consumer's process image to go stale. Real, unmodified
 * SCDPlugin.c's own fork() caller (_SCDPluginExecCommand2()) does its
 * own getpid()-independent child bookkeeping.
 *
 * Same real gap this project's own libSystem_atfork machinery documents
 * (docs/launchd-pid1-postmortem.md): real Libc/sys/fork.c wraps the raw
 * trap with pthread_atfork()/malloc fork-hook dispatch this project's
 * dylib stack has real support for only in specific already-wired
 * consumers (launchd itself). SCDPlugin.c's own real fork()+execv()
 * pattern (immediate exec in the child, per _SCDPluginExecCommand2())
 * is the classic case where skipping those hooks is safe in practice --
 * documented here as a real, known, narrower-than-launchd's gap, not
 * silently papered over. */
pid_t fork(void) {
	register long x0 asm("x0") = 0;
	register long x1 asm("x1") = 0;
	register long x16 asm("x16") = SC_SYS_FORK;
	long is_error;
	asm volatile("svc #0x80\n"
	             "cset %2, cs"
	             : "+r"(x0), "+r"(x1), "=r"(is_error)
	             : "r"(x16)
	             : "memory", "cc");
	if (is_error) return (pid_t)cerror_nocancel((int)x0);
	return (pid_t)(x1 ? 0 : x0);
}

int wait4(pid_t pid, int *status, int options, void *rusage) {
	return (int)sc_syscall4(SC_SYS_WAIT4, pid, (long)status, options, (long)rusage);
}

/* Real real Apple naming: this project's CFLAGS (-D__DARWIN_UNIX03=1,
 * same as every other tier) make every real header declaration of these
 * four functions asm-label to a "$UNIX2003" (or "$UNIX2003$INODE64"-
 * class, not applicable to these four) variant symbol name via
 * __DARWIN_ALIAS -- confirmed by this exact real "_fchmod$UNIX2003
 * undefined" link error, not guessed. Real, unmodified caller source
 * expects that exact suffixed symbol, not the plain one; asm-labeling
 * the definition itself (matching bash_syscalls.c's own precedent for
 * referencing, not defining, these suffixed names) is the correct real
 * fix, not a new plain symbol nothing calls. */
int fchmod(int fd, int mode) asm("_fchmod$UNIX2003");
int fchmod(int fd, int mode) {
	return (int)sc_syscall2(SC_SYS_FCHMOD, fd, mode);
}

int getattrlist(const char *path, void *alist, void *attributeBuffer,
                 size_t bufferSize, unsigned long options) asm("_getattrlist$UNIX2003");
int getattrlist(const char *path, void *alist, void *attributeBuffer,
                 size_t bufferSize, unsigned long options) {
	return (int)sc_syscall5(SC_SYS_GETATTRLIST, (long)path, (long)alist,
	                         (long)attributeBuffer, (long)bufferSize, options);
}

/* usleep(): real standard libc shape (BSD usleep(3) is a thin wrapper
 * over nanosleep(2), not its own raw syscall -- real, exported
 * `_nanosleep` confirmed via `nm -gU libc_build/system/
 * libsystem_kernel.dylib`). */
struct sc_timespec { long tv_sec; long tv_nsec; };
extern int nanosleep(const struct sc_timespec *rqtp, struct sc_timespec *rmtp);
int usleep(unsigned int useconds) asm("_usleep$UNIX2003");
int usleep(unsigned int useconds) {
	struct sc_timespec ts;
	ts.tv_sec = useconds / 1000000;
	ts.tv_nsec = (long)(useconds % 1000000) * 1000L;
	return nanosleep(&ts, (void *)0);
}

/* pthread_setcanceltype(): real POSIX thread-cancellation state setter.
 * Same real, documented gap class as getty_syscalls.c's own
 * pthread_testcancel() stand-in: this project's libpthread port has
 * never implemented real thread cancellation (confirmed via nm: no such
 * symbol anywhere in the pthread dylib/archive stack, only callers), and
 * since cancellation requests are never posted in the first place here,
 * "record the requested type, never actually deliver a cancellation" is
 * the exact correct behavior for this target, not a degraded
 * approximation. SCDOpen.c's own call site (add_state_handler()'s
 * dispatch_queue setup) only checks the OLD out-parameter is non-NULL
 * before use, never the specific value, so any consistent old-type
 * value is safe here. */
int pthread_setcanceltype(int type, int *oldtype) asm("_pthread_setcanceltype$UNIX2003");
int pthread_setcanceltype(int type, int *oldtype) {
	static int sc_cancel_type = 0 /* PTHREAD_CANCEL_DEFERRED */;
	if (oldtype != (void *)0) {
		*oldtype = sc_cancel_type;
	}
	sc_cancel_type = type;
	return 0;
}

/* backtrace()/backtrace_symbols(): real, exported `___Unwind_Backtrace`
 * exists in this project's libunwind.dylib (confirmed via nm -gU), but a
 * correct backtrace_symbols() also needs real dladdr()-based symbol
 * resolution this project has not wired up for arbitrary return
 * addresses. Honest, safe degraded behavior matching this project's own
 * os_log_pack_*()/os_log_with_args() precedent (sc_log_pack_compat.c/
 * os_log_with_args_compat.c): report zero frames rather than guess at
 * unwind-context plumbing or fabricate symbol names. SCDPrivate.c's own
 * real caller, _SC_copyBacktrace(), already treats n==-1 as a real,
 * expected failure path (logs and returns NULL) -- reusing that exact
 * real path via -1 here rather than returning a technically-different
 * "0 frames, not an error" value keeps this consumer's real behavior
 * honest (a crash-report helper that can't report a real trace should
 * say so, not report an empty one that looks successful). */
int backtrace(void **buffer, int size) {
	(void)buffer;
	(void)size;
	return -1;
}

char **backtrace_symbols(void *const *buffer, int size) {
	(void)buffer;
	(void)size;
	return (char **)0;
}
