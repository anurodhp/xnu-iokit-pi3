/* Redirect to the REAL swift-corelibs-foundation ForFoundationOnly.h --
 * see CFPriv.h in this same directory for the full real-fix writeup;
 * identical forwarding-shim reason (Apple SPI header, never shipped in
 * any public SDK, needed by real launchctl.c's own real
 * `#include <CoreFoundation/ForFoundationOnly.h>`).
 */
#include "../../../../third_party/swift-corelibs-foundation/CoreFoundation/Base.subproj/ForFoundationOnly.h"
