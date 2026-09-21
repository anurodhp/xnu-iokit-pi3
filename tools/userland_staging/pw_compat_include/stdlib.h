/* pw(8) build-wide augmentation of <stdlib.h> (DAR-341).
 *
 * system() -- this project's own pinned Apple Libc/include/stdlib.h
 * declares system() under `__IOS_PROHIBITED` (an App Store policy
 * annotation, not a genuine capability gap -- see
 * pw_real_system_compat.c's own header for the full citation). Clang
 * merges availability attributes across ALL redeclarations of a symbol
 * in a translation unit, so simply providing our own clean prototype
 * first does not clear it -- the fix instead renames the identifier
 * itself: `system` becomes `pw_real_system` for this build's ENTIRE
 * translation units (this #define stays active, deliberately not
 * `#undef`'d after #include_next below), so BOTH the real SDK's own
 * (now-renamed) declaration AND pw_user.c's own two real, unmodified
 * `system(tmp)`/`system(file)` call sites resolve to the same real,
 * un-annotated function this project provides
 * (pw_real_system_compat.c). Confirmed safe via a real, exhaustive grep
 * of every usr.sbin/pw/*.c file for the bare word "system" -- these two
 * call sites are the only ones (see pw_real_system_compat.c's header).
 *
 * reallocarray() -- not declared anywhere in this project's pinned Apple
 * Libc/iPhoneOS SDK headers (confirmed via grep), needed by
 * pw_pw_util_freebsd.c/pw_gr_util_freebsd.c/login_cap.c's own real
 * pw_copy()/gr_copy()/allocarray() bodies. Real implementation:
 * pw_reallocarray_compat.c.
 *
 * The rename above is NOT sufficient by itself: Clang unions
 * availability attributes across every redeclaration of one symbol in a
 * translation unit, so the (renamed) declaration coming out of the real
 * stdlib.h still carries `__IOS_PROHIBITED`, and the call sites (also
 * renamed to the same identifier) still error. `__IOS_PROHIBITED`
 * (Availability.h) expands to a real `availability(ios, unavailable)`
 * attribute -- an App Store submission-policy annotation for iOS
 * targets specifically, confirmed via direct read of Availability.h,
 * not a stand-in for a genuine capability gap.
 *
 * Neutralized here PERMANENTLY for the rest of this translation unit
 * (tried push_macro/pop_macro first -- scoped to just this
 * #include_next, restored right after -- but that silently broke every
 * LATER `__IOS_PROHIBITED` reference: Apple's own Availability.h is only
 * ever processed once per TU (its own #ifndef guard), and in these
 * particular translation units nothing earlier had pulled it in yet, so
 * pop_macro was restoring "never defined" instead of the real
 * definition -- confirmed by the exact real failure this caused,
 * third_party/Libc/include/time.h:240: "unknown type name
 * '__IOS_PROHIBITED'"). A permanent neutralization is also the
 * philosophically correct choice here, not just the expedient one: this
 * project's whole reason for using an iOS-vintage SDK is Darwin 20.3
 * header/struct-layout parity (see CLAUDE.md), never an actual App
 * Store submission -- this target is real hardware/QEMU, so the
 * restriction never legitimately applies to anything this build
 * compiles. (The declaration's other attributes --
 * `__swift_unavailable_on`, `__WATCHOS_PROHIBITED`, `__TVOS_PROHIBITED`
 * -- are harmless here: the first is a real Swift-only availability
 * attribute Clang's C frontend does not enforce on C callers (confirmed
 * via direct read of its own `__swift_unavailable` definition,
 * `availability(swift, unavailable, ...)`), and the latter two target
 * watchOS/tvOS, not this build's real `arm64-apple-ios14.4` target.)
 */
#ifndef _PW_STDLIB_AUGMENT_H_
#define _PW_STDLIB_AUGMENT_H_

#define system pw_real_system

#undef __IOS_PROHIBITED
#define __IOS_PROHIBITED
#include_next <stdlib.h>

#include <stddef.h>
extern void *reallocarray(void *, size_t, size_t);

#endif
