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
 *
 * libxml2's HTMLparser.c/SAX2.c (real, unmodified Apple source, see
 * build_libxml2.sh) call two more real functions from it for a real
 * legacy-quirk check (detecting apps built against pre-10.9 SDKs to
 * preserve old parser behavior) -- dyld_get_program_sdk_version and
 * the DYLD_MACOSX_VERSION_10_9 constant it's compared against. Real
 * value 0x000A0900 confirmed against an older real dyld tag
 * (apple-oss-distributions/dyld dyld-360.14) that still has this
 * historically-stable macro -- our own pinned dyld-940 already
 * replaced it with the newer dyld_platform_t/dyld_build_version_t
 * system, so it's not in third_party/dyld's own header at all.
 */
#ifndef __CF_COMPAT_DYLD_PRIV_H__
#define __CF_COMPAT_DYLD_PRIV_H__
#include <stdint.h>
/* Real public header (unaffected by the bridgeos() SDK-vintage issue
 * above) -- HTMLparser.c/SAX2.c call real, public _dyld_get_image_name
 * but only #include this private header, not the public one directly
 * (real Apple SDKs transitively pull it in from the real
 * dyld_priv.h; ours doesn't, so it's included explicitly here). */
#include <mach-o/dyld.h>
extern const char* dyld_image_path_containing_address(const void* addr);
extern uint32_t dyld_get_program_sdk_version(void);
#define DYLD_MACOSX_VERSION_10_9 0x000A0900
#endif
