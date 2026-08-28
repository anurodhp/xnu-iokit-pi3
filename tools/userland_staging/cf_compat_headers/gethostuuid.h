/* Compat shadow for the real iPhoneOS SDK's <gethostuuid.h>: the real
 * declaration there marks gethostuuid() __IPHONE_NA (unavailable on
 * iOS) -- a real Apple userland-API-surface restriction for iOS apps,
 * not a real functional gap: this project's own kernel
 * (third_party/xnu-7195/bsd/kern/sys_generic.c's real gethostuuid(),
 * BSD syscall #142, NO_SYSCALL_STUB) genuinely implements it, and this
 * project's target (BCM2837/Darwin, built against the iPhoneOS SDK only
 * for toolchain-vintage reasons per CLAUDE.md, not an actual iOS device)
 * has no App Store policy reason to block it. Real signature copied
 * verbatim from the real SDK header, minus the unavailable annotation --
 * same "compat_include forwarding shim" pattern as this directory's own
 * mach/mach_vm.h. This project has no real userland wrapper for syscall
 * #142 yet (NO_SYSCALL_STUB, needs a hand-written raw-trap wrapper, same
 * shape as getpeereid's) -- CFPreferences.c's own real, honest -1/NULL
 * fallback path handles that absence until one is written; this header
 * only unblocks the compile, it doesn't fabricate a working call. */
#ifndef _GETHOSTUUID_H_
#define _GETHOSTUUID_H_

#include <sys/types.h>
#include <sys/_types/_uuid_t.h>
#include <time.h>

__BEGIN_DECLS
int gethostuuid(uuid_t, const struct timespec *);
__END_DECLS

#endif /* _GETHOSTUUID_H_ */
