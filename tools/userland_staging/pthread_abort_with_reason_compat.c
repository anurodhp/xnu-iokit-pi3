/* Real Apple abort_with_reason() (third_party/xnu-7195/libsyscall/wrappers/
 * terminate_with_reason.c) sends a real SIGABRT via __abort_with_payload,
 * falling back to a real SIGKILL-equivalent terminate_with_payload -
 * both real Mach/BSD reason-payload syscalls, and needs real
 * sigemptyset/sigaddset/sigprocmask/getpid first to unmask SIGABRT. This
 * project has no real signal-delivery subsystem built yet (Phase 4 has
 * not reached signals - separate, larger scope than this pthread tier).
 *
 * This function's ONLY real caller in this project's compiled libpthread
 * tier is inline_internal.h's _pthread_validate_signature() - reached
 * only when a pthread_t's signature fails to verify, i.e. real memory
 * corruption was just detected. Real Apple's behavior there is
 * "terminate the process, do not continue running with a corrupted
 * pthread_t" - the exact effect (not the exact mechanism) is what
 * matters for correctness, and this project has no signal handlers
 * installed anywhere that would need SIGABRT specifically vs. any other
 * hard stop. Real, honest implementation of that effect: exit(1) via
 * this project's own already-real raw exit syscall, not a fabricated
 * "success" return - the process genuinely terminates. Last resort per
 * CLAUDE.md's source-priority rule, after confirming the real Apple
 * path's actual dependency (signal delivery) is out of scope for this
 * pthread tier specifically, not skipped preemptively.
 */

typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

void abort_with_reason(uint32_t reason_namespace, uint64_t reason_code,
		const char *reason_string, uint64_t reason_flags) {
	(void)reason_namespace;
	(void)reason_code;
	(void)reason_string;
	(void)reason_flags;
	register long x0 asm("x0") = 1;
	register long x16 asm("x16") = 1; /* exit */
	asm volatile("svc #0x80" : "+r"(x0) : "r"(x16) : "memory", "cc");
	__builtin_unreachable();
}
