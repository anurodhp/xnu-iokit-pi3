/* Real body for os_log_with_args(), the one function SCD.c's real
 * __SCLog() (reached only via SC_log()/SCPrivate.h's SC_log macro) calls
 * beyond the os_log_pack_*() family sc_log_pack_compat.c already covers
 * (see that file's own header comment for the full sourced reasoning --
 * this is the same real gap class and same real unreachability argument).
 *
 * os_log_with_args() itself is real Apple SPI (declared in the real,
 * never-open-sourced os/log_private.h, confirmed absent from every
 * pinned apple-oss-distributions repo and from this project's pinned
 * SDK's public os/log.h) -- but every real, unmodified call site this
 * project has pinned (SCD.c's __SC_log_send(), SCD.c:546) already checks
 * os_log_type_enabled(log, type) first, and this project's real
 * os_log_type_enabled() (os_log_compat.c) always returns 0, so
 * __SCLog()/os_log_with_args() is unreachable at runtime today. Honest
 * no-op body, not a reimplementation of Apple's real firehose formatting
 * pipeline.
 */
#include <os/log_private.h>
#include <stdarg.h>

void
os_log_with_args(os_log_t log, os_log_type_t type, const char *format,
                  va_list args, void *addr)
{
	(void)log;
	(void)type;
	(void)format;
	(void)args;
	(void)addr;
}
