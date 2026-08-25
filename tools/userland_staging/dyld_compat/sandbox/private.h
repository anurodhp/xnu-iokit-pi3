#ifndef _COMPAT_SANDBOX_PRIVATE_H_
#define _COMPAT_SANDBOX_PRIVATE_H_
#include <sys/types.h>
typedef enum {
    SANDBOX_FILTER_NONE = 0,
    SANDBOX_FILTER_PATH = 1,
    SANDBOX_FILTER_GLOBAL_NAME = 2,
    SANDBOX_FILTER_LOCAL_NAME = 3,
    SANDBOX_FILTER_APPLEEVENT_DESTINATION = 4,
    SANDBOX_FILTER_RIGHT_NAME = 5,
    SANDBOX_FILTER_PREFERENCE_DOMAIN = 6,
    SANDBOX_FILTER_KEXT_BUNDLE_ID = 7,
    SANDBOX_FILTER_INFO_TYPE = 8,
    SANDBOX_FILTER_NOTIFICATION = 9,
} sandbox_filter_type;
#define SANDBOX_CHECK_NO_REPORT 0x0001
#ifdef __cplusplus
extern "C" {
#endif
int sandbox_check(pid_t pid, const char *operation, sandbox_filter_type type, ...);
#ifdef __cplusplus
}
#endif
#endif
