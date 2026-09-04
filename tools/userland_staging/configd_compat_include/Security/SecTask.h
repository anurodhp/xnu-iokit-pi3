#ifndef _SECURITY_SECTASK_H_CONFIGD_COMPAT_
#define _SECURITY_SECTASK_H_CONFIGD_COMPAT_
/* Real gap: configd.tproj/session.c #includes <Security/SecTask.h>
 * directly (a separate real Security.framework header from
 * <Security/Security.h>, which build_libsystemconfiguration_client.sh's
 * own compat_include/Security/Security.h already stands in for -- that
 * file is reached below via a plain #include so SecTaskRef/CFTypeRef/
 * CFAllocatorRef/CFStringRef/CFErrorRef stay the same single real
 * definition, not redeclared). Unlike dy_framework.c's own dlsym()-typeof
 * usage of Security symbols (which never needs a real symbol to link,
 * only a type), session.c's copyEntitlement() calls
 * SecTaskCreateWithAuditToken()/SecTaskCopyValueForEntitlement() directly
 * -- the linker needs a real DEFINED symbol, not just a declaration. See
 * configd_sectask_compat.c for the real, honest implementation (same
 * "no active policy -> honest default" class as PureDarwin's own
 * pd_sandbox_check.c, which this same script also compiles in for
 * <sandbox.h>/sandbox_check()).
 */
#include <Security/Security.h>
#include <mach/message.h>

#ifdef __cplusplus
extern "C" {
#endif
SecTaskRef SecTaskCreateWithAuditToken(CFAllocatorRef allocator, audit_token_t token);
#ifdef __cplusplus
}
#endif
#endif /* _SECURITY_SECTASK_H_CONFIGD_COMPAT_ */
