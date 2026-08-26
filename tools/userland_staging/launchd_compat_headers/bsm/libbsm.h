/* Redirect to the REAL, unmodified apple-oss-distributions BSM repo's
 * own libbsm.h (third_party/BSM/bsm/lib/libbsm.h, tag bsm-13, already
 * pinned in this project) instead of this project's pinned iPhoneOS14.4
 * SDK's own shipped <bsm/libbsm.h> -- the SDK's copy marks
 * audit_token_to_au32() API_UNAVAILABLE(ios,...) at the resolved-symbol
 * level (a real, hard clang error that neither `__asm` symbol-renaming
 * nor C-level identifier-renaming can bypass, confirmed by real trial --
 * clang's availability-attribute merging unions across every
 * declaration of a name touched by the same macro rename). The real
 * upstream BSM repo's own header has no such restriction at all (gated
 * only on `__APPLE_API_PRIVATE`, which this project defines below) --
 * this redirect exists only because the real repo's own directory
 * layout (`bsm/lib/libbsm.h`, with an extra `lib/` path component) does
 * not match the `#include <bsm/libbsm.h>` convention every real caller
 * in this project's source (xnu, PureDarwin) actually uses.
 */
#define __APPLE_API_PRIVATE 1
/* real value: this project's pinned SDK's own <sys/param.h> defines
 * NGROUPS (max supplemental group ids) as 16 -- the real upstream BSM
 * header assumes a MAX_GROUPS macro is already defined by whatever
 * platform header normally precedes it on real Darwin, which isn't
 * true of this project's own include chain reaching this redirect. */
#include <sys/param.h>
#ifndef MAX_GROUPS
#define MAX_GROUPS NGROUPS
#endif
#include "../../../../third_party/BSM/bsm/lib/libbsm.h"
