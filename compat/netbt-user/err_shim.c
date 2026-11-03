/*
 * err(3)/warn(3) for the netbt userland. This port's libsystem_c.dylib
 * exports the variadic err/warn only as FreeBSD's internal __err/__warn,
 * but does export verr/vwarn (CLAUDE.md, "the __err/__warn -> err/warn
 * aliases"); a local definition over those avoids a linker alias.
 */
#include <stdarg.h>
#include <err.h>

void
err(int eval, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	verr(eval, fmt, ap);
}

void
warn(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vwarn(fmt, ap);
	va_end(ap);
}
