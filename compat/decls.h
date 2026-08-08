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
 * {storage,network}, pulled from a newer apple-oss-distributions release
 * than this project's pinned 2019-era xnu-6153.11.26 checkout) use
 * `__exported` directly on class declarations, e.g.
 * `class __exported IOBlockStorageDevice : public IOService`. This macro
 * normally comes from sys/cdefs.h, but xnu's own bsd/sys/cdefs.h (an
 * older vintage, found first via -Ithird_party/xnu/bsd) doesn't define it
 * yet - the SDK's newer cdefs.h does, but never gets reached since xnu's
 * copy wins the -I search first. Defining it directly here sidesteps the
 * header-resolution order problem entirely.
 */
#ifndef __exported
#define __exported __attribute__((__visibility__("default")))
#endif

#include <sys/types.h>
extern void bzero(void *, size_t);
extern void bcopy(const void *, void *, size_t);
extern int  bcmp(const void *, const void *, size_t);

#endif
