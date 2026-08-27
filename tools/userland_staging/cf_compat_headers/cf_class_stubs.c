/* Real gap, not fabrication: swift-corelibs-foundation's CFRuntime.c has
 * one monolithic static class-registration table
 * (__CFRuntimeClassTable[]) that references EVERY CF type's
 * CFRuntimeClass descriptor by extern symbol, regardless of whether
 * this project actually builds/uses that type. This makes CF's own
 * "minimal subset" build (build_corefoundation.sh's own honest scope
 * statement) not link on its own without SOMETHING providing these
 * symbols for the types it doesn't build. (CFError/CFDate/CFURL/
 * CFCharacterSet/CFLocale/CFNumberFormatter/CFCalendar/CFTimeZone were
 * all originally in this list too, but each real port turned out to
 * compile clean with little to no extra work -- see
 * build_corefoundation.sh's own FILES array; their class-table entries
 * now come from the real .c files, not stubs. CFCharacterSet/CFLocale/
 * CFNumberFormatter/CFCalendar/CFTimeZone specifically only became
 * portable once this project got a real ICU port, 2026-08-26/27 --
 * see docs/backlog.md item d7.)
 *
 * Real CFRuntimeClass descriptors with every function pointer NULL are
 * genuinely safe here (see CFRuntime.h's own struct comment: NULL
 * fields just mean "no custom behavior"), because __CFRuntimeClassTable
 * entries are only ever dereferenced when a real object of that type is
 * created (CFRuntimeCreateInstance) -- and nothing in this project's
 * real call graph (launchd/core.c's plist parsing, IOKitUser) creates
 * CFRunLoop/CFBundle/etc instances, since those types have no real
 * ported implementation to create them WITH. Porting any of these
 * remaining subsystems for real is tracked as docs/backlog.md P2 d7
 * (RunLoop/Stream need real kqueue+Mach port integration; Bundle/PlugIn/
 * Preferences need real bundle-relative filesystem lookup logic, and
 * CFBundle.c itself was tried and reverted as a bad trade -- see
 * build_corefoundation.sh's own header comment).
 */
#include <CoreFoundation/CFBase.h>
#include "CFRuntime.h"

#define CF_STUB_CLASS(name) const CFRuntimeClass name = { \
    0, #name, 0, 0, 0, 0, 0, 0, 0, \
}

CF_STUB_CLASS(__CFAttributedStringClass);
CF_STUB_CLASS(__CFBundleClass);
CF_STUB_CLASS(__CFDateComponentsClass);
CF_STUB_CLASS(__CFDateFormatterClass);
CF_STUB_CLASS(__CFDateIntervalFormatterClass);
CF_STUB_CLASS(__CFMachPortClass);
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
CF_STUB_CLASS(__CFURLComponentsClass);
CF_STUB_CLASS(__CFWriteStreamClass);
/* __CFCharacterSetClass/__CFLocaleClass/__CFNumberFormatterClass/
 * __CFCalendarClass/__CFTimeZoneClass removed 2026-08-27: real
 * CFCharacterSet.c/CFLocale.c/CFNumberFormatter.c/CFCalendar.c/
 * CFTimeZone.c now built for real (see build_corefoundation.sh's FILES
 * array) now that this project has a real ICU port
 * (docs/backlog.md item d7's own "Real ICU port" entry) -- each
 * defines its own real CFRuntimeClass descriptor. */

/* Real CFBundle global CFRuntime.c declares extern -- still legitimately
 * NULL/unset since CFBundle.c is not built (see build_corefoundation.sh's
 * own header comment on the CFBundle.c revert). */
char *__CFBundleMainID = NULL;
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
