/* Redirect to the REAL swift-corelibs-foundation CFPriv.h (third_party/
 * swift-corelibs-foundation/CoreFoundation/Base.subproj/CFPriv.h,
 * already pinned + built by build_corefoundation.sh into
 * libc_build/libCoreFoundation.a) instead of this project's pinned
 * iPhoneOS14.4 SDK, which ships only the PUBLIC CoreFoundation.framework
 * headers -- CFPriv.h/ForFoundationOnly.h/CFLogUtilities.h are Apple SPI
 * headers, never shipped in any public SDK. real launchctl.c (PureDarwin,
 * third_party/PureDarwin/src/Libraries/XPC/launchctl/launchctl.c)
 * `#include`s these with the real Darwin `<CoreFoundation/CFPriv.h>`
 * angle-bracket spelling; this shim directory exists only to make that
 * spelling resolve to the real, already-pinned source instead of
 * requiring launchctl.c itself to be patched. Same forwarding-shim
 * pattern as launchd_compat_headers/bsm/libbsm.h.
 */
#include "../../../../third_party/swift-corelibs-foundation/CoreFoundation/Base.subproj/CFPriv.h"
