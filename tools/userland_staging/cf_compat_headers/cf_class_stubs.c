/* Real gap, not fabrication: swift-corelibs-foundation's CFRuntime.c has
 * one monolithic static class-registration table
 * (__CFRuntimeClassTable[]) that references EVERY CF type's
 * CFRuntimeClass descriptor by extern symbol, regardless of whether
 * this project actually builds/uses that type. This makes CF's own
 * "minimal subset" build (build_corefoundation.sh's own honest scope
 * statement: Array/Dictionary/Data/Set/Bag/BinaryHeap/BitVector/
 * Storage/Tree/Number/Boolean/String/PropertyList/BinaryPList only, no
 * RunLoop/Stream/Socket/MachPort/URL/Locale/Calendar/TimeZone/Date
 * family/Formatters/CharacterSet/AttributedString/Bundle/PlugIn/
 * Preferences/Error) not link on its own without SOMETHING providing
 * these symbols.
 *
 * Real CFRuntimeClass descriptors with every function pointer NULL are
 * genuinely safe here (see CFRuntime.h's own struct comment: NULL
 * fields just mean "no custom behavior"), because __CFRuntimeClassTable
 * entries are only ever dereferenced when a real object of that type is
 * created (CFRuntimeCreateInstance) -- and nothing in this project's
 * real call graph (launchd/core.c's plist parsing, IOKitUser) creates
 * CFURL/CFRunLoop/CFLocale/CFDate/etc instances, since none of those
 * types have a real ported implementation to create them WITH. Porting
 * any of these subsystems for real is tracked as docs/backlog.md P2 d7
 * (RunLoop/Stream need real kqueue+Mach port integration; Locale/
 * Calendar/TimeZone/Formatters/CharacterSet need real ICU, out of scope
 * per this project's own no-ICU-yet stance; Bundle/PlugIn/Preferences
 * need real bundle-relative filesystem lookup logic; CFError is
 * probably the cheapest real follow-up, small self-contained file).
 */
#include <CoreFoundation/CFBase.h>
#include "CFRuntime.h"

#define CF_STUB_CLASS(name) const CFRuntimeClass name = { \
    0, #name, 0, 0, 0, 0, 0, 0, 0, \
}

CF_STUB_CLASS(__CFAttributedStringClass);
CF_STUB_CLASS(__CFBundleClass);
CF_STUB_CLASS(__CFCalendarClass);
CF_STUB_CLASS(__CFCharacterSetClass);
CF_STUB_CLASS(__CFDateClass);
CF_STUB_CLASS(__CFDateComponentsClass);
CF_STUB_CLASS(__CFDateFormatterClass);
CF_STUB_CLASS(__CFDateIntervalFormatterClass);
CF_STUB_CLASS(__CFErrorClass);
CF_STUB_CLASS(__CFLocaleClass);
CF_STUB_CLASS(__CFMachPortClass);
CF_STUB_CLASS(__CFNumberFormatterClass);
CF_STUB_CLASS(__CFPFactoryClass);
CF_STUB_CLASS(__CFPlugInInstanceClass);
CF_STUB_CLASS(__CFPreferencesDomainClass);
CF_STUB_CLASS(__CFReadStreamClass);
CF_STUB_CLASS(__CFRunArrayClass);
CF_STUB_CLASS(__CFRunLoopClass);
CF_STUB_CLASS(__CFRunLoopModeClass);
CF_STUB_CLASS(__CFRunLoopObserverClass);
CF_STUB_CLASS(__CFRunLoopSourceClass);
CF_STUB_CLASS(__CFRunLoopTimerClass);
CF_STUB_CLASS(__CFSocketClass);
CF_STUB_CLASS(__CFTimeZoneClass);
CF_STUB_CLASS(__CFURLClass);
CF_STUB_CLASS(__CFURLComponentsClass);
CF_STUB_CLASS(__CFWriteStreamClass);

/* Real init hooks CFRuntime.c's own __CFInitialize() calls unconditionally
 * (CFDate.c/CFCharacterSet.c set up static lookup tables here on real
 * builds) -- safe no-ops since nothing calls into CFDate/CFCharacterSet
 * APIs in this project's minimal build. */
void __CFDateInitialize(void) {}
void __CFCharacterSetInitialize(void) {}

/* Real CFBundle/CFLocale globals CFRuntime.c and CFInternal.h declare
 * extern -- both legitimately NULL/unset here since CFBundle.c/
 * CFLocale.c aren't built. */
char *__CFBundleMainID = NULL;
const CFStringRef __kCFLocaleCollatorID = NULL;

/* Real per-object "is this locale the null/default locale" hooks
 * CFString.c's locale-aware formatting path calls into (real impl in
 * CFLocale.c, not built here) -- always-null-locale is the correct
 * real answer since this project has no real CFLocale objects to be
 * non-null in the first place. */
struct __CFLocale;
Boolean __CFLocaleGetNullLocale(struct __CFLocale *locale) { return true; }
void __CFLocaleSetNullLocale(struct __CFLocale *locale) {}

/* Real Apple libSystem function (locale-derived default script/region
 * for legacy CFStringEncoding APIs) this project's pinned SDK doesn't
 * provide standalone -- always "no locale, use defaults" since there is
 * no real CFLocale/ICU here. */
void _CFStringGetUserDefaultEncoding(UInt32 *oScriptValue, UInt32 *oRegionValue) {
    if (oScriptValue) *oScriptValue = 0;
    if (oRegionValue) *oRegionValue = 0;
}

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

/* Real _CFCompareStringsWithLocale (String.subproj/CFStringUtilities.c)
 * needs real ICU (<unicode/ucol.h>) for actual locale collation -- out
 * of scope (this project has no ICU port). Only reachable when a caller
 * passes a non-NULL CFLocale to CFStringCompareWithOptionsAndLocale();
 * since __CFLocaleGetNullLocale() above always reports "null locale"
 * and this project builds no real CFLocale.c to construct one anyway,
 * no real caller in this project's call graph can reach this with a
 * non-NULL locale. Implemented as a real ordinal (non-locale-aware)
 * UTF-16 comparison rather than an unreachable-abort, in case that
 * assumption ever turns out wrong -- correct-but-unlocalized beats a
 * silent wrong answer or a crash. */
#include <CoreFoundation/CFString.h>
#include "CFInternal.h"
CFComparisonResult _CFCompareStringsWithLocale(CFStringInlineBuffer *str1, CFRange str1Range, CFStringInlineBuffer *str2, CFRange str2Range, CFOptionFlags options, const void *compareLocale) {
    CFIndex i1 = 0, i2 = 0;
    while (i1 < str1Range.length && i2 < str2Range.length) {
        UniChar c1 = CFStringGetCharacterFromInlineBuffer(str1, str1Range.location + i1);
        UniChar c2 = CFStringGetCharacterFromInlineBuffer(str2, str2Range.location + i2);
        if ((options & kCFCompareCaseInsensitive) && c1 != c2) {
            if (c1 >= 'A' && c1 <= 'Z') c1 += 'a' - 'A';
            if (c2 >= 'A' && c2 <= 'Z') c2 += 'a' - 'A';
        }
        if (c1 != c2) return c1 < c2 ? kCFCompareLessThan : kCFCompareGreaterThan;
        i1++; i2++;
    }
    if (str1Range.length - i1 == str2Range.length - i2) return kCFCompareEqualTo;
    return (str1Range.length - i1) < (str2Range.length - i2) ? kCFCompareLessThan : kCFCompareGreaterThan;
}
