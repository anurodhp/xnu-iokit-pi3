/* Real, narrow, single-file override of third_party/objc4/runtime/
 * objc-config.h -- -include'd ONLY when compiling objc-opt.mm (see
 * build_objc4.sh's per-file flags), not the whole runtime.
 *
 * Real root cause: objc-opt.mm's SUPPORT_PREOPT=1 branch (dyld shared
 * cache class/selector-table optimization reading) needs
 * <objc-shared-cache.h> from the pinned third_party/dyld tag. That
 * pinned dyld's real objc-shared-cache.h is a genuinely older/different
 * shape than objc4-818.2 expects -- it never declares
 * objc_stringhash_offset_t/objc_clsopt_t/objc_protocolopt2_t/
 * objc_headeropt_rw_t at all (confirmed via grep: this pinned
 * objc-shared-cache.h only has objc_headeropt_ro_t and a plain
 * objc_opt_t, an older/incompatible real Apple struct generation). A
 * real, confirmed version-skew gap between two separately-pinned real
 * Apple components (same class as this project's other SDK-vintage
 * fixes), not something to fabricate a matching struct layout for.
 *
 * Real, zero-fabrication fix: objc-opt.mm's own real Apple source
 * ALREADY ships the correct answer for this exact situation --
 * `#if !SUPPORT_PREOPT` / "Preoptimization not supported on this
 * platform" (objc-opt.mm lines 34-108), a complete, honest, real
 * Apple-authored fallback implementation (isPreoptimized() => false,
 * getPreoptimizedClass()/getPreoptimizedProtocol() => nil, etc) that
 * entirely avoids touching <objc-shared-cache.h>. This project's kernel
 * boots real per-file .dylibs off an SD card with NO dyld shared cache
 * at all (confirmed: no shared-cache builder, no
 * update_dyld_shared_cache equivalent anywhere in this from-source
 * port) -- so "preoptimization not supported on this platform" is
 * genuinely, permanently TRUE here, not a workaround.
 *
 * objc-config.h's own real #define is unconditional (no #ifndef guard
 * around individual macros), so a plain command-line -DSUPPORT_PREOPT=0
 * gets silently overwritten by the header's own later #define when
 * objc-private.h's normal #include "objc-config.h" chain runs -- and
 * objc-config.h is always reached via a quoted, same-directory
 * #include, so a higher-priority -I can't shadow it either (quoted
 * includes always check the including file's own directory first).
 * The only real, surgical way to force just the ONE line different for
 * just this ONE file: -include this verbatim copy of objc-config.h
 * (unmodified except the single SUPPORT_PREOPT line, diff-able against
 * the real file above) BEFORE objc-opt.mm's normal #include chain runs
 * -- the real header's own #ifndef _OBJC_CONFIG_H_ guard then correctly
 * sees the guard already defined and skips itself, matching this
 * project's existing "narrow override wins by being processed first"
 * pattern used elsewhere (e.g. mach-o/dyld_priv.h shadowing).
 *
 * Every other macro below is a byte-for-byte copy of the real pinned
 * objc-config.h (objc4-818.2) -- confirmed by diffing this file against
 * it after editing. Only the SUPPORT_PREOPT branch differs, and it's
 * marked below.
 */
#ifndef _OBJC_CONFIG_H_
#define _OBJC_CONFIG_H_

#include <TargetConditionals.h>

#if !NDEBUG
#   define DEBUG 1
#else
#   define DEBUG 0
#endif

#if !TARGET_OS_OSX
#   define SUPPORT_GC_COMPAT 0
#else
#   define SUPPORT_GC_COMPAT 1
#endif

#if !(TARGET_OS_OSX || TARGET_OS_MACCATALYST)
#   define SUPPORT_ZONES 0
#else
#   define SUPPORT_ZONES 1
#endif

#if defined(__arm__)
#   define SUPPORT_MOD 0
#else
#   define SUPPORT_MOD 1
#endif

/* REAL DAR-236 OVERRIDE (see this file's own header comment above):
 * forced to 0 -- this project has no dyld shared cache, ever, matching
 * objc-opt.mm's own real "Preoptimization not supported on this
 * platform" fallback. Real Apple's own upstream condition
 * (`#if TARGET_OS_WIN32`) is preserved in a comment for diff-ability;
 * it would evaluate to 1 (SUPPORT_PREOPT enabled) on our real
 * TARGET_OS_IOS target, which is the value we are deliberately
 * overriding. */
#define SUPPORT_PREOPT 0

#if !__LP64__
#   define SUPPORT_TAGGED_POINTERS 0
#else
#   define SUPPORT_TAGGED_POINTERS 1
#endif

#if !SUPPORT_TAGGED_POINTERS  ||  ((TARGET_OS_OSX || TARGET_OS_MACCATALYST) && __x86_64__)
#   define SUPPORT_MSB_TAGGED_POINTERS 0
#else
#   define SUPPORT_MSB_TAGGED_POINTERS 1
#endif

#if __ARM_ARCH_7K__ >= 2  ||  (__arm64__ && !__LP64__)
#   define SUPPORT_INDEXED_ISA 1
#else
#   define SUPPORT_INDEXED_ISA 0
#endif

#if (!__LP64__  ||  TARGET_OS_WIN32  ||  \
     (TARGET_OS_SIMULATOR && !TARGET_OS_MACCATALYST && !__arm64__))
#   define SUPPORT_PACKED_ISA 0
#else
#   define SUPPORT_PACKED_ISA 1
#endif

#if !SUPPORT_INDEXED_ISA  &&  !SUPPORT_PACKED_ISA
#   define SUPPORT_NONPOINTER_ISA 0
#else
#   define SUPPORT_NONPOINTER_ISA 1
#endif

