/* Real bodies for the os/log_private.h compat shim's os_log_pack_*()
 * declarations (see build_bootp_dhcp_engine.sh's generated
 * libc_build/compat_include/os/log_private.h for the full sourced
 * reasoning on why these exist and why their exact ABI doesn't matter).
 *
 * These are NOT reimplementations of Apple's real, never-open-sourced,
 * ABI-undocumented firehose pack-buffer wire format. They compose the
 * message text and nothing else -- which is the only part of a pack any
 * real call site this project pins ever reads back.
 *
 * UPDATED 2026-09-07 (DAR-263/DAR-265). These used to be pure no-ops:
 * os_log_pack_fill() discarded the format string and memset the pack to
 * zero, and os_log_pack_compose() returned an empty string. The stated
 * premise for that was "os_log_type_enabled() always returns 0, so none
 * of these are reachable at runtime" -- which is WRONG for configd, and
 * cost two real boots to discover.
 *
 * __SC_log_enabled() (SCD.c:544) only consults os_log_type_enabled()
 * FIRST; it then falls through to `if (_sc_log != kSCLogDestinationDefault)`
 * and returns TRUE for every level < LOG_INFO. configd sets exactly that
 * with its own real `-d` flag (configd.m:417, `_sc_log =
 * kSCLogDestinationFile`), and __SC_log_send()'s print path then does
 * `composed = os_log_pack_compose(...)` followed by
 * `fprintf(stdout, "%s\n", composed)`. So every SC_log() in configd DID
 * reach here and DID print -- as a blank line. `tail -40
 * /var/log/configd.log` on a real boot returned ~30 empty lines, which
 * read exactly like "configd logs nothing" rather than "configd's log
 * messages are being thrown away here".
 *
 * That made configd undiagnosable: DAR-261 and DAR-265 both had to be
 * root-caused from ps/exit codes and disassembly alone.
 *
 * `%@` handling: real SC_log() format strings use `%@` for CFTypeRef
 * arguments, which vsnprintf(3) does not implement -- passing a
 * CFStringRef to an unknown conversion is undefined. Each `%@` is
 * rewritten to `%p` before formatting, which consumes exactly the same
 * one pointer argument and prints its address. An honest, argument-safe
 * degradation: the message text and every non-%@ argument come out
 * real, and a CFTypeRef shows up as a pointer rather than as its
 * description. Formatting a real CFString here would mean calling
 * CFStringGetCString from inside the logging path of the process that
 * owns CoreFoundation's own locks -- a genuinely bad trade.
 */
#include "libc_build/compat_include/os/log_private.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

size_t
os_log_pack_size(const char *format, ...)
{
	(void)format;
	return sizeof(struct os_log_pack_s);
}

/* Rewrite every `%@` to `%p` (see this file's header for why). Both
 * consume exactly one pointer argument, so the varargs walk stays in
 * step; only the rendering of that one argument changes. `%%` is
 * skipped so an escaped percent can't be mistaken for a conversion. */
static void
sc_log_pack_sanitize_format(const char *in, char *out, size_t out_size)
{
	size_t i = 0;

	if (out_size == 0) {
		return;
	}
	while (in != NULL && *in != '\0' && i + 1 < out_size) {
		if (in[0] == '%' && in[1] == '%') {
			out[i++] = *in++;
			if (i + 1 < out_size) {
				out[i++] = *in++;
			}
			continue;
		}
		if (in[0] == '%' && in[1] == '@') {
			out[i++] = '%';
			if (i + 1 < out_size) {
				out[i++] = 'p';
			}
			in += 2;
			continue;
		}
		out[i++] = *in++;
	}
	out[i] = '\0';
}

void
os_log_pack_fill(os_log_pack_t pack, size_t size, int saved_errno,
                  const char *format, ...)
{
	char fmt[256];
	va_list ap;

	(void)size;
	(void)saved_errno;
	if (pack == NULL) {
		return;
	}
	memset(pack, 0, sizeof(*pack));
	if (format == NULL) {
		return;
	}
	sc_log_pack_sanitize_format(format, fmt, sizeof(fmt));
	va_start(ap, format);
	(void)vsnprintf((char *)pack->olp_reserved, sizeof(pack->olp_reserved),
	                fmt, ap);
	va_end(ap);
	pack->olp_reserved[sizeof(pack->olp_reserved) - 1] = '\0';
}

void
os_log_pack_send(os_log_pack_t pack, os_log_t log, os_log_type_t type)
{
	(void)pack;
	(void)log;
	(void)type;
}

/* Shared by both compose entry points: hand back whatever
 * os_log_pack_fill() composed. Real Apple's send_and_compose() also
 * emits to the firehose; there is no firehose here (os_log_compat.c's
 * _os_log_impl is an honest no-op), so the two differ only in name. */
static char *
sc_log_pack_copy_out(os_log_pack_t pack, char *buf, size_t buf_size)
{
	if (buf == NULL || buf_size == 0) {
		return buf;
	}
	if (pack == NULL) {
		buf[0] = '\0';
		return buf;
	}
	strlcpy(buf, (const char *)pack->olp_reserved, buf_size);
	return buf;
}

char *
os_log_pack_send_and_compose(os_log_pack_t pack, os_log_t log,
                              os_log_type_t type, char *buf, size_t buf_size)
{
	(void)log;
	(void)type;
	return sc_log_pack_copy_out(pack, buf, buf_size);
}

char *
os_log_pack_compose(os_log_pack_t pack, os_log_t log, os_log_type_t type,
                     char *buf, size_t buf_size)
{
	(void)log;
	(void)type;
	return sc_log_pack_copy_out(pack, buf, buf_size);
}
