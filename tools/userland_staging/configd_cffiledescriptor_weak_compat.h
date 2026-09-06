#ifndef _CONFIGD_CFFILEDESCRIPTOR_WEAK_COMPAT_H_
#define _CONFIGD_CFFILEDESCRIPTOR_WEAK_COMPAT_H_
/* DAR-252 follow-up (2026-09-06): real Apple's own CFFileDescriptorCreate()
 * family (FDSet.c's real FDCalloutCreateFileDescriptor(), the raw-BPF-fd
 * receive path FDCalloutCreate() takes for a non-socket fd) is genuinely
 * absent from every real Apple CF source this project has access to --
 * not just swift-corelibs-foundation (already established, see
 * build_configd.sh's own DAR-252 comment), but also the real
 * apple-oss-distributions/CF repo itself: cloned CF-855.17 directly and
 * grepped for CFFileDescriptorGetTypeID/__CFFileDescriptor, zero hits.
 * Even CFFileDescriptorRef's own struct tag is undeclared anywhere in
 * that checkout -- Apple's public CF source drop has never included this
 * file, on any version, confirmed rather than assumed.
 *
 * build_configd.sh's original DAR-252 fix (-Wl,-U,_CFFileDescriptorCreate
 * et al, an "undefined, dynamically-looked-up" deferral) was written
 * assuming classic lazy dyld binding: a deferred symbol only gets
 * resolved -- and only fails -- the first time FDCalloutCreate() actually
 * takes the raw-fd branch, which real DHCP lease negotiation only does
 * for the BPF-based broadcast-reply receive path, not on every launch.
 * This project's default -fixup_chains link mode resolves EVERY bind
 * eagerly at process start instead, so that same "-Wl,-U" symbol now
 * aborts configd's dyld load unconditionally, before main() ever runs --
 * a real, systemic regression class also hit and fixed today for
 * notifyd's libsystem_c.dylib->libsystem_m.dylib asin/sqrt gap and
 * libsystem_kernel.dylib's missing getgid trap (see build_notifyd.sh's
 * and build_libsystem_kernel.sh's own updated header comments). Found
 * the same way both of those were: a real kernel-side
 * abort_with_payload() reason-string probe (bsd/kern/kern_exit.c,
 * -user_fault_debug) reading dyld's own real
 * "symbol not found in flat namespace '_CFFileDescriptorCreate'" halt
 * message, since configd's stderr is never wired to /dev/console either.
 *
 * Real fix for THIS regression class (as opposed to DAR-252's still-open
 * "write a real CFFileDescriptor implementation" scope): mark these five
 * symbols __attribute__((weak_import)) instead of a bare "-Wl,-U"
 * dynamic-lookup deferral. Real dyld's chained-fixups format has a
 * distinct WEAK bind kind precisely for this case -- a weak-imported
 * symbol that is missing at load time binds to NULL instead of aborting,
 * even under eager -fixup_chains resolution (unlike a plain flat/dynamic
 * "-Wl,-U" bind, which chained fixups always eagerly resolves and always
 * treats a miss as fatal). This header shadows the real SDK's
 * CFFileDescriptor.h (matching its own real include guard exactly, so
 * the SDK header's body never runs once this is -include'd first) and
 * re-declares the same five real, unimplemented-here entry points
 * weak-imported, using the exact real signatures from this project's own
 * pinned SDK copy of that header (not guessed). This does NOT implement
 * CFFileDescriptor -- DAR-252's real port is still owed before
 * FDCalloutCreateFileDescriptor()'s raw-BPF-fd path can actually work;
 * calling any of these five once configd is actually running will null-
 * deref through an unbound weak import, a real, narrower, and far more
 * diagnosable follow-on bug than "the whole daemon cannot launch at
 * all". Whether real DHCP lease negotiation over Ethernet actually
 * reaches this path (vs. staying on FDCalloutCreate()'s real, already-
 * working CFSocket branch for a genuine socket fd) is exactly the next
 * thing DAR-170's own QEMU verification needs to determine.
 */
#define __COREFOUNDATION_CFFILEDESCRIPTOR__ 1

#include <CoreFoundation/CFRunLoop.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int CFFileDescriptorNativeDescriptor;
typedef struct __CFFileDescriptor *CFFileDescriptorRef;

enum {
    kCFFileDescriptorReadCallBack = 1UL << 0,
    kCFFileDescriptorWriteCallBack = 1UL << 1
};

typedef void (*CFFileDescriptorCallBack)(CFFileDescriptorRef f, CFOptionFlags callBackTypes, void *info);

typedef struct {
    CFIndex	version;
    void *	info;
    void *	(*retain)(void *info);
    void	(*release)(void *info);
    CFStringRef	(*copyDescription)(void *info);
} CFFileDescriptorContext;

extern CFTypeID CFFileDescriptorGetTypeID(void) __attribute__((weak_import));
extern CFFileDescriptorRef CFFileDescriptorCreate(CFAllocatorRef allocator, CFFileDescriptorNativeDescriptor fd, Boolean closeOnInvalidate, CFFileDescriptorCallBack callout, const CFFileDescriptorContext *context) __attribute__((weak_import));
extern CFFileDescriptorNativeDescriptor CFFileDescriptorGetNativeDescriptor(CFFileDescriptorRef f) __attribute__((weak_import));
extern void CFFileDescriptorGetContext(CFFileDescriptorRef f, CFFileDescriptorContext *context) __attribute__((weak_import));
extern void CFFileDescriptorEnableCallBacks(CFFileDescriptorRef f, CFOptionFlags callBackTypes) __attribute__((weak_import));
extern void CFFileDescriptorDisableCallBacks(CFFileDescriptorRef f, CFOptionFlags callBackTypes) __attribute__((weak_import));
extern void CFFileDescriptorInvalidate(CFFileDescriptorRef f) __attribute__((weak_import));
extern Boolean CFFileDescriptorIsValid(CFFileDescriptorRef f) __attribute__((weak_import));
extern CFRunLoopSourceRef CFFileDescriptorCreateRunLoopSource(CFAllocatorRef allocator, CFFileDescriptorRef f, CFIndex order) __attribute__((weak_import));

#ifdef __cplusplus
}
#endif

#endif /* _CONFIGD_CFFILEDESCRIPTOR_WEAK_COMPAT_H_ */
