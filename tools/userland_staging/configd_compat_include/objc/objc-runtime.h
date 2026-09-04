#ifndef _CONFIGD_OBJC_RUNTIME_COMPAT_H_
#define _CONFIGD_OBJC_RUNTIME_COMPAT_H_
/* Real gap, SDK-vintage class: this pinned Xcode-12 iPhoneOS14.4 SDK ships
 * <objc/runtime.h>/<objc/message.h>/<objc/objc.h> but NOT the legacy
 * umbrella name <objc/objc-runtime.h> that configd.tproj/configd.m's real,
 * unmodified source #includes (confirmed via find over the real SDK --
 * zero hits for that exact filename, while the split-out headers all
 * exist). Real Apple's own <objc/objc-runtime.h> on SDKs that do carry it
 * is itself just this same forwarding shape (pulls in runtime.h + message.h
 * for source compatibility with pre-objc2 code) -- not a fabricated
 * substitute, the umbrella's real content IS "re-include the split
 * headers". configd.m itself never calls a single objc_*() runtime
 * function (confirmed by grep across the whole real Sources-phase file
 * set) -- it only wants the header to resolve.
 */
#include <objc/objc.h>
#include <objc/runtime.h>
#include <objc/message.h>
#endif /* _CONFIGD_OBJC_RUNTIME_COMPAT_H_ */
