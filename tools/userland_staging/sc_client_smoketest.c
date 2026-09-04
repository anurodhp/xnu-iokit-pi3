/*
 * Real smoketest for libc_build/system/libSystemConfiguration.dylib
 * (DAR-170 Tier 1 step 2). Calls the real, unmodified
 * SCDynamicStoreCreate() from third_party/configd's SCDOpen.c and reports
 * what it gets back.
 *
 * Documented EXPECTED outcome (this project has no configd.tproj yet --
 * that's DAR-170 Tier 1 step 3, a separate future session): real
 * __SCDynamicStoreServerPort() calls bootstrap_look_up() for
 * "com.apple.SystemConfiguration.configd", which is not registered in
 * this project's bootstrap namespace, so the real, well-formed answer is
 * SCDynamicStoreCreate() returning NULL with SCError() set to a real
 * kern_return_t/errno-shaped constant -- NOT a crash, NOT a hang, NOT a
 * link-time failure. That is the real milestone this smoketest proves:
 * real symbols resolve and real Mach IPC plumbing runs end to end up to
 * the point where a real server would need to answer.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SCDynamicStore.h>
#include <SystemConfiguration/SCPrivate.h>
#include <stdio.h>

int main(void) {
	SCDynamicStoreRef store;

	store = SCDynamicStoreCreate(NULL, CFSTR("sc_client_smoketest"), NULL, NULL);
	if (store == NULL) {
		printf("sc_client_smoketest: SCDynamicStoreCreate() returned NULL, "
		       "SCError()=%d (%s) -- expected real failure, no configd.tproj server yet\n",
		       SCError(), SCErrorString(SCError()));
		return 0;
	}

	printf("sc_client_smoketest: SCDynamicStoreCreate() unexpectedly succeeded "
	       "(a real configd.tproj must already be running)\n");
	CFRelease(store);
	return 0;
}
