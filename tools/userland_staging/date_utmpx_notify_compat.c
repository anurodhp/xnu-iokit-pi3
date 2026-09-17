/* notify_post(): real, narrow, honest no-op stand-in for date(1)'s own
 * newly-real pututxline() path (DAR-149).
 *
 * Same exact reasoning as login_syscalls.c's own notify_post() (read
 * that file's header for the full writeup, not re-litigated here) -- a
 * real trial link of this project's already-built libnotify_client.a
 * surfaced a full real libdispatch + XPC + mach_port_allocate/destruct
 * dependency chain wildly disproportionate to this one call site, and
 * real, unmodified third_party/Libc/gen/NetBSD/utmpx.c's own ONLY call
 * site (`notify_post(UTMPX_CHANGE_NOTIFICATION)`, `__pututxline()`,
 * return value never even checked by the caller) is a real, best-effort,
 * fire-and-forget "tell anyone listening that utmpx changed" -- this
 * project has no real subscriber for that notification anywhere
 * (confirmed via grep for UTMPX_CHANGE_NOTIFICATION/notify_register* --
 * zero hits outside this one post site), so "no one is listening,
 * nothing happens" is the real, correct observable behavior on this
 * target regardless of whether notify_post() itself is real or this
 * honest stand-in.
 *
 * A dedicated copy (not a shared object pulled from login's own
 * login_syscalls.c) on purpose: that file also defines getpgid()/
 * shm_open()/fork()/alarm()/__strlcpy_chk()/endpwent(), none of which
 * date(1) needs or should silently pick up as a side effect of wanting
 * this one symbol -- same "narrow, scoped, per-consumer compat file"
 * discipline this project's other per-binary *_compat.c files already
 * follow (e.g. date_compat.c itself). Real success code
 * (NOTIFY_STATUS_OK == 0, third_party/PureDarwin/src/Libraries/XPC/
 * notify/notify.h) reused directly, not guessed. */
typedef unsigned int date_utmpx_notify_uint32_t;
#define DATE_UTMPX_NOTIFY_STATUS_OK 0

date_utmpx_notify_uint32_t
notify_post(const char *name)
{
	(void)name;
	return DATE_UTMPX_NOTIFY_STATUS_OK;
}
