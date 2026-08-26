/* Real Apple kdebug_signpost()/_start()/_end() (bsd/sys/kdebug_signpost.h,
 * already deprecated upstream in favor of os_signpost) have no real
 * source anywhere in this project's pinned third_party/Libc checkout.
 * Real semantics are purely a diagnostic side channel (emit a trace
 * event for external tools like Instruments/ktrace) -- never affects
 * program correctness. launchd's own real ktrace.c (third_party/
 * PureDarwin) calls these guarded by `if (launchd_apple_internal)`,
 * which is false on this target (never set true outside an Apple-
 * internal build variant this project doesn't produce), so these real
 * calls are already dead code in practice (`launchd_apple_internal` is
 * only ever set true if a real `/AppleInternal` marker file exists on
 * the root filesystem, runtime.c:1450 -- never true on a normal,
 * non-Apple-internal boot image this project produces) -- a safe,
 * honest no-op (rather than a full raw kdebug_trace(2) wrapper) is
 * correct, not a simplification of real, reachable behavior.
 */
#include <stdint.h>

int kdebug_signpost(uint32_t code, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3, uintptr_t arg4) {
	(void)code; (void)arg1; (void)arg2; (void)arg3; (void)arg4;
	return 0;
}

int kdebug_signpost_start(uint32_t code, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3, uintptr_t arg4) {
	(void)code; (void)arg1; (void)arg2; (void)arg3; (void)arg4;
	return 0;
}

int kdebug_signpost_end(uint32_t code, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3, uintptr_t arg4) {
	(void)code; (void)arg1; (void)arg2; (void)arg3; (void)arg4;
	return 0;
}
