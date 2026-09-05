/* Real, honestly-documented compat stand-in for two real Apple typedefs,
 * user64_addr_t/user32_addr_t, that BridgeConfiguration.c's and
 * VLANConfiguration.c's own real `#define KERNEL_PRIVATE` /
 * `#include <net/if.h>` / `#undef KERNEL_PRIVATE` trick (both files, real
 * unmodified Apple source, confirmed by reading directly) needs in order
 * to parse net/if.h's `#ifdef KERNEL_PRIVATE` block (struct
 * if_clonereq64/if_clonereq32).
 *
 * Real, confirmed root cause: this project's pinned xnu-7195 raw KERNEL
 * source tree (bsd/net/if.h) is the actual kernel-internal header, not
 * real Apple's separately-generated, installed public/private SDK copy
 * (confirmed by diffing against third_party/xnu-7195/BUILD.hdrs/dst/usr/
 * include/net/if.h -- real Apple's own installhdrs step strips this
 * whole struct out of the installed userland header entirely, so on a
 * real Mac SDK this code path is never reached at all). In the raw
 * kernel tree, user64_addr_t/user32_addr_t (bsd/arm/types.h) are
 * themselves only declared `#ifdef KERNEL` -- real Apple's installed SDK
 * copy of arm/types.h has no such restriction (these are always-visible
 * userland-safe typedefs there), so this KERNEL guard is an artifact of
 * consuming the raw source tree, not a real userland/kernel API
 * boundary.
 *
 * Real, load-bearing fact confirmed by reading both files directly (grep,
 * not assumed): NEITHER BridgeConfiguration.c NOR VLANConfiguration.c
 * ever references if_clonereq64/if_clonereq32/user64_addr_t/
 * user32_addr_t itself -- KERNEL_PRIVATE is defined only to unlock OTHER,
 * unrelated real content further down in net/if.h (extended interface
 * flags, if_bridge/if_vlan-adjacent definitions). This compat header only
 * needs to make the two typedefs exist syntactically; exact ABI fidelity
 * is not load-bearing here since no code in this build's target files
 * ever reads or writes a value of either type. DAR-240.
 */
#ifndef _CONFIGD_SCNET_BRIDGEVAR_TYPES_COMPAT_H_
#define _CONFIGD_SCNET_BRIDGEVAR_TYPES_COMPAT_H_

#include <sys/_types.h>	/* real __uint64_t/__uint32_t */

#ifndef KERNEL
typedef __uint64_t user64_addr_t;
typedef __uint32_t user32_addr_t;
#endif

#endif /* _CONFIGD_SCNET_BRIDGEVAR_TYPES_COMPAT_H_ */
