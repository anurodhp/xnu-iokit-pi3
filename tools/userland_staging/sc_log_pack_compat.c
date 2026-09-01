/* Real bodies for the os/log_private.h compat shim's os_log_pack_*()
 * declarations (see build_bootp_dhcp_engine.sh's generated
 * libc_build/compat_include/os/log_private.h for the full sourced
 * reasoning on why these exist and why their exact ABI doesn't matter).
 *
 * Every real, unmodified Apple call site this project has pinned
 * (SystemConfiguration.fproj/SCD.c's __SC_log_send(), reached only via
 * SC_log()/SCPrivate.h's SC_log macro) already checks
 * os_log_type_enabled() first, and this project's real
 * os_log_type_enabled() (os_log_compat.c) always returns 0 -- so none
 * of these are reachable at runtime in this project today. Honest
 * no-op bodies, not a reimplementation of Apple's real, never-open-
 * sourced, ABI-undocumented firehose pack-buffer wire format.
 *
 * Not yet wired into any build script's FILES/link step -- link this in
 * alongside real configd/SystemConfiguration object files at the first
 * real smoketest link that pulls in SCD.c (Linear DAR-170 Tier 1: the
 * libSystemConfiguration_client.c / configd.tproj port).
 */
#include "libc_build/compat_include/os/log_private.h"
#include <string.h>

size_t
os_log_pack_size(const char *format, ...)
{
	(void)format;
	return sizeof(struct os_log_pack_s);
}

void
os_log_pack_fill(os_log_pack_t pack, size_t size, int saved_errno,
                  const char *format, ...)
{
	(void)size;
	(void)saved_errno;
	(void)format;
	if (pack != NULL) {
		memset(pack, 0, sizeof(*pack));
	}
}

void
os_log_pack_send(os_log_pack_t pack, os_log_t log, os_log_type_t type)
{
	(void)pack;
	(void)log;
	(void)type;
}

char *
os_log_pack_send_and_compose(os_log_pack_t pack, os_log_t log,
                              os_log_type_t type, char *buf, size_t buf_size)
{
	(void)pack;
	(void)log;
	(void)type;
	if (buf != NULL && buf_size > 0) {
		buf[0] = '\0';
	}
	return buf;
}

char *
os_log_pack_compose(os_log_pack_t pack, os_log_t log, os_log_type_t type,
                     char *buf, size_t buf_size)
{
	(void)pack;
	(void)log;
	(void)type;
	if (buf != NULL && buf_size > 0) {
		buf[0] = '\0';
	}
	return buf;
}
