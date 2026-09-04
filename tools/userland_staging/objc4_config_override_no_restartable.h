/* Real, narrow, single-file override -- -include'd ONLY when compiling
 * objc-cache.mm (see build_objc4.sh's per-file flags). Byte-for-byte
 * copy of the real pinned objc-config.h EXCEPT HAVE_TASK_RESTARTABLE_
 * RANGES, forced to 0 here.
 *
 * Real root cause: objc-cache.mm's cache_t::init() calls the real Mach
 * MIG routine task_restartable_ranges_register()/_synchronize()
 * (real, genuinely implemented kernel-side in this project's own pinned
 * third_party/xnu-7195/osfmk/kern/restartable.c) -- but this project
 * has never regenerated a real MIG client stub for
 * osfmk/mach/restartable.defs anywhere (confirmed via grep across every
 * already-built .a/.dylib). A real MIG client port for this subsystem
 * is legitimate, real, future work (not fabricated-away), but is not
 * needed to prove the runtime's basic message-dispatch smoketest works
 * -- HAVE_TASK_RESTARTABLE_RANGES=0 selects real Apple's OWN documented
 * fallback (objc-cache.mm's `_collecting_in_critical()` thread-suspend-
 * and-inspect path, used on every real platform/config where restartable
 * ranges aren't registered), not a fabricated behavior.
 *
 * Real Apple's own upstream condition (preserved in a comment for
 * diff-ability): `#if TARGET_OS_SIMULATOR || defined(__i386__) ||
 * defined(__arm__) || !TARGET_OS_MAC` => 0, else => 1. On our real
 * TARGET_OS_IOS arm64 target this evaluates to 1 (the value we are
 * deliberately overriding, pending a real restartable.defs MIG port).
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

#if TARGET_OS_WIN32
#   define SUPPORT_PREOPT 0
#else
#   define SUPPORT_PREOPT 1
#endif

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

/* REAL DAR-236 OVERRIDE (see this file's own header comment above):
 * forced to 0 pending a real restartable.defs MIG client port. */
#define HAVE_TASK_RESTARTABLE_RANGES 0

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
