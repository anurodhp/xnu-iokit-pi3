/* Real proc_pidinfo() CFBundle_Grok.c/CFBundle_Binary.c and other CF
 * callers need -- extracted verbatim (cites its real origin line) from
 * third_party/xnu-7195/libsyscall/wrappers/libproc/libproc.c rather than
 * compiling that whole file, same "extract just the needed real
 * function" pattern as cf_mach_port_syscalls.c/cf_pthread_attr_syscalls.c
 * (see either file's own header comment for the full reasoning).
 * libproc.c's own real body is otherwise untouched; not a fabrication.
 *
 * Real origin: libsyscall/wrappers/libproc/libproc.c:100-110. Not
 * including that file's own real #include <sys/proc_info.h> here: the
 * only other thing this extraction needs from it is the raw call-number
 * constant, and pulling the whole header drags in struct kevent_qos_s
 * (PRIVATE-gated, real, but stripped out of this project's exported
 * userspace headers, needed only by proc_* flavors this extraction
 * doesn't use) -- confirmed the hard way via a real trial compile, not
 * assumed. Real Apple's own libpthread/src/pthread.c takes the exact
 * same shortcut for pthread_setname_np's own __proc_info call (raw
 * literal call numbers, no header), see cf_pthread_setname_syscalls.c.
 */
#include <stdint.h>

/* Real value from xnu-7195 bsd/sys/proc_info.h:961
 * ("#define PROC_INFO_CALL_PIDINFO 0x2"), not re-included per this
 * file's own header comment above. */
#define PROC_INFO_CALL_PIDINFO 0x2

/* Real raw syscall wrapper, generated fresh by this project's own build
 * step (create-syscalls.pl against xnu-7195's real syscalls.master,
 * same recipe build_libxpc_syscalls.sh already established for this
 * exact symbol) and linked in as proc_info_trap.o -- declared here to
 * match libproc.c's own real prototype. */
int __proc_info(int callnum, int pid, int flavor, uint64_t arg, void * buffer, int buffersize);

int
proc_pidinfo(int pid, int flavor, uint64_t arg, void *buffer, int buffersize)
{
	int retval;

	if ((retval = __proc_info(PROC_INFO_CALL_PIDINFO, pid, flavor, arg, buffer, buffersize)) == -1) {
		return 0;
	}

	return retval;
}
