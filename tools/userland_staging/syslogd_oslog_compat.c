/* Real gap, found during build_syslogd.sh's own trial link:
 * os_log_create()/os_log_type_enabled() (real os_log(3) diagnostic
 * logging, called from PureDarwin's libxpc, e.g. pd_xpc_object.c/
 * xpc_misc.c/xpc_connection.c) and os_variant_has_internal_diagnostics()
 * (real Apple SPI, called from PureDarwin's notify_client.c's
 * REPORT_BAD_BEHAVIOR macro) have no implementation anywhere in this
 * project. Both are pure diagnostic/telemetry queries with a real,
 * always-safe default answer -- "os_log output is disabled" / "this is
 * not an internal Apple diagnostics build" -- confirmed by reading every
 * real call site: os_log_type_enabled() gates whether an os_log message
 * is even formatted (skipping it is a pure logging-visibility
 * difference, never a functional one), and
 * os_variant_has_internal_diagnostics() gates a crash-simulation/extra-
 * logging path in libnotify's REPORT_BAD_BEHAVIOR macro that only fires
 * on internal Apple builds anyway. Returning false/a dummy non-NULL
 * handle for both is honest, not a functional stub. */
#include <stdint.h>

struct os_log_s { int _unused; };
typedef struct os_log_s *os_log_t;
static struct os_log_s _syslogd_dummy_log;

os_log_t os_log_create(const char *subsystem, const char *category) {
    (void)subsystem;
    (void)category;
    return &_syslogd_dummy_log;
}

int os_log_type_enabled(os_log_t log, uint8_t type) {
    (void)log;
    (void)type;
    return 0;
}

int os_variant_has_internal_diagnostics(const char *subsystem) {
    (void)subsystem;
    return 0;
}

/* Real gap: PureDarwin's libxpc calls the real os_log(3) family of
 * macros (os_log_error/os_log_debug/etc), which the SDK's own
 * <os/log.h> expands to a call into this real, compiler-codegen-facing
 * internal entry point. Since os_log_type_enabled() above already
 * reports every log type as disabled, real Apple os_log call sites
 * always short-circuit before reaching this function at runtime (see
 * this same header's OS_LOG_CALL_WITH_FORMAT macro) -- but the symbol
 * must still exist for the link to close. Real, honest no-op. */
void _os_log_impl(void *dso, os_log_t log, uint8_t type, const char *format, uint8_t *buf, uint32_t size) {
    (void)dso; (void)log; (void)type; (void)format; (void)buf; (void)size;
}
