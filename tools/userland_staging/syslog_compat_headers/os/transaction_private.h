/* Real os_transaction_t (os/transaction_private.h) has no source
 * anywhere in this project's pinned checkouts -- it's part of Apple's
 * closed-source libxpc/libdispatch private SPI used by launchd-
 * supervised daemons to defer idle-exit while work is pending
 * ("com.apple.syslogd" / "com.apple.syslogd.message" transactions in
 * daemon.c). This project's custom PID-1 (tools/init_binary/) has no
 * idle-exit lifecycle for spawned daemons at all -- syslogd runs until
 * killed, never idle-exits -- so there is nothing for a real
 * implementation to hook into regardless. Honest no-op stand-in
 * (opaque non-NULL sentinel, not a fabricated success): create/end are
 * real, callable, always-succeed no-ops, matching this project's
 * existing sandbox_spawnattrs_compat.h precedent for the same class of
 * "real API, structurally inapplicable on this target" gap.
 */
#ifndef _OS_TRANSACTION_PRIVATE_COMPAT_H_
#define _OS_TRANSACTION_PRIVATE_COMPAT_H_

typedef struct os_transaction_s *os_transaction_t;

static inline os_transaction_t
os_transaction_create(const char *name)
{
	(void)name;
	return (os_transaction_t)(void *)1; /* non-NULL sentinel, never dereferenced */
}

static inline void
os_transaction_end(os_transaction_t transaction)
{
	(void)transaction;
}

#endif /* _OS_TRANSACTION_PRIVATE_COMPAT_H_ */
