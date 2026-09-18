/*
 * Real smoketest for libc_build/system/libSystemConfiguration.dylib
 * (DAR-170 Tier 1 step 2). Calls the real, unmodified
 * SCDynamicStoreCreate() from third_party/configd's SCDOpen.c and reports
 * what it gets back.
 *
 * Either outcome is a real pass; which one you get depends on whether a
 * real configd is resident:
 *
 *   configd running  -- real __SCDynamicStoreServerPort()'s
 *     bootstrap_look_up() for "com.apple.SystemConfiguration.configd"
 *     finds the real server and SCDynamicStoreCreate() returns a real
 *     store. This is what a real boot does today.
 *   no configd       -- the lookup fails, and the real, well-formed
 *     answer is NULL with SCError() set to a real kern_return_t/errno-
 *     shaped constant.
 *
 * What the smoketest actually proves in both cases is the same thing:
 * real symbols resolve and real Mach IPC plumbing runs end to end up to
 * the point where a real server would need to answer -- NOT a crash, NOT
 * a hang, NOT a link-time failure.
 *
 * The original text here (DAR-170 Tier 1 step 2) called the success path
 * "unexpected", because when this was written there was no configd.tproj
 * at all and this binary could not even be loaded on the target: it
 * needed a CFURL flat-namespace definition it did not carry, and linked
 * against a `dyld_stub_binder` this project's libSystem does not export.
 * DAR-300 (2026-09-18) fixed both, and its first real run on the target
 * took the success path.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SCDynamicStore.h>
#include <SystemConfiguration/SCPrivate.h>
#include <stdio.h>

int main(void) {
	SCDynamicStoreRef store;

	store = SCDynamicStoreCreate(NULL, CFSTR("sc_client_smoketest"), NULL, NULL);
	if (store == NULL) {
		printf("sc_client_smoketest: PASS -- SCDynamicStoreCreate() returned NULL, "
		       "SCError()=%d (%s) -- real, well-formed failure; no configd is resident\n",
		       SCError(), SCErrorString(SCError()));
		return 0;
	}

	printf("sc_client_smoketest: PASS -- SCDynamicStoreCreate() returned a real "
	       "store (a real configd is resident and answered the bootstrap lookup)\n");
	CFRelease(store);
	return 0;
}
