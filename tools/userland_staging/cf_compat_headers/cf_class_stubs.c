/* Real gap, not fabrication: swift-corelibs-foundation's CFRuntime.c has
 * one monolithic static class-registration table
 * (__CFRuntimeClassTable[]) that references EVERY CF type's
 * CFRuntimeClass descriptor by extern symbol, regardless of whether
 * this project actually builds/uses that type. This makes CF's own
 * build (build_corefoundation.sh's own honest scope statement) not
 * link on its own without SOMETHING providing these symbols for the
 * types it doesn't build.
 *
 * Real CFRuntimeClass descriptors with every function pointer NULL are
 * genuinely safe here (see CFRuntime.h's own struct comment: NULL
 * fields just mean "no custom behavior"), because __CFRuntimeClassTable
 * entries are only ever dereferenced when a real object of that type is
 * created (CFRuntimeCreateInstance) -- and nothing in this project's
 * real call graph creates instances of the handful of types below,
 * since none of them have a real ported implementation to create them
 * WITH.
 *
 * As of the 2026-08-27 full-sweep pass (build_corefoundation.sh's own
 * header comment has the full story), this list is down to exactly the
 * types whose real .c file has a genuine, verified compile failure --
 * everything else (CFError/CFDate/CFURL/CFCharacterSet/CFLocale/
 * CFNumberFormatter/CFCalendar/CFTimeZone/CFAttributedString/CFBundle/
 * CFRunLoop/CFMachPort/CFSocket/CFStream/CFURLComponents/CFRunArray/
 * etc) now has its class-table entry come from the real file, not a
 * stub here. CFPreferences.c's own real, multi-part guard-mismatch gap
 * (see build_corefoundation.sh's own header comment) was closed
 * 2026-08-28 -- its stub was removed here the same day (real duplicate-
 * symbol conflict once the real file started providing
 * __CFPreferencesDomainClass itself, caught by a real trial link).
 * CFUserNotification.c (real GUI alert-dialog API, hard unavailable-
 * on-iOS SDK errors on its Icon/Sound/LocalizationURL keys -- no GUI on
 * this project's boot target regardless) is the one real type left
 * with a genuine compile failure -- see build_corefoundation.sh's own
 * header comment for the full breakdown. (CFWindowsUtilities.c, the third real exclusion, has no
 * stub here because it has nothing to stub: its entire body is one
 * `#if TARGET_OS_WIN32` block, so on this Darwin target it preprocesses
 * to a completely empty translation unit -- zero symbols, confirmed by
 * reading the file, not assumed.)
 */
#include <CoreFoundation/CFBase.h>
#include "CFRuntime.h"

#define CF_STUB_CLASS(name) const CFRuntimeClass name = { \
    0, #name, 0, 0, 0, 0, 0, 0, 0, \
}

CF_STUB_CLASS(__CFUserNotificationClass);

/* __kCFLocaleCollatorID / __CFLocaleGetNullLocale / __CFLocaleSetNullLocale /
 * _CFStringGetUserDefaultEncoding removed 2026-08-27: real CFLocale.c
 * (__kCFLocaleCollatorID, __CFLocaleGetNullLocale, __CFLocaleSetNullLocale)
 * and real CFStringEncodings.c (_CFStringGetUserDefaultEncoding) now
 * provide these for real. */

/* Real toll-free-bridging hooks CFDictionary.c calls unconditionally
 * under !DEPLOYMENT_RUNTIME_SWIFT (real Apple semantics: these live in
 * Foundation.framework, weak-imported, and return NULL/fall through to
 * CF's own native implementation whenever Foundation isn't loaded --
 * exactly this project's situation, since there is no real Objective-C
 * Foundation here). Always returning NULL reproduces that real, already
 *-handled fallback path verbatim, not a new behavior. */
#include <CoreFoundation/CFDictionary.h>
CFDictionaryRef __NSCFDictionaryCreate(CFAllocatorRef allocator, void const **klist, void const **vlist, CFIndex numValues, const CFDictionaryKeyCallBacks *keyCallBacks, const CFDictionaryValueCallBacks *valueCallBacks) { return NULL; }
CFMutableDictionaryRef __NSCFDictionaryCreateMutable(CFAllocatorRef allocator, CFIndex capacity, const CFDictionaryKeyCallBacks *keyCallBacks, const CFDictionaryValueCallBacks *valueCallBacks) { return NULL; }
CFDictionaryRef __NSCFDictionaryCreateTransfer(CFAllocatorRef allocator, const void **klist, const void **vlist, CFIndex numValues) { return NULL; }

/* _CFCompareStringsWithLocale stub removed 2026-08-27: real
 * String.subproj/CFStringUtilities.c (now built for real, see
 * build_corefoundation.sh's FILES array) provides the real ICU-backed
 * (<unicode/ucol.h>) implementation now that this project has a real
 * ICU port. */
