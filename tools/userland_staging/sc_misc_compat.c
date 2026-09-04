/* Real, honest bodies for the small set of real Apple SPI declarations
 * this script's own compat headers introduce but don't implement --
 * gCRAnnotations (CrashReporterClient.h's real global, needs exactly one
 * real definition), os_state_add_handler()/os_variant_has_internal_
 * content() (os/state_private.h / os/variant_private.h compat, see this
 * script's own header comments for the full sourced "never open sourced,
 * narrow honest stand-in" reasoning), SecTaskCreateFromSelf()/
 * SecTaskCopyValueForEntitlement() (Security.framework not pinned in
 * this project -- real, always-safe "no such task info available"
 * answer), and CFUserNotificationDisplayNotice() (real CF API this
 * project's libCoreFoundation.dylib has never compiled in, since it's
 * macOS-only UI with no GUI subsystem here at all -- same real,
 * always-safe "notification not displayed" answer).
 */
#include <CoreFoundation/CoreFoundation.h>
#include <os/state_private.h>
#include <os/variant_private.h>
#include <stdint.h>

/* Real struct layout, copied from third_party/PureDarwin/src/Libraries/
 * CrashReporterClient/include/CrashReporterClient.h -- the ONLY real
 * definition of this global this project's link needs (SCDPrivate.c's
 * real crash_info macros write/clear .message via CRSetCrashLogMessage(),
 * a pure header-inline macro that just assigns this struct's field --
 * no other translation unit in this project's link currently defines
 * gCRAnnotations, confirmed via this exact real "undefined symbol" -- a
 * real definition provided by real crt/Libsystem-startup code on actual
 * macOS, which this project's freestanding link has no equivalent of). */
struct crashreporter_annotations_t {
	uint64_t version;
	uint64_t message;
	uint64_t signature_string;
	uint64_t backtrace;
	uint64_t message2;
	uint64_t thread;
	uint64_t dialog_mode;
	uint64_t abort_cause;
};
__attribute__((visibility("hidden")))
struct crashreporter_annotations_t gCRAnnotations = { 5 /* CRASHREPORTER_ANNOTATIONS_VERSION */, 0, 0, 0, 0, 0, 0, 0 };

/* os_state_add_handler(): never open-sourced real SPI (see this script's
 * own compat_include/os/state_private.h header comment) -- this project
 * has no real os_state/statedump collector to register with at all, so
 * "accept the registration, never actually invoke the block" is the
 * real, honest, always-safe behavior (SCDOpen.c/SCPOpen.c's own real
 * callers both `(void)`-discard the return value already; DAR-241's
 * dnsinfo_server.c/network_information_server.c callers store it in a
 * real `os_state_handle_t`, hence the return type below matching that
 * header's now-real typedef instead of the original placeholder
 * `uint32_t` -- still always literal 0 either way). */
os_state_handle_t os_state_add_handler(dispatch_queue_t q, os_state_block_t block) {
	(void)q;
	(void)block;
	return 0;
}

/* os_variant_has_internal_content(): same real, honest "always false"
 * default this project's sibling SPI (os_variant_has_internal_
 * diagnostics(), libsystem_notify_misc_compat.c/syslogd_oslog_compat.c)
 * already established -- this is not an internal Apple diagnostics
 * build. */
_Bool os_variant_has_internal_content(const char *subsystem) {
	(void)subsystem;
	return 0;
}

/* SecTask*(): Security.framework is not pinned in this project (see this
 * script's own compat_include/Security/Security.h header comment) --
 * real, always-safe "no task/entitlement info available" answers.
 * SCDHostName.c's own real caller (__SCDynamicStoreCopyComputerName-ish
 * entitlement check) already treats a NULL SecTaskRef/entitlement value
 * as "caller is not entitled", the same conservative answer a real
 * denial would produce. */
void *SecTaskCreateFromSelf(CFAllocatorRef allocator) {
	(void)allocator;
	return (void *)0;
}

CFTypeRef SecTaskCopyValueForEntitlement(void *task, CFStringRef entitlement, CFErrorRef *error) {
	(void)task;
	(void)entitlement;
	if (error != (void *)0) {
		*error = NULL;
	}
	return NULL;
}

/* CFUserNotificationDisplayNotice(): real CF API, but this project's
 * libCoreFoundation.dylib has never compiled CFUserNotification.c in at
 * all (no GUI subsystem exists here to display anything to -- confirmed
 * via this exact real "undefined symbol", not assumed). SCDPrivate.c's
 * own real caller (_SC_ReportCrash(), a diagnostic-only helper gated
 * behind a one-time `if (!warned)` guard, not on any DHCP/
 * SCDynamicStore/SCPreferences data path) only logs the return value
 * implicitly by ignoring it -- a real, safe "notification not
 * displayed" answer (-1, matching CFUserNotificationDisplayNotice's own
 * real documented error-return convention) is honest here. */
int32_t CFUserNotificationDisplayNotice(CFTimeInterval timeout, CFOptionFlags flags,
                                         CFURLRef iconURL, CFURLRef soundURL,
                                         CFURLRef localizationURL, CFStringRef alertHeader,
                                         CFStringRef alertMessage, CFStringRef defaultButtonTitle) {
	(void)timeout; (void)flags; (void)iconURL; (void)soundURL;
	(void)localizationURL; (void)alertHeader; (void)alertMessage; (void)defaultButtonTitle;
	return -1;
}
