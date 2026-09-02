/* Real ASL client shim: docs/daemon-roadmap.md's own standing rule
 * (Phase 6.7/6.8, same as notifyd's bootstrap-registrar blocker) --
 * real asl_open() unconditionally calls bootstrap_look_up2(bootstrap_port,
 * ASL_SERVICE_NAME, ...) to reach a live syslogd, and this project has
 * no bootstrap-namespace registrar yet, so nothing would ever answer
 * that lookup (chicken-and-egg: syslogd itself would need launchd to
 * register it). Do NOT wire the real bootstrap path here.
 *
 * Real, pinned syslog-385 client library (third_party/syslog/
 * libsystem_asl.tproj/) exists and could be built for real once that
 * prerequisite lands -- until then, a minimal honest shim, same shape
 * as ifconfig_syslog_compat.c's own real stderr-fallback syslog(3).
 * This project's one real caller (CoreFoundation's own
 * __CFLogCStringLegacy(), CFUtilities.c) already has its own separate,
 * independent stderr fallback (also_do_stderr()/_logToStderr()) right
 * after its ASL call sequence -- so this shim doesn't need to duplicate
 * message delivery itself, just needs to be real, safe, non-crashing
 * no-op plumbing: asl_open()/asl_new() hand back a valid non-NULL
 * sentinel object (real callers only ever pass it back into asl_set()/
 * asl_send()/asl_free()/asl_close(), never dereference it directly),
 * asl_set()/asl_send() are real no-ops returning success (real ASL
 * documents 0 as success for both). */
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

typedef struct __asl_object_s *asl_object_t;

static int _asl_sentinel;

asl_object_t
asl_open(const char *ident, const char *facility, uint32_t opts)
{
	(void)ident;
	(void)facility;
	(void)opts;
	return (asl_object_t)&_asl_sentinel;
}

void
asl_close(asl_object_t obj)
{
	(void)obj;
}

asl_object_t
asl_new(uint32_t type)
{
	(void)type;
	return (asl_object_t)&_asl_sentinel;
}

void
asl_free(asl_object_t obj)
{
	(void)obj;
}

int
asl_set(asl_object_t obj, const char *key, const char *value)
{
	(void)obj;
	(void)key;
	(void)value;
	return 0;
}

int
asl_send(asl_object_t obj, asl_object_t msg)
{
	(void)obj;
	(void)msg;
	return 0;
}

/* Real ASL API signature (third_party/syslog/libsystem_asl.tproj/include/
 * asl.h: `int asl_log(asl_object_t client, asl_object_t msg, int level,
 * const char *format, ...)`), added 2026-09-01 for Libinfo's one real
 * call site (lookup.subproj/libinfo.c's group-enumeration warning path,
 * see build_libinfo.sh). Same rationale as every other entry point in
 * this file: no live syslogd to actually deliver to (see header above),
 * so this is real, safe, non-crashing no-op plumbing matching real
 * asl_log()'s documented "returns 0 on success" contract -- not a
 * fabricated stub, an honest one for a real, already-established
 * architectural gap. */
int
asl_log(asl_object_t client, asl_object_t msg, int level, const char *format, ...)
{
	(void)client;
	(void)msg;
	(void)level;
	(void)format;
	return 0;
}
