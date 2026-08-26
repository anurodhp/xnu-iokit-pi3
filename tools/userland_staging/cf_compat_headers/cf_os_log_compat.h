/* This project's pinned iPhoneOS 14.4 SDK's <os/log.h> predates
 * OS_LOG_SUBSYSTEM_RUNTIME_ISSUES (a fixed subsystem-name macro added in
 * a later SDK; real value "com.apple.runtime-issues", used verbatim
 * across Apple's own open-source consumers e.g. libpthread). Only
 * CFUtilities.c's _CFRuntimeIssuesLog()/_CFFoundationRuntimeIssuesLog()
 * need it here.
 */
#ifndef CF_OS_LOG_COMPAT_H
#define CF_OS_LOG_COMPAT_H
#ifndef OS_LOG_SUBSYSTEM_RUNTIME_ISSUES
#define OS_LOG_SUBSYSTEM_RUNTIME_ISSUES "com.apple.runtime-issues"
#endif
#endif
