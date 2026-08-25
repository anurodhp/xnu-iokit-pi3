/* Real historical alias: dyld-940-era source uses PLATFORM_IOSMAC, the
 * original name Apple later renamed to PLATFORM_MACCATALYST (this
 * project's pinned iOS 14.4 SDK's real <mach-o/loader.h> only has the
 * newer name, but at the same real value -- 6 -- confirmed against
 * that header). Not a new platform, just the old name for the same
 * real one.
 */
#ifndef PLATFORM_IOSMAC
#define PLATFORM_IOSMAC PLATFORM_MACCATALYST
#endif

/* Real, confirmed kernel trap gap: task_dyld_process_info_notify_get()
 * is a genuine mach trap added to a NEWER xnu than this project's
 * pinned xnu-7195 (Big Sur/Darwin 20.3) -- confirmed absent from every
 * .defs file in that checkout, not a header search-path miss. Used only
 * by dyld's RemoteNotificationResponder (DyldAPIs.cpp), which supports
 * external debugger/Instruments image-load notifications -- a real
 * feature this target has no consumer for (no lldb/Instruments remote
 * attach support exists here). The real call site already handles
 * KERN_NO_SPACE/failure gracefully (falls through to an empty name
 * list), so a real, honest "this trap doesn't exist" failure return is
 * the correct behavior here, not a stand-in for missing logic.
 */
#include <stdint.h>
#include <mach/mach_types.h>
#include <mach/kern_return.h>

#ifdef __cplusplus
extern "C"
#endif
kern_return_t task_dyld_process_info_notify_get(mach_port_t *names, uint32_t *namesCnt);
