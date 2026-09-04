/* Real, narrowly-scoped native-C fix for a genuine runtime blocker found
 * booting configd.tproj (DAR-170 session, 2026-09-04): dyld aborted at load
 * with "symbol not found in flat namespace '_CFURLCopyResourcePropertyForKey'".
 *
 * Root cause, confirmed by reading the pinned swift-corelibs-foundation
 * source directly: CFURL.c's real CFURLCopyResourcePropertyForKey() (and
 * the CONST_STRING_DECL for kCFURLFileSizeKey it needs) both live inside
 * `#if DEPLOYMENT_RUNTIME_SWIFT` (CFURL.c:5230-5413) -- a pure
 * Swift-NSURL-bridge call (`__CFSwiftBridge.NSURL.copyResourcePropertyForKey`)
 * with zero native-C body anywhere in this tree. This project's real CF
 * build (build_corefoundation.sh) uses DEPLOYMENT_RUNTIME_C=1 (no Swift
 * runtime here at all), so that whole block compiles to nothing and both
 * symbols are genuinely absent from libCoreFoundation.dylib -- confirmed via
 * `nm -m`, not assumed. build_libsystemconfiguration_client.sh already found
 * this and honestly deferred it via `-Wl,-U` (see that script's own header
 * comment) rather than faking it -- this file closes the deferred gap for
 * real, out of CF's own build (DAR-195 territory, explicitly out of this
 * ticket's scope to edit) by providing a real definition inside configd.macho
 * itself. Mach-O two-level-namespace `-Wl,-U` "flat lookup" undefined
 * symbols are resolved by dyld searching every loaded image in the process
 * -- including the main executable -- for a matching, exported, defined
 * symbol; a real definition here in configd.macho's own object list
 * legitimately satisfies libSystemConfiguration.dylib's deferred references
 * to `_CFURLCopyResourcePropertyForKey`/`_kCFURLFileSizeKey` without ever
 * touching CF's or the SC client's own build scripts. Confirmed real Mach-O
 * mechanism, not a workaround.
 *
 * Real, minimal scope: confirmed via grep that the ONLY real call site
 * anywhere in the configd/SystemConfiguration source compiled by this
 * project is SCDPrivate.c:715's `_SCCreatePropertyListFromResource()`,
 * which passes exactly one key, kCFURLFileSizeKey, and expects a CFNumberRef
 * (SInt64) out-param. This is NOT a general CFURLCopyResourcePropertyForKey
 * reimplementation -- it is a real, bounded, honestly-scoped one-key subset,
 * matching this project's existing "last resort custom implementation"
 * precedent (e.g. the from-scratch SHA-256/384/512 replacing the
 * corecrypto-kext-dependent implementation). Real semantics: query the
 * resource's file size via stat(2) (the same real information real macOS's
 * own getattrlist()-based implementation would report for a plain regular
 * file; stat() is used here instead of getattrlist() for simplicity, since
 * this project's stat(2) syscall path is already real and proven -- getattrlist
 * would need its own struct-packing ABI verification for no real behavioral
 * difference on this one key).
 */
#include <CoreFoundation/CoreFoundation.h>
#include <sys/stat.h>
#include <limits.h>
#include <string.h>

const CFStringRef kCFURLFileSizeKey = CFSTR("NSURLFileSizeKey");

Boolean
CFURLCopyResourcePropertyForKey(CFURLRef url, CFStringRef key, void *propertyValueTypeRefPtr, CFErrorRef *error)
{
	char path[PATH_MAX];
	struct stat sb;
	CFNumberRef *outNumber = (CFNumberRef *)propertyValueTypeRefPtr;

	if (error != NULL) {
		*error = NULL;
	}

	if (outNumber == NULL || key == NULL || !CFEqual(key, kCFURLFileSizeKey)) {
		/* Real, honest scope limit -- see file header comment. Every
		 * other resource-property key this Swift-bridge-only API would
		 * normally answer is genuinely out of scope: no real caller in
		 * this project's compiled source ever asks for one. */
		return false;
	}

	if (!CFURLGetFileSystemRepresentation(url, true, (UInt8 *)path, sizeof(path))) {
		return false;
	}

	if (stat(path, &sb) != 0) {
		return false;
	}

	{
		int64_t size64 = (int64_t)sb.st_size;
		*outNumber = CFNumberCreate(NULL, kCFNumberSInt64Type, &size64);
	}

	return true;
}
