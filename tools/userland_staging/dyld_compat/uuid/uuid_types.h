/* Real Apple newer-SDK split: uuid_t/uuid_string_t used to live only in
 * <uuid/uuid.h> (this project's pinned iOS 14.4 SDK's real, unmodified
 * version); a later SDK split them into this separate header. This
 * project's Libc/uuid/uuidsrc/uuidP.h (real source) expects the newer
 * split -- forwarding to the real, already-real uuid.h is correct
 * either way since the typedefs are identical. */
#include <uuid/uuid.h>
