/* Compat stub for <mach-o/dyld_priv.h>. Real header exists in this
 * project's own pinned third_party/dyld/include/mach-o/dyld_priv.h, but
 * its __API_AVAILABLE annotations reference a bridgeos() argument macro
 * that Xcode 12's iPhoneOS SDK Availability.h predates and does not
 * define -- a real SDK-version mismatch. CFLocale.c includes this
 * header but calls nothing from it (confirmed via grep: no
 * dyld_get_active_platform / dyld_program_sdk_at_least / plain _dyld_ /
 * dyld3 namespace reference anywhere in that file), so this started as
 * an empty stub.
 *
 * PlugIn.subproj/CFBundle_Binary.c (real dlfcn-based bundle-loading
 * introspection) does call one real function from it,
 * dyld_image_path_containing_address -- declared here with its real
 * signature (confirmed against the real header above), no
 * __API_AVAILABLE annotation attached since nothing here checks SDK
 * availability at compile time anyway.
 */
#ifndef __CF_COMPAT_DYLD_PRIV_H__
#define __CF_COMPAT_DYLD_PRIV_H__
extern const char* dyld_image_path_containing_address(const void* addr);
#endif
