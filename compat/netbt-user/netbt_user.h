/*
 * netbt_user.h -- force-included (-include) into NetBSD's Bluetooth userland
 * (lib/libbluetooth, btconfig, sdpquery, ... from anurodhp/netbsd-src
 * iokit-port) when built for this port by build_libbluetooth.sh and
 * build_netbt_tools.sh. docs/bluetooth-stack-plan.md S3.
 */
#ifndef _NETBT_USER_H_
#define _NETBT_USER_H_

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/uio.h>

/* The kernel's value (xnu fork bsd/sys/socket.h: AF_BLUETOOTH 41); the
 * iPhoneOS SDK's sys/socket.h has none. */
#ifndef AF_BLUETOOTH
#define AF_BLUETOOTH    41
#define PF_BLUETOOTH    AF_BLUETOOTH
#endif

/* NetBSD sys/cdefs.h spellings. */
#define __RCSID(s)
#define __COPYRIGHT(s)
#define __KERNEL_RCSID(n, s)
#ifndef __UNCONST
#define __UNCONST(a)            ((void *)(uintptr_t)(const void *)(a))
#endif
#define __arraycount(a)         (sizeof(a) / sizeof((a)[0]))
#define __packed                __attribute__((__packed__))
#ifndef __aligned
#define __aligned(x)            __attribute__((__aligned__(x)))
#endif
#define __dead                  __dead2
#define __predict_false(x)      __builtin_expect(!!(x), 0)
#define __predict_true(x)       __builtin_expect(!!(x), 1)
/* NetBSD __RENAME(x): the C symbol is x (sys/cdefs.h, __asm label);
 * Mach-O C symbols carry a leading underscore. */
#ifndef __RENAME
#define __RENAME(x)             __asm("_" #x)
#endif
#ifndef __type_fit
#define __type_fit(t, a)        1
#endif

/* NetBSD sys/cdefs.h bit-field macros. */
#define __BIT(n)                (((uintmax_t)1) << (n))
#define __BITS(m, n)            ((__BIT((m) > (n) ? (m) : (n)) << 1) - __BIT((m) < (n) ? (m) : (n)))
#define __LOWEST_SET_BIT(mask)  ((((mask) - 1) & (mask)) ^ (mask))
#define __SHIFTOUT(x, mask)     (((x) & (mask)) / __LOWEST_SET_BIT(mask))
#define __SHIFTIN(x, mask)      ((x) * __LOWEST_SET_BIT(mask))
#define ___STRING(x)            __STRING(x)

/* NetBSD compile-time and libc assertions (sys/cdefs.h, assert.h). */
#ifndef __CTASSERT
#define __CTASSERT(x)           _Static_assert((x), #x)
#endif
#ifndef _DIAGASSERT
#define _DIAGASSERT(e)          ((void)0)
#endif

/* NetBSD <poll.h>: wait forever. */
#ifndef INFTIM
#define INFTIM                  (-1)
#endif

/* humanize_number(3): NetBSD declares it in <util.h>; Apple's is libutil
 * (third_party/libutil/humanize_number.c, libutil.h:62-94), built here. */
int humanize_number(char *, size_t, int64_t, const char *, int, int);
#define HN_DECIMAL              0x01
#define HN_NOSPACE              0x02
#define HN_B                    0x04
#define HN_DIVISOR_1000         0x08
#define HN_IEC_PREFIXES         0x10
#define HN_GETSCALE             0x10
#define HN_AUTOSCALE            0x20

/*
 * reallocarr(3), NetBSD libc (lib/libc/stdlib/reallocarr.c): resize the
 * array *ptrp to n * size bytes; returns 0 or an errno value (EOVERFLOW,
 * ENOMEM) and leaves *ptrp untouched on failure; n * size == 0 frees it
 * and stores NULL.
 */
#include <errno.h>
#include <stdlib.h>
static __inline int
reallocarr(void *ptrp, size_t n, size_t size)
{
	void **pp = (void **)ptrp, *p;

	if (n == 0 || size == 0) {
		free(*pp);
		*pp = NULL;
		return 0;
	}
	if (n > SIZE_MAX / size)
		return EOVERFLOW;
	if ((p = realloc(*pp, n * size)) == NULL)
		return ENOMEM;
	*pp = p;
	return 0;
}

/* NetBSD libutil pidfile(3) (libc_shim.c). */
int pidfile(const char *);

/* NetBSD libutil snprintb(3), old-style formats only (snprintb.c here). */
int snprintb(char *, size_t, const char *, uint64_t);

#endif /* _NETBT_USER_H_ */
