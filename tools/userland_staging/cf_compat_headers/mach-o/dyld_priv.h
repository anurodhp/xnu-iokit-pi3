/* Compat stub for <mach-o/dyld_priv.h>. CFLocale.c includes this real
 * private dyld header but calls nothing declared in it (confirmed via
 * grep for every dyld_get_active_platform / dyld_program_sdk_at_least /
 * plain _dyld_ / dyld3 namespace reference across that file: none).
 * The real header exists in this project's own pinned
 * third_party/dyld/include/mach-o/dyld_priv.h, but its
 * __API_AVAILABLE availability annotations reference a bridgeos()
 * argument macro that Xcode 12's iPhoneOS SDK Availability.h predates
 * and does not define -- a real SDK-version mismatch, not a reason to
 * fight a header nothing here actually calls.
 */
#ifndef __CF_COMPAT_DYLD_PRIV_H__
#define __CF_COMPAT_DYLD_PRIV_H__
#endif
