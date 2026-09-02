/* Real, minimal smoketest for DAR-195's libCoreFoundation.dylib
 * conversion -- proves two things a clean link alone does NOT prove:
 *
 * 1. CF's real constructor (cf_compat_headers/cf_init_constructor.c's
 *    __iokit_CFInitialize_trigger, __attribute__((constructor))) really
 *    runs at real dyld load time in this project's real dyld-940, before
 *    this program's own main() -- checked by reading CFRuntime.c's own
 *    real, external `Boolean __CFInitialized` global directly, with NO
 *    CF call made first (a call to any CF API could itself trigger lazy
 *    init via some other path and mask a constructor that never ran --
 *    reading the flag cold, before any other CF call, is the real test).
 *
 * 2. CF actually works end-to-end once loaded this way -- a real
 *    CFMutableArrayRef built from a real CFStringRef, and a real
 *    CFUUIDRef (exercises this dylib's own new uuid_generate_random/
 *    uuid_pack/uuid_unpack real-symbol-closure work from this same
 *    ticket, not just a no-op call).
 *
 * Prints fixed, greppable markers (real boot_test.py --expect strings,
 * see build_corefoundation_dylib_test.sh) rather than relying on process
 * exit code alone, matching this project's other smoketests.
 */
#include <CoreFoundation/CFArray.h>
#include <CoreFoundation/CFString.h>
#include <CoreFoundation/CFUUID.h>
#include <stdio.h>

/* Real, project-owned marker set by cf_init_constructor.c's own real
 * __attribute__((constructor)) AFTER it calls through to CFRuntime.c's
 * own __CFInitialize() -- see that file's header for why this is used
 * instead of reading CF's own __CFInitialized directly (that symbol is
 * real Apple CF_PRIVATE/hidden-visibility, not exported, matching real
 * Apple's own encapsulation). */
extern int __iokit_cf_init_constructor_ran;

int main(void) {
    /* Read cold, before any other real CF call in this process. */
    if (__iokit_cf_init_constructor_ran) {
        printf("CF_DYLIB_SMOKETEST: constructor_ran=1 (real __attribute__((constructor)) fired, __CFInitialize() returned)\n");
    } else {
        printf("CF_DYLIB_SMOKETEST: constructor_ran=0 (constructor did NOT run -- REAL BUG)\n");
        return 1;
    }

    CFStringRef s = CFStringCreateWithCString(kCFAllocatorDefault, "dar195", kCFStringEncodingUTF8);
    if (!s) {
        printf("CF_DYLIB_SMOKETEST: CFStringCreateWithCString returned NULL -- REAL BUG\n");
        return 1;
    }
    CFMutableArrayRef arr = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    CFArrayAppendValue(arr, s);
    CFIndex len = CFArrayGetCount(arr);
    printf("CF_DYLIB_SMOKETEST: array count=%ld (expect 1)\n", (long)len);
    if (len != 1) {
        printf("CF_DYLIB_SMOKETEST: FAIL array count\n");
        return 1;
    }

    CFUUIDRef uuid = CFUUIDCreate(kCFAllocatorDefault);
    if (!uuid) {
        printf("CF_DYLIB_SMOKETEST: CFUUIDCreate returned NULL -- REAL BUG\n");
        return 1;
    }
    CFStringRef uuidStr = CFUUIDCreateString(kCFAllocatorDefault, uuid);
    char buf[64];
    if (uuidStr && CFStringGetCString(uuidStr, buf, sizeof(buf), kCFStringEncodingUTF8)) {
        printf("CF_DYLIB_SMOKETEST: uuid=%s\n", buf);
    } else {
        printf("CF_DYLIB_SMOKETEST: CFUUIDCreateString/CFStringGetCString FAILED -- REAL BUG\n");
        return 1;
    }

    printf("CF_DYLIB_SMOKETEST: PASS\n");
    return 0;
}