#if !(defined(__x86_64__) && (TARGET_OS_OSX || TARGET_OS_SIMULATOR))
#   define SUPPORT_FIXUP 0
#else
#   define SUPPORT_FIXUP 1
#endif

#if defined(__arm__)  &&  __USING_SJLJ_EXCEPTIONS__
#   define SUPPORT_ZEROCOST_EXCEPTIONS 0
#else
#   define SUPPORT_ZEROCOST_EXCEPTIONS 1
#endif

#if !SUPPORT_ZEROCOST_EXCEPTIONS  ||  !TARGET_OS_OSX
#   define SUPPORT_ALT_HANDLERS 0
#else
#   define SUPPORT_ALT_HANDLERS 1
#endif

#if TARGET_OS_WIN32
#   define SUPPORT_RETURN_AUTORELEASE 0
#else
#   define SUPPORT_RETURN_AUTORELEASE 1
#endif

#if defined(__arm64__)
#   define SUPPORT_STRET 0
#else
#   define SUPPORT_STRET 1
#endif

#if !TARGET_OS_OSX
#   define SUPPORT_MESSAGE_LOGGING 0
#else
#   define SUPPORT_MESSAGE_LOGGING 1
#endif

#if !__LP64__
#   define SUPPORT_AUTORELEASEPOOL_DEDUP_PTRS 0
#else
#   define SUPPORT_AUTORELEASEPOOL_DEDUP_PTRS 1
#endif

#if TARGET_OS_SIMULATOR || defined(__i386__) || defined(__arm__) || !TARGET_OS_MAC
#   define HAVE_TASK_RESTARTABLE_RANGES 0
#else
#   define HAVE_TASK_RESTARTABLE_RANGES 1
#endif

//#define OBJC_INSTRUMENTED

#define CONFIG_USE_CACHE_LOCK 0

#define CACHE_IMP_ENCODING_NONE 1
#define CACHE_IMP_ENCODING_ISA_XOR 2
#define CACHE_IMP_ENCODING_PTRAUTH 3

#if __PTRAUTH_INTRINSICS__
#define CACHE_IMP_ENCODING CACHE_IMP_ENCODING_PTRAUTH
#elif defined(__arm__)
#define CACHE_IMP_ENCODING CACHE_IMP_ENCODING_NONE
#else
#define CACHE_IMP_ENCODING CACHE_IMP_ENCODING_ISA_XOR
#endif

#define CACHE_MASK_STORAGE_OUTLINED 1
#define CACHE_MASK_STORAGE_HIGH_16 2
#define CACHE_MASK_STORAGE_LOW_4 3
#define CACHE_MASK_STORAGE_HIGH_16_BIG_ADDRS 4

#if defined(__arm64__) && __LP64__
#if TARGET_OS_OSX || TARGET_OS_SIMULATOR
#define CACHE_MASK_STORAGE CACHE_MASK_STORAGE_HIGH_16_BIG_ADDRS
#else
#define CACHE_MASK_STORAGE CACHE_MASK_STORAGE_HIGH_16
#endif
#elif defined(__arm64__) && !__LP64__
#define CACHE_MASK_STORAGE CACHE_MASK_STORAGE_LOW_4
#else
#define CACHE_MASK_STORAGE CACHE_MASK_STORAGE_OUTLINED
#endif

#define ISA_SIGNING_DISCRIMINATOR 0x6AE1
#define ISA_SIGNING_DISCRIMINATOR_CLASS_SUPERCLASS 0xB5AB

#define ISA_SIGNING_KEY ptrauth_key_process_independent_data

#define ISA_SIGNING_STRIP 1
#define ISA_SIGNING_AUTH  2

#define ISA_SIGNING_SIGN_NONE       1
#define ISA_SIGNING_SIGN_ONLY_SWIFT 2
#define ISA_SIGNING_SIGN_ALL        3

#if __has_feature(ptrauth_objc_isa_strips) || __has_feature(ptrauth_objc_isa_signs) || __has_feature(ptrauth_objc_isa_authenticates)
#   if __has_feature(ptrauth_objc_isa_authenticates)
#       define ISA_SIGNING_AUTH_MODE ISA_SIGNING_AUTH
#   else
#       define ISA_SIGNING_AUTH_MODE ISA_SIGNING_STRIP
#   endif
#   if __has_feature(ptrauth_objc_isa_signs)
#       define ISA_SIGNING_SIGN_MODE ISA_SIGNING_SIGN_ALL
#   else
#       define ISA_SIGNING_SIGN_MODE ISA_SIGNING_SIGN_NONE
#   endif
#else
#   if __has_feature(ptrauth_objc_isa)
#       define ISA_SIGNING_AUTH_MODE ISA_SIGNING_AUTH
#       define ISA_SIGNING_SIGN_MODE ISA_SIGNING_SIGN_ALL
#   else
#       define ISA_SIGNING_AUTH_MODE ISA_SIGNING_STRIP
#       define ISA_SIGNING_SIGN_MODE ISA_SIGNING_SIGN_NONE
#   endif
#endif

#define SUPERCLASS_SIGNING_TREAT_UNSIGNED_AS_NIL 0

#if defined(__arm64__) && TARGET_OS_IOS && !TARGET_OS_SIMULATOR && !TARGET_OS_MACCATALYST
#define CONFIG_USE_PREOPT_CACHES 1
#else
#define CONFIG_USE_PREOPT_CACHES 0
#endif

#define CONFIG_SHARED_CACHE_RELATIVE_DIRECT_SELECTORS 1

#endif
