/* Redirect to the REAL third_party/IOKitUser/IOKitLib.h (already pinned
 * + built by build_iokituser.sh into libc_build/libiokituser.a) --
 * IOKitUser's own checkout keeps IOKitLib.h flat at its repo root rather
 * than under an `IOKit/` subdirectory, so the real Darwin
 * `#include <IOKit/IOKitLib.h>` spelling real launchctl.c (PureDarwin)
 * uses doesn't resolve on its own. Same forwarding-shim pattern as
 * ../CoreFoundation/CFPriv.h in this same compat directory. IOKitLib.h's
 * own `#include <IOKit/IOTypes.h>` etc resolve separately via
 * `-idirafter $XNU/iokit`, which has a real `IOKit/` subdirectory
 * (xnu's iokit/IOKit/ headers are the same real shared kernel+user
 * headers as Apple's actual IOKit.framework on real Darwin).
 */
#include "../../../../third_party/IOKitUser/IOKitLib.h"
