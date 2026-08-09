#ifndef IOKIT_KERNEL_LIBC_COMPAT_H
#define IOKIT_KERNEL_LIBC_COMPAT_H
/*
 * Force-included ahead of every kext translation unit (via clang -include,
 * not via a header search path - the Xcode 12 iPhoneOS SDK's own
 * usr/include/string.h does `#include "strings.h"` with a quoted path,
 * which resolves relative to the SDK's own directory before any -I list
 * is ever consulted, so a plain -Icompat override never gets a chance).
 *
 * xnu's own bsd/libkern/libkern.h unconditionally declares
 * ffs/ffsll/fls/flsll with kernel-appropriate (unsigned) signatures and no
 * guard, since real kernel builds never route through the SDK's userspace
 * strings.h at all. Predefining its include guard here makes the SDK's
 * real strings.h a no-op wherever it's pulled in, avoiding the conflict.
 * bzero/bcopy/bcmp, also normally declared there, are re-declared below
 * with their standard signatures instead.
 */
#define _STRINGS_H_

/*
 * IOStorageFamily/IONetworkingFamily headers (third_party/_sdk_shim/IOKit/
 * {storage,network}) use `__exported` directly on class declarations, e.g.
 * `class __exported IOBlockStorageDevice : public IOService`. This macro
 * normally comes from sys/cdefs.h; xnu-7195's own bsd/sys/cdefs.h (found
 * first via -Ithird_party/xnu-7195/bsd) does define it, so this is a
 * no-op against the current tree - kept guarded with #ifndef in case a
 * future header swap regresses to a vintage that doesn't.
 */
#ifndef __exported
#define __exported __attribute__((__visibility__("default")))
#endif

#include <sys/types.h>
extern void bzero(void *, size_t);
extern void bcopy(const void *, void *, size_t);
extern int  bcmp(const void *, const void *, size_t);

/*
 * bsd/sys/kdebug_kernel.h's KERNEL_DEBUG_CONSTANT* macros reference
 * KDEBUG_ENABLE_PPT unconditionally, but it's only defined in the
 * "private" bsd/sys_private/kdebug_private.h, which kdebug_kernel.h never
 * includes itself - a real kernel build's translation units apparently
 * pull that header in some other way before kdebug_kernel.h is reached.
 * Standalone kext compiles never do, so IOKit/IOTimeStamp.h's use of
 * these macros fails to parse without this. Value matches
 * kdebug_private.h's own (marked "obsolete" there, but still referenced).
 */
#ifndef KDEBUG_ENABLE_PPT
#define KDEBUG_ENABLE_PPT (1U << 3)
#endif

#endif
