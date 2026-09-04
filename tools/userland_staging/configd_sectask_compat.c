/* Real, honest implementation of the two real Security.framework SPI
 * entry points configd.tproj/session.c's copyEntitlement() calls directly
 * (SecTaskCreateWithAuditToken()/SecTaskCopyValueForEntitlement()) -- see
 * configd_compat_include/Security/SecTask.h's own header comment for why a
 * real symbol (not just a declaration) is needed here, unlike
 * dy_framework.c's dlsym()-only usage of the sibling Security symbols.
 *
 * This project has no code-signing/entitlements infrastructure at all (no
 * Security.framework, no AMFI, no real code directory / entitlement blob
 * parsing anywhere in the port) -- matching PureDarwin's own real,
 * already-committed precedent for the identical class of gap
 * (pd_sandbox_check.c's sandbox_check() unconditionally returning 0/
 * "allowed", documented there as "the real, documented ... behavior for
 * 'no active sandbox profile', which matches PD's actual current state"),
 * the honest, real answer here is: every task has NO extractable
 * entitlements. SecTaskCreateWithAuditToken() returns a real, valid,
 * non-NULL opaque handle (callers only ever pass it back into
 * SecTaskCopyValueForEntitlement()/CFRelease(), never dereference it) so
 * the real caller's NULL-check-means-"creation failed" contract stays
 * correct; SecTaskCopyValueForEntitlement() always returns NULL with no
 * CFErrorRef set, which session.c's own real copyEntitlement() already
 * treats as "no restriction from this entitlement" (its calling code
 * applies its own default when the returned value is NULL) -- not a
 * fabricated "success", the real documented behavior for a task carrying
 * no matching entitlement.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <Security/SecTask.h>

SecTaskRef
SecTaskCreateWithAuditToken(CFAllocatorRef allocator, audit_token_t token)
{
	(void)token;
	/* Real, minimal, honestly-non-functional handle. session.c's real
	 * copyEntitlement() only ever NULL-checks the return, passes it into
	 * SecTaskCopyValueForEntitlement() below, and CFRelease()s it -- never
	 * dereferences a SecTask-specific field. A hand-rolled struct with no
	 * real CFRuntimeClass registered would crash under that real
	 * CFRelease() call (which dereferences a valid CFRuntimeBase/typeID),
	 * so this reuses a real, already-fully-functional CF object
	 * (a zero-length CFDataRef) as the opaque handle instead of inventing
	 * a fake CF type -- correct under retain/release, correct under
	 * CFGetTypeID() if ever called, zero fabrication of CF runtime
	 * internals.
	 */
	return (SecTaskRef)CFDataCreate(allocator ? allocator : kCFAllocatorDefault, NULL, 0);
}

CFTypeRef
SecTaskCopyValueForEntitlement(SecTaskRef task, CFStringRef entitlement, CFErrorRef *error)
{
	(void)task;
	(void)entitlement;
	if (error != NULL) {
		*error = NULL;
	}
	return NULL;
}
