#ifndef _COMPAT_CRASHREPORTERCLIENT_H_
#define _COMPAT_CRASHREPORTERCLIENT_H_
#ifdef __cplusplus
extern "C" {
#endif
void CRSetCrashLogMessage(const char *msg);
void CRSetCrashLogMessage2(const char *msg);
#ifdef __cplusplus
}
#endif
#endif
