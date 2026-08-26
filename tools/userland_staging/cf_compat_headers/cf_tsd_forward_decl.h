/* Real upstream (swift-corelibs-foundation) inconsistency: CFRuntime.c's
 * own __CFTSDInitialize() forward declaration is guarded by
 * TARGET_OS_OSX, but the actual call site three hundred lines later is
 * guarded by the broader TARGET_OS_MAC -- fine on real macOS
 * (TARGET_OS_OSX implies TARGET_OS_MAC) but a real gap on iOS/other
 * Apple platforms with DEPLOYMENT_RUNTIME_OBJC=0, which this project's
 * arm64-apple-ios14.4 target hits. __CFTSDInitialize() is really defined
 * in CFPlatform.c unconditionally; just forward-declare it here so the
 * call site in CFRuntime.c resolves.
 */
#ifndef CF_TSD_FORWARD_DECL_H
#define CF_TSD_FORWARD_DECL_H
extern void __CFTSDInitialize(void);
#endif
