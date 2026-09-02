/* Real, project-owned trigger for CFRuntime.c's own real __CFInitialize().
 *
 * REAL BUG FOUND 2026-09-02 while verifying DAR-195's dylib conversion
 * (the ticket's own mandate: "verify CF's constructors/global state
 * actually reinitialize correctly per-process... not just compiles
 * clean" -- this is exactly what that verification found).
 *
 * third_party/swift-corelibs-foundation/CoreFoundation/Base.subproj/
 * CFRuntime.c:1121 declares:
 *   #if TARGET_OS_LINUX || TARGET_OS_BSD
 *   static void __CFInitialize(void) __attribute__ ((constructor));
 *   static
 *   #endif
 *   #if TARGET_OS_WIN32
 *   CF_EXPORT
 *   #endif
 *   void __CFInitialize(void) { ... }
 *
 * Confirmed via a real preprocessor probe against this project's exact
 * build flags (arm64-apple-ios14.4, DEPLOYMENT_TARGET_MACOSX=0): on this
 * target TARGET_OS_MAC=1, TARGET_OS_LINUX=0, TARGET_OS_BSD=0,
 * TARGET_OS_WIN32=0 -- so NEITHER guarded branch applies.
 * __CFInitialize is a plain, external, non-constructor, non-static
 * function on this build, and its only other real call site in the
 * entire checkout (grepped across every .c file) is inside
 * CFRuntime.c's own DllMain(), itself TARGET_OS_WIN32-only. On real
 * TARGET_OS_MAC, this file's own real, unmodified upstream source
 * NEVER CALLS __CFInitialize() AT ALL.
 *
 * This is not a dylib-specific bug -- it was equally true of the old
 * static-archive build (build_corefoundation.sh), just never noticed:
 * CF's own real init routine (TSD setup, __CFCharacterSetInitialize,
 * __CFDateInitialize, __CFCharacterSetInitialize, the pthread_atfork
 * registration, __CFArgStuff/_CFProcessPath caching, CFNumberGetTypeID's
 * own "NB: this does other work" side effects) has never actually run in
 * this project, in either build mode. Existing consumers (launchd/
 * launchctl) have apparently gotten away with it so far only because
 * nothing on their exercised code paths depends on those specific side
 * effects yet -- not because the bug wasn't real.
 *
 * WHY THE GUARD LOOKS THE WAY IT DOES (read, not guessed): this whole
 * file is swift-corelibs-foundation's own portable REIMPLEMENTATION of
 * CoreFoundation, normally used only on platforms with no real Apple
 * CoreFoundation.framework to link against (Linux/BSD/Windows) -- real
 * Darwin builds of Swift just link the real, closed-source system
 * framework instead, which has its own real constructor this file never
 * needed to duplicate. This project is a structurally different case: it
 * compiles this exact reimplementation AS its whole CoreFoundation on a
 * Darwin target (TARGET_OS_MAC=1), because there is no closed-source
 * system CoreFoundation.framework to fall back to here. The upstream
 * guard's assumption ("TARGET_OS_MAC means a real system CF already
 * exists, so this file doesn't need to self-initialize") is simply false
 * in this project's context.
 *
 * FIX CHOICE: a NEW, project-owned constructor-attributed wrapper here,
 * not a patch to CFRuntime.c itself. Same real bug CLASS this project's
 * swift-corelibs-foundation.patch already fixed twice (__CFTSDInitialize
 * TARGET_OS_OSX/TARGET_OS_MAC mismatch, CFPreferences.c's own guard
 * mismatch -- "these two guards should agree"), but a *different* fix
 * shape is correct here: this project already has a real, recent,
 * directly-on-point precedent for NOT touching upstream source when the
 * gap is "real Darwin needs an alias/trigger the guard assumes is
 * unnecessary" -- CFLocaleKeys.c's own legacy short-form aliases hit the
 * identical TARGET_OS_MAC-guard-too-narrow shape (commit 867cccb in this
 * checkout) and were reverted (commit 9381a2e) in favor of real
 * `-Wl,-alias` link flags instead of a source patch, once it became
 * clear the narrower, non-invasive fix was just as correct. There is no
 * link-time equivalent of "run this function automatically at load
 * time" for a plain (non-aliased) missing constructor attribute, so the
 * narrowest non-invasive equivalent here is this one small, separately
 * compiled, real constructor -- __CFInitialize() itself is plain
 * external (non-static) linkage on this TARGET_OS_MAC build (confirmed
 * via the guard read above), so it's a legitimate real symbol for
 * another translation unit to call directly, no forward-declaration
 * trickery needed.
 */

extern void __CFInitialize(void);

/* Real, project-owned, deliberately-exported marker -- NOT reaching into
 * CFRuntime.c's own `__CFInitialized` (declared `CF_PRIVATE` in
 * CFInternal.h, i.e. real Apple hidden-visibility/private-extern by
 * design, matching real Apple's own CF encapsulation -- confirmed via a
 * real `nm -m` showing "private external" on that exact symbol, not
 * forced public here). This flag instead proves the one thing this
 * project's own code is responsible for: that
 * __iokit_CFInitialize_trigger below really ran as a real constructor
 * and really called through to __CFInitialize(). Set AFTER the call
 * returns, so a crash inside __CFInitialize() itself would leave this at
 * 0 rather than falsely reading 1.
 */
int __iokit_cf_init_constructor_ran = 0;

__attribute__((constructor))
static void __iokit_CFInitialize_trigger(void) {
    __CFInitialize();
    __iokit_cf_init_constructor_ran = 1;
}
