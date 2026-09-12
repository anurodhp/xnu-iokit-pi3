/*
 * DAR-287 (2026-09-11): a real, native-C CFURLResourceIsReachable() for
 * libSystemConfiguration.dylib, which is where SCNetworkMigration.c --
 * its only consumer in this whole project -- now lives.
 *
 * WHY THIS IS A REAL GAP, NOT A SHORTCUT
 * ======================================
 * CFURLResourceIsReachable() is real, public CoreFoundation API: it is
 * declared in the pinned swift-corelibs-foundation checkout's own
 * CoreFoundation/URL.subproj/CFURL.h:666 with full availability
 * annotations. It has NO implementation anywhere in that source tree --
 * confirmed by a full grep of CoreFoundation/, which finds the symbol
 * only in CFURL.h's declaration, CFURL.h's own doc comment, and
 * CFURLAccess.h's "Use CFURLResourceIsReachable instead" deprecation
 * note for kCFURLFileExists. Real macOS implements it inside the closed
 * parts of CoreFoundation; swift-corelibs-foundation leaves the job to
 * Swift's NSURL.checkResourceIsReachable. Either way there is no Apple
 * C body to port, so this is the documented "write custom code only as a
 * last resort" case, not a stand-in for source we could have used.
 *
 * WHY IT LIVES HERE AND NOT IN THE CONSUMING EXECUTABLES
 * =====================================================
 * This project already had one symbol in exactly this category --
 * CFURLCopyResourcePropertyForKey/kCFURLFileSizeKey, real CF API whose
 * only bodies are behind `#if DEPLOYMENT_RUNTIME_SWIFT` -- and solved it
 * the other way: build_libsystemconfiguration_client.sh defers both with
 * `-Wl,-U`, and configd_cfurl_filesize_compat.c supplies real
 * definitions inside each executable. That works (flat-namespace
 * undefineds really are resolved against every loaded image, the main
 * executable included), but it makes a real definition mandatory in
 * EVERY image that loads this dylib -- configd.macho, ipconfig.macho and
 * scutil.macho all carry that file for this reason, and the fourth such
 * image, sc_client_smoketest.macho, does not and so cannot load.
 *
 * Repeating that pattern for a third symbol would widen a trap rather
 * than close one, and would mean relinking configd.macho and
 * ipconfig.macho -- the boot-proven DHCP chain -- for a symbol neither
 * of them calls. Defining it inside this dylib instead keeps the dylib
 * self-contained: the definition sits beside its single caller, adds no
 * new flat-namespace deferral, and leaves every existing consumer
 * binary byte-identical.
 *
 * This is still not the canonical home. Real Apple's is CoreFoundation,
 * and all three of these symbols belong in libCoreFoundation.dylib.
 * That move is filed separately rather than attempted here: CF's own
 * build scripts do not currently run end-to-end in this tree (they abort
 * on a missing os/alloc_once_private.h, the same shared-compat-header
 * class of breakage DAR-247 turned out to be), so touching CF is its own
 * piece of work, not a side effect of this one.
 *
 * REAL SEMANTICS
 * ==============
 * Per CFURL.h's own documentation of the API: "CFURLResourceIsReachable
 * synchronously checks if the resource's backing store is reachable...
 * This function is currently applicable only to URLs for file system
 * resources. If this function returns false, the optional error is
 * populated. For other URL types, false is returned."
 *
 * So: non-file URLs are false; file URLs are reachable exactly when the
 * path can be stat(2)'d; a false return populates *error when the caller
 * asked for one, with a real POSIX-domain CFError carrying the real
 * errno. All five real call sites (SCNetworkMigration.c:4410, :4420,
 * :4445, :4456, :4557) pass NULL for error and use only the Boolean, so
 * the error path is for API correctness rather than for any behaviour
 * this project depends on.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>

static void
set_posix_error(CFErrorRef *error, int code)
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
		set_posix_error(error, EINVAL);
		return false;
	}

	/* "For other URL types, false is returned." A URL with no scheme at
	 * all is relative and un-resolvable on its own, which is the same
	 * answer. */
	scheme = CFURLCopyScheme(url);
	if (scheme == NULL) {
		set_posix_error(error, EINVAL);
		return false;
	}
	isFile = CFEqual(scheme, CFSTR("file"));
	CFRelease(scheme);
	if (!isFile) {
		set_posix_error(error, EINVAL);
		return false;
	}

	if (!CFURLGetFileSystemRepresentation(url, true, (UInt8 *)path, sizeof(path))) {
		set_posix_error(error, ENAMETOOLONG);
		return false;
	}

	if (stat(path, &sb) != 0) {
		set_posix_error(error, errno);
		return false;
	}

	return true;
}
