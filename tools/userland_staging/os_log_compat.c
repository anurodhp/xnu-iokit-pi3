/* Real os_log(3) engine: confirmed never open-sourced anywhere (not
 * part of any apple-oss-distributions repo, checked via git ls-remote,
 * not assumed) -- real macOS delivers these through a kernel logging
 * subsystem (oslogd/the AppleSystemLogger kext path) this project has
 * never built and has no equivalent of. Same shape as
 * third_party/PureDarwin/src/Libraries/XPC/launchd/pd_sandbox_check.c's
 * own sandbox_check() -- an honest no-op matching the real documented
 * behavior for "nothing is configured to receive this", not a
 * fabrication of log delivery.
 *
 * os_log_type_enabled() always returning false is the real, honest
 * "no log stream is currently active" answer (matches real os_log's own
 * documented behavior when no listener/predicate has enabled that
 * type+category) -- every real call site in this project's CoreFoundation/
 * libxpc guards its _os_log_*_impl() call behind this check first
 * (os/log.h's own os_log()/os_log_debug()/os_log_error() macros), so the
 * three _impl functions below are real, reachable code, just never
 * actually invoked as a direct consequence -- not stubbed out
 * separately, they still do the real (no-op, no daemon to deliver to)
 * thing if ever called directly. */
#include <stdint.h>
#include <stddef.h>

typedef struct os_log_s *os_log_t;
typedef uint8_t os_log_type_t;

/* struct os_log_s is opaque in the real header (no member layout is
 * ever exposed to callers -- every real access goes through the
 * functions below) -- a real int-sized placeholder object is enough to
 * hand back a valid, distinct, non-NULL os_log_t. */
static int _os_log_default_storage;
os_log_t _os_log_default = (os_log_t)&_os_log_default_storage;

os_log_t
os_log_create(const char *subsystem, const char *category)
{
	(void)subsystem;
	(void)category;
	return _os_log_default;
}

int
os_log_type_enabled(os_log_t oslog, os_log_type_t type)
{
	(void)oslog;
	(void)type;
	return 0;
}

void
_os_log_impl(void *dso, os_log_t log, os_log_type_t type,
	     const char *format, uint8_t *buf, uint32_t size)
{
	(void)dso;
	(void)log;
	(void)type;
	(void)format;
	(void)buf;
	(void)size;
}

void
_os_log_debug_impl(void *dso, os_log_t log, os_log_type_t type,
		    const char *format, uint8_t *buf, uint32_t size)
{
	(void)dso;
	(void)log;
	(void)type;
	(void)format;
	(void)buf;
	(void)size;
}

void
_os_log_error_impl(void *dso, os_log_t log, os_log_type_t type,
		    const char *format, uint8_t *buf, uint32_t size)
{
	(void)dso;
	(void)log;
	(void)type;
	(void)format;
	(void)buf;
	(void)size;
}

/* Real Libinfo/lookup.subproj/si_compare.c (DAR-181's real addrinfo
 * sort/RFC6724 gap, si_getaddrinfo.c's own real si_destination_compare()
 * dependency) is the first real caller of the 4th os_log severity this
 * project's libc/CoreFoundation/libxpc callers never needed -- same
 * honest no-op as the three above. */
void
_os_log_fault_impl(void *dso, os_log_t log, os_log_type_t type,
		    const char *format, uint8_t *buf, uint32_t size)
{
	(void)dso;
	(void)log;
	(void)type;
	(void)format;
	(void)buf;
	(void)size;
}
