/* Real, curated __chk_fail_overflow()/__chk_fail_overlap()/__chk_overlap()/
 * __chk_assert_no_overlap -- verbatim algorithm from
 * third_party/Libc/secure/chk_fail.c (real Apple source, needed by the 5
 * real _FORTIFY_SOURCE _chk functions build_libxpc_syscalls2.sh compiles
 * as-is: memmove_chk.c/strcpy_chk.c/strlcpy_chk.c/strncpy_chk.c/
 * vsnprintf_chk.c -- all `#include "secure.h"`, which declares exactly
 * these 4 symbols).
 *
 * Curated rather than compiling that file directly: its real
 * PR_13085474_CHECK block (a pre-10.9-linked-app compatibility shim that
 * disables overlap checking for old binaries, gated on
 * `TARGET_OS_OSX && dyld_sdk_at_least(...)`) already compiles out to
 * nothing on this project's iOS-target build (TARGET_OS_OSX is 0), so the
 * only real behavioral difference here is __chk_fail_overflow()/
 * __chk_fail_overlap() calling plain abort() instead of the real
 * os_crash()/_os_crash() machinery (libplatform's os_log-backed crash
 * reporter, which needs a real crash-reporter/exception-port pipeline
 * this target has none of) -- both still, correctly, abort the process on
 * a real detected overflow/overlap, just without a formatted message.
 */
#include <stdint.h>
#include <stdlib.h>

__attribute__((visibility("hidden")))
uint32_t __chk_assert_no_overlap = 1;

__attribute__((visibility("hidden")))
__attribute__((noreturn))
void
__chk_fail_overflow(void)
{
	abort();
}

__attribute__((visibility("hidden")))
__attribute__((noreturn))
void
__chk_fail_overlap(void)
{
	abort();
}

__attribute__((visibility("hidden")))
void
__chk_overlap(const void *_a, size_t an, const void *_b, size_t bn)
{
	uintptr_t a = (uintptr_t)_a;
	uintptr_t b = (uintptr_t)_b;

	if (__builtin_expect(an == 0 || bn == 0, 0)) {
		return;
	} else if (__builtin_expect(a == b, 0)) {
		__chk_fail_overlap();
	} else if (a < b) {
		if (__builtin_expect(a + an > b, 0)) {
			__chk_fail_overlap();
		}
	} else {
		if (__builtin_expect(b + bn > a, 0)) {
			__chk_fail_overlap();
		}
	}
}
