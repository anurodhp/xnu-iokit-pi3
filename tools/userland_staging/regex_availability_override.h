/* Real gap, real narrow fix: this project's whole userland_staging build
 * targets `-target arm64-apple-ios14.4` uniformly (Xcode-12's SDK
 * vintage, same reason every other build_*.sh in this directory uses
 * it -- NOT because this board runs iOS, just the SDK/toolchain that
 * happens to line up with xnu-7195's Darwin 20.3 era, see
 * build_libc_locale.sh's own header comment). Apple's real
 * xlocale/_regex.h marks the xlocale-suffixed regex entry points
 * (regcomp_l/regncomp_l/regwcomp_l/regwnexec_l) `__OSX_AVAILABLE_STARTING
 * (__MAC_10_8, __IPHONE_NA)` -- "never available on iOS" -- which is a
 * real Apple App-Store-review-time policy annotation (these are
 * macOS-only public API on real Apple platforms), not a real functional
 * restriction: the underlying code (third_party/Libc/regex/TRE/lib/
 * regcomp.c, via tre.h's TRE_USE_SYSTEM_REGEX_H rename of
 * tre_regncomp_l -> regncomp_l et al) is portable C with zero iOS-vs-
 * macOS runtime difference. Defining these symbols under an iOS SDK
 * target trips a real, but purely-lint-shaped, "unavailable" compile
 * error the moment they're DEFINED (not just declared) -- confirmed via
 * the exact diagnostic: "'regncomp_l' is unavailable: not available on
 * iOS".
 *
 * Fix: neutralize just the __IPHONE_NA sentinel's expansion
 * (__AVAILABILITY_INTERNAL__IPHONE_NA, defined in the SDK's own
 * AvailabilityInternal.h as
 * __attribute__((availability(ios,unavailable)))) to a no-op, by
 * force-#include'ing AvailabilityInternal.h ourselves first (so its real
 * header-guard is already tripped before any other SDK header gets to
 * it) and then redefining that one macro. This is scoped to exactly the
 * `__IPHONE_NA` sentinel -- every other real availability annotation in
 * every other header stays fully intact for this compile unit. Force-
 * included (-include) ahead of any TRE/Libc source in
 * build_libc_regex.sh's CFLAGS, same general "compat shim" pattern
 * build_libc_locale.sh's compat_include/ stubs already use for other
 * genuine SDK-vintage mismatches (os/assumes.h, mach-o/dyld_priv.h).
 */
#ifndef _REGEX_AVAILABILITY_OVERRIDE_H_
#define _REGEX_AVAILABILITY_OVERRIDE_H_

#include <AvailabilityInternal.h>

#ifdef __AVAILABILITY_INTERNAL__IPHONE_NA
#undef __AVAILABILITY_INTERNAL__IPHONE_NA
#endif
#define __AVAILABILITY_INTERNAL__IPHONE_NA /* nothing -- see header comment */

#endif /* _REGEX_AVAILABILITY_OVERRIDE_H_ */
