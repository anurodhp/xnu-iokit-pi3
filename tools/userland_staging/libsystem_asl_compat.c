/* Two real gaps in libsystem_asl.dylib (build_libsystem_asl_dylib.sh,
 * DAR-121), each with no real owning library in this project. Both are
 * kept LOCAL to the dylib (-unexported_symbol in that script), so this
 * file never becomes the canonical export of either symbol.
 *
 * os_log_shim_enabled(): real Apple SPI from libtrace (os/log_private.h,
 * never open-sourced). Every syslog-385 call site guards its os_log path
 * with it and falls back to the legacy send-to-syslogd path otherwise --
 * third_party/syslog/libsystem_asl.tproj/src/asl.c:811 (asl_vlog),
 * :878, :923, :1600 (asl_client_internal_send), and
 * libsystem_asl.tproj/src/syslog.c:141. This project has no os_log
 * backend: os_log_compat.c:43's os_log_type_enabled() always returns 0.
 * Answering "shim disabled" is the consistent answer, and it routes every
 * ASL/syslog(3) message to syslogd over com.apple.system.logger, which
 * is the only log sink this project has.
 *
 * mbr_check_membership(): real owner is Libinfo's membership.subproj,
 * never ported here (no opendirectoryd/membershipd). The one caller,
 * libsystem_asl.tproj/src/asl_core.c:193 (asl_core_check_group_access),
 * is a read-access check. Fail closed: "not a member", success return.
 * Same stand-in syslogd_dbserver_compat.c already uses for the same
 * call site in syslogd's static copy of asl_core.c.
 *
 * The rest are libc/libtrace symbols that exist in NO dylib this
 * project builds (checked with nm -gU across libc_build/system/, and
 * libSystem.B.dylib only re-exports), found by this dylib's first link:
 *
 * _NSGetArgv(): real Libc/sys/crt_externs.c:50 returns &NXArgv. NXArgv is
 * a real export of libdyld.dylib here. Same body as the copy
 * libopenpam.dylib already exports (date_compat.c), which is what
 * CoreFoundation binds today; not re-exported from here.
 *
 * atexit(): only caller is asl_fd.c:204 (asl_log_descriptor's
 * redirect_atexit, not reached by asl_open/asl_log/asl_send). Real
 * Libc/stdlib/FreeBSD/atexit.c keeps its own table, which this project
 * never built (libsystem_c_cxa_atexit_compat.c's header says why); that
 * file's __cxa_atexit, exported by libsystem_c.dylib, is the one real
 * registration table here, so register there with a NULL dso handle.
 *
 * atoll(): real Libc/stdlib/FreeBSD/atoll.c is
 * `strtoll_l(str, NULL, 10, __current_locale())`; __current_locale is
 * not exported by libsystem_c.dylib, so use the default-locale
 * strtoll(), which is what that call reduces to with no per-thread
 * locale set (nothing in libsystem_asl sets one).
 *
 * fls(): libplatform/src/string/generic/flsll.c:83's body, taking its
 * `__has_builtin(__builtin_clz)` branch (Xcode 12's clang has no
 * __builtin_fls). That file only compiles fls() for
 * VARIANT_DYLD && TARGET_OS_SIMULATOR, so it cannot be built as-is.
 *
 * os_activity_get_identifier()/_os_activity_current: libtrace, never
 * open-sourced. The one caller, asl.c:1087, attaches an OSActivityID
 * key only `if (osaid)`; 0 means "no current activity", which is true
 * with no activity-tracing subsystem. */
#include <sys/types.h>
#include <uuid/uuid.h>
#include <stdbool.h>

bool
os_log_shim_enabled(void *addr)
{
	(void)addr;
	return false;
}

int
mbr_check_membership(const uuid_t user, const uuid_t group, int *ismember)
{
	(void)user;
	(void)group;
	if (ismember) *ismember = 0;
	return 0;
}

#include <stdlib.h>
#include <stdint.h>

extern char **NXArgv;

char ***
_NSGetArgv(void)
{
	return &NXArgv;
}

extern int __cxa_atexit(void (*func)(void *), void *arg, void *dso);

int
atexit(void (*func)(void))
{
	return __cxa_atexit((void (*)(void *))func, NULL, NULL);
}

long long
atoll(const char *str)
{
	return strtoll(str, (char **)NULL, 10);
}

int
fls(int mask)
{
	if (mask == 0)
		return (0);

	return (sizeof(mask) << 3) - __builtin_clz(mask);
}

/* OS_ACTIVITY_CURRENT expands to `&_os_activity_current` (SDK os/activity.h);
 * only its address is ever passed, never dereferenced here. */
char _os_activity_current;

uint64_t
os_activity_get_identifier(void *activity, uint64_t *parent_id)
{
	(void)activity;
	if (parent_id) *parent_id = 0;
	return 0;
}
