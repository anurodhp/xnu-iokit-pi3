/*
 * DAR-300 (2026-09-18): the three real, public CoreFoundation URL
 * resource-property symbols that have no native-C body anywhere in the
 * pinned swift-corelibs-foundation checkout, implemented here so they
 * live where real Apple puts them -- inside CoreFoundation itself --
 * instead of in libSystemConfiguration.dylib and in each of its
 * consuming executables.
 *
 * WHY THERE IS NO APPLE SOURCE TO PORT
 * ====================================
 *   CFURLResourceIsReachable            declared in the pinned tree's own
 *     CoreFoundation/URL.subproj/CFURL.h:666, with no implementation
 *     anywhere in CoreFoundation/ -- a full grep finds the name only in
 *     that declaration, its doc comment, and CFURLAccess.h's "Use
 *     CFURLResourceIsReachable instead" deprecation note on
 *     kCFURLFileExists. Real macOS implements it in the closed part of
 *     CF; swift-corelibs-foundation leaves it to Swift's
 *     NSURL.checkResourceIsReachable.
 *   CFURLCopyResourcePropertyForKey     body exists, but only inside
 *     `#if DEPLOYMENT_RUNTIME_SWIFT` (CFURL.c:5230-5413), and is a pure
 *     `__CFSwiftBridge.NSURL.copyResourcePropertyForKey` call.
 *   kCFURLFileSizeKey                   its CONST_STRING_DECL is in that
 *     same Swift-only block.
 *
 * This project builds CF with DEPLOYMENT_RUNTIME_C=1 and has no Swift
 * runtime, so the whole block compiles to nothing. That makes these the
 * documented "write custom code only as a last resort" case, not a
 * stand-in for source that could have been used.
 *
 * WHY IT MOVED HERE (this is the whole point of DAR-300)
 * =====================================================
 * Before this file, the three symbols lived in two different places,
 * neither of them CF:
 *   - build_libsystemconfiguration_client.sh deferred
 *     CFURLCopyResourcePropertyForKey/kCFURLFileSizeKey with `-Wl,-U`,
 *     and configd_cfurl_filesize_compat.c defined them inside EACH
 *     consuming executable. That mechanism is real (flat-namespace
 *     undefineds are resolved against every loaded image, the main
 *     executable included), but it made a definition mandatory in every
 *     image that loads libSystemConfiguration.dylib -- and the fourth
 *     such image, sc_client_smoketest.macho, did not carry one, so it
 *     could not load at all.
 *   - sc_cfurl_reachable_compat.c defined CFURLResourceIsReachable
 *     inside libSystemConfiguration.dylib, deliberately NOT as a third
 *     `-Wl,-U`, to avoid widening that trap (DAR-287).
 * Both are deleted by this change. Consumers now get all three the
 * ordinary way, as real exports of libCoreFoundation.dylib.
 *
 * Compiled with build_corefoundation.sh's own CFLAGS so it shares
 * CFRuntime/CFInternal internals, but lives in this project's tree
 * rather than the vendored fork since there is no Apple source to
 * patch -- same convention as cf_filedescriptor_compat.c (DAR-252).
 *
 * REAL SEMANTICS
 * ==============
 * CFURLResourceIsReachable, per CFURL.h's own documentation: "checks if
 * the resource's backing store is reachable... currently applicable only
 * to URLs for file system resources. If this function returns false, the
 * optional error is populated. For other URL types, false is returned."
 * So non-file URLs are false, file URLs are reachable exactly when the
 * path stat(2)s, and a false return populates *error with a real
 * POSIX-domain CFError carrying the real errno.
 *
 * CFURLCopyResourcePropertyForKey stays honestly scoped to the one key
 * this project's compiled source ever asks for. Confirmed by grep: the
 * only real call site in anything built here is SCDPrivate.c:715's
 * _SCCreatePropertyListFromResource(), which passes exactly
 * kCFURLFileSizeKey and expects a CFNumberRef (SInt64) out-param. Any
 * other key returns false rather than a fabricated answer. stat(2)
 * supplies the size -- the same real information real macOS's own
 * getattrlist()-based implementation reports for a regular file, and
 * this project's stat(2) path is already real and proven, where
 * getattrlist() would need its own struct-packing ABI verification for
 * no behavioural difference on this one key.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

const CFStringRef kCFURLFileSizeKey = CFSTR("NSURLFileSizeKey");

static void
cf_set_posix_error(CFErrorRef *error, int code)
{
	if (error != NULL) {
		*error = CFErrorCreate(NULL, kCFErrorDomainPOSIX, code, NULL);
	}
}

Boolean
CFURLResourceIsReachable(CFURLRef url, CFErrorRef *error)
{
	char path[PATH_MAX];
	struct stat sb;
	CFStringRef scheme;
	Boolean isFile;

	if (error != NULL) {
		*error = NULL;
	}

	if (url == NULL) {
		cf_set_posix_error(error, EINVAL);
		return false;
	}

	/* "For other URL types, false is returned." A URL with no scheme at
	 * all is relative and un-resolvable on its own, which is the same
	 * answer. */
	scheme = CFURLCopyScheme(url);
	if (scheme == NULL) {
		cf_set_posix_error(error, EINVAL);
		return false;
	}
	isFile = CFEqual(scheme, CFSTR("file"));
	CFRelease(scheme);
	if (!isFile) {
		cf_set_posix_error(error, EINVAL);
		return false;
	}

	if (!CFURLGetFileSystemRepresentation(url, true, (UInt8 *)path, sizeof(path))) {
		cf_set_posix_error(error, ENAMETOOLONG);
		return false;
	}

	if (stat(path, &sb) != 0) {
		cf_set_posix_error(error, errno);
		return false;
	}

	return true;
}

Boolean
CFURLCopyResourcePropertyForKey(CFURLRef url, CFStringRef key, void *propertyValueTypeRefPtr, CFErrorRef *error)
{
	char path[PATH_MAX];
	struct stat sb;
	CFNumberRef *outNumber = (CFNumberRef *)propertyValueTypeRefPtr;

	if (error != NULL) {
		*error = NULL;
	}

	if (url == NULL) {
		cf_set_posix_error(error, EINVAL);
		return false;
	}

	if (outNumber == NULL || key == NULL || !CFEqual(key, kCFURLFileSizeKey)) {
		/* Real, honest scope limit -- see this file's header comment.
		 * Every other resource-property key this Swift-bridge-only API
		 * would normally answer is genuinely out of scope: no real
		 * caller in this project's compiled source ever asks for one. */
		cf_set_posix_error(error, EINVAL);
		return false;
	}

	if (!CFURLGetFileSystemRepresentation(url, true, (UInt8 *)path, sizeof(path))) {
		cf_set_posix_error(error, ENAMETOOLONG);
		return false;
	}

	if (stat(path, &sb) != 0) {
		cf_set_posix_error(error, errno);
		return false;
	}

	{
		int64_t size64 = (int64_t)sb.st_size;
		*outNumber = CFNumberCreate(NULL, kCFNumberSInt64Type, &size64);
	}

	return true;
}
