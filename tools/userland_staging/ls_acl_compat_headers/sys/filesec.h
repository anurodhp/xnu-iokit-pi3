/* Real Apple <sys/filesec.h>, dropped from the public SDK (confirmed
 * absent from Xcode 12's iPhoneOS/MacOSX SDKs directly) but genuinely
 * reconstructable, not guessed: the opaque `filesec_t` object it
 * declares is defined entirely privately inside the real, unmodified
 * Libc/gen/filesec.c this project already vendors (struct _filesec,
 * never exposed to callers) -- this header only needs to supply the
 * small public surface that file's own functions are declared against:
 * the typedef, the filesec_property_t enum (matching filesec.c's own
 * FILESEC_* switch cases exactly), and the two sentinel values
 * filesec_set_property()/filesec_query_property() compare by pointer
 * identity, never dereference (DAR-148).
 *
 * The real kernel-side ABI (struct kauth_filesec, KAUTH_FILESEC_SIZE
 * etc) that Libc/sys/statx_np.c's real statx1() drives the raw
 * stat64_extended/lstat64_extended/fstat64_extended syscalls with is
 * NOT missing -- Xcode 12's iPhoneOS SDK ships a real, complete public
 * <sys/kauth.h> (confirmed via grep) with the exact struct this
 * project's own xnu-7195 kernel fills in via vn_stat(..., &fsec, ...).
 * Only this small userland wrapper was ever actually gone.
 */
#ifndef _SYS_FILESEC_H_
#define _SYS_FILESEC_H_

#include <sys/types.h>

typedef struct _filesec *filesec_t;

typedef enum {
	FILESEC_OWNER = 1,
	FILESEC_GROUP = 2,
	FILESEC_UUID = 3,
	FILESEC_GRPUUID = 4,
	FILESEC_ACL = 5,
	FILESEC_ACL_RAW = 6,
	FILESEC_ACL_ALLOCSIZE = 7,
	FILESEC_MODE = 8,
} filesec_property_t;

/* Sentinel pointers, compared by identity only (never dereferenced) --
 * see filesec.c's own filesec_set_property()/filesec_query_property().
 * NULL is safe as "unset": every real caller passes the address of a
 * real stack variable when actually setting a value, never NULL. */
#define _FILESEC_UNSET_PROPERTY        ((void *)0)
#define _FILESEC_REMOVE_ACL            ((void *)1)

__BEGIN_DECLS
filesec_t       filesec_init(void);
filesec_t       filesec_dup(filesec_t _fsec);
void            filesec_free(filesec_t _fsec);
int             filesec_get_property(filesec_t _fsec, filesec_property_t _property, void *_value);
int             filesec_query_property(filesec_t _fsec, filesec_property_t _property, int *_value);
int             filesec_set_property(filesec_t _fsec, filesec_property_t _property, const void *_value);
int             filesec_unset_property(filesec_t _fsec, filesec_property_t _property);
__END_DECLS

#endif /* _SYS_FILESEC_H_ */
