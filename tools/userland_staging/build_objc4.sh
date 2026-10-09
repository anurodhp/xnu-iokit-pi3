#!/bin/bash
# Builds tools/userland_staging/libc_build/libobjc.A.dylib: DAR-236 --
# the FULL real objc4 Objective-C runtime, ported for its own sake (not
# gated on any one consumer's narrow needs). Real, unmodified Apple
# source from third_party/objc4 (tag objc4-818.2, exact macos-112
# manifest match, pinned by the DAR-170/DAR-236 coordinating session,
# commit 6ef00b3).
#
# Also builds objc4_smoketest.macho: a minimal, standalone Objective-C
# program (one @interface/@implementation, one method, +alloc/-init off
# libobjc's own real NSObject.mm root class) proving real message
# dispatch actually works -- the first real Objective-C consumer this
# project has ever run, before attempting anything as complex as real
# configd/IPMonitor.m/QoSMarking (DAR-170's job, not this script's).
#
# ============================================================
# REAL FILE LIST (verified against objc.xcodeproj's own real "objc"
# PBXNativeTarget -- product libobjc.A.dylib -- Sources build phase,
# not guessed or hand-picked for any one consumer)
# ============================================================
# 41 real Sources-phase entries. Of those, this arm64-only build:
#   - Compiles all 35 real .mm/.m files that are NOT per-other-arch
#     assembly (the four objc-msg-{arm,i386,simulator-i386,
#     simulator-x86_64,x86_64}.s siblings are each other's real per-arch
#     twin of the ONE we need, objc-msg-arm64.s -- Messengers.subproj/).
#   - Assembles objc-msg-arm64.s directly. Confirmed EMPIRICALLY (per
#     this ticket's own explicit instruction, not assumed): this real
#     Apple arm64 assembly needs NO portable-C fallback the way Libm's
#     ARM32-only asm did (docs/apple-oss-catalog.md's Libm section) --
#     it's already written for real AArch64 registers/mnemonics and
#     assembles clean with this project's standard Xcode-12 integrated
#     assembler, same as every other real arm64 .s already in this
#     project (Csu/start.s, syscall traps, etc). _objc_msgSend/
#     _objc_msgSendSuper2/_cache_getImp/_objc_msgLookup and friends are
#     all real, confirmed via `nm` on the assembled object.
#   - Excludes dummy-library-mac-i386.c (i386-only, real Apple's own
#     file, genuinely inapplicable to arm64 -- matches this project's
#     own "exclude by real target boundary" precedent, e.g. DAR-170's
#     SCDConsoleUser.c).
# 36 real objects total (35 .mm/.m + 1 .s) archived into
# libobjc4_runtime.a, then -force_load'ed into the real dylib link (a
# static-archive link without -force_load only pulls members that
# resolve an ALREADY-undefined symbol -- with no consumer yet demanding
# anything from objc4, that pulls in nothing at all and produces a
# near-empty dylib; found empirically this session, same real
# precedent build_launchd.sh's own header comment already documents for
# liblaunchd_core.a).
#
# ============================================================
# REAL GAPS FOUND AND CLOSED, one file/header at a time (compile-one-
# file methodology, same as every prior DAR-170 script)
# ============================================================
# - objc-probes.h: real Apple generates this via
#   `dtrace -h -s objc-probes.d`; this project has no DTrace/USDT probe
#   subsystem at all. Honest no-op macro stand-in
#   (objc4_compat_include/objc-probes.h), matching the exact fallback
#   shape objc-os.h itself already defines for TARGET_OS_WIN32.
# - __BRIDGEOS_DEPRECATED/__BRIDGEOS_UNAVAILABLE and
#   __API_UNAVAILABLE_PLATFORM_bridgeos/__API_AVAILABLE_PLATFORM_bridgeos:
#   same real bridgeOS AvailabilityInternal.h vintage gap this project's
#   other scripts already fixed (build_configd.sh precedent) -- this
#   pinned Xcode-12 iPhoneOS14.4 SDK never got bridgeOS added to its
#   internal platform-macro table.
# - <sys/reason.h>, <os/lock_private.h>, <os/base_private.h>,
#   xlocale_private.h's own <timelocal.h>/<System/machine/
#   cpu_capabilities.h>/<os/tsd.h> chain: real, already-pinned headers,
#   just needed correct -I staging (xnu-7195/bsd, libplatform/private,
#   xnu-7195/libkern, Libc/stdtime/FreeBSD, xnu-7195/libsyscall,
#   libc_build/compat_include).
# - <mach-o/dyld_priv.h>: real, already-pinned header at
#   third_party/dyld/include -- NOT the narrower stand-in
#   build_libc_locale.sh's own compat_include copy provides for ITS
#   consumer (this script uses its own -I ordering, doesn't touch that
#   shared file).
# - <System/pthread_machdep.h>: genuinely never carried by this
#   project's pinned libpthread/libplatform (the "direct TSD slot"
#   SPI). objc-os.h's own real TLS_DIRECT_KEY/etc macros are already
#   `#if defined(__PTK_FRAMEWORK_OBJC_KEY0)`-gated for exactly this
#   case -- an intentionally EMPTY compat header
#   (objc4_compat_include/System/pthread_machdep.h) makes that #if
#   false and objc-os.h falls through to its own real plain
#   pthread_getspecific()/pthread_setspecific() path. Not a fabrication,
#   real Apple's own documented fallback.
# - objc-bp-assist.h: quoted #include in objc-os.mm, but this pinned
#   objc4-818.2 snapshot doesn't ship the file, and a full grep across
#   the whole pinned tree confirms NOTHING references any bp_assist
#   symbol -- genuinely vestigial in this snapshot. Empty stand-in.
# - os/feature_private.h (os_feature_enabled_simple), os/variant_private.h
#   (os_variant_has_internal_diagnostics -- objc4-specific copy, NOT an
#   edit to the shared libc_build/compat_include/os/variant_private.h,
#   which build_bootp_dhcp_engine.sh regenerates fresh every run and
#   isn't this script's file to own): never-open-sourced SPI, same
#   honest "safe real default" class this project has used throughout
#   (os_feature_enabled_simple returns its own caller-supplied default,
#   matching real Apple's own documented fallback semantics).
#   os_variant_has_internal_diagnostics's actual BODY comes for free by
#   linking libsystem_notify.dylib, which already real-exports it (and
#   os_fault_with_payload) for its own reasons -- confirmed via nm, not
#   re-implemented here.
# - Block_private.h -> third_party/libclosure (real, already-pinned,
#   the actual BlocksRuntime source this project's libsystem_blocks
#   already builds from). os/linker_set.h -> third_party/Libc/os (real,
#   already-pinned). objc-shared-cache.h's real
#   objc_stringhash_offset_t/objc_clsopt_t/objc_protocolopt2_t/
#   objc_headeropt_rw_t: see SUPPORT_PREOPT note below -- this project
#   never needs to resolve these.
# - dyld_fall_2018_os_versions/dyld_fall_2020_os_versions/
#   dyld_platform_version_{macOS_10_11,macOS_10_12,macOS_10_13,
#   iOS_10_0,tvOS_10_0,watchOS_3_0,bridgeOS_2_0}: real, CONFIRMED version
#   skew -- this pinned dyld's own mach-o/dyld_priv.h documents these
#   "version set" convenience constants in a comment but the real
#   #define bodies live in <dyld/for_dyld_priv.inc>, a file this
#   open-source snapshot doesn't ship at all (confirmed: not present
#   anywhere under third_party/dyld, and dyld's OWN test that exercises
#   them, testing/test-cases/dyld_version_spis.dtest/version_set.c,
#   wouldn't itself build from this tree either). This project's entire
#   userland is arm64-apple-ios14.4 -- iOS, and only iOS -- so real
#   dyld_priv.h's own documented equivalence ("the platform specific
#   ones will only ever return true if the running binary is the
#   platform being tested") means the honest, narrow fix is real,
#   single-platform dyld_build_version_t values for iOS (using real
#   PLATFORM_IOS=2 from mach-o/loader.h) plus real, permanently-false
#   sibling values for the other 4 platforms real objc4 source token-
#   pastes via sdkIsAtLeast(). See objc4_compat_include/
#   objc4_dyld_version_compat.h's own header comment for full detail;
#   -include'd for every file via CXXFLAGS below.
# - SUPPORT_PREOPT (objc-opt.mm ONLY, via objc4_config_override.h,
#   -include'd for this one file): real, confirmed version skew --
#   this pinned dyld's real objc-shared-cache.h is an older/different
#   shape than objc4-818.2 expects (missing objc_stringhash_offset_t/
#   objc_clsopt_t/objc_protocolopt2_t/objc_headeropt_rw_t entirely).
#   objc-opt.mm's OWN real Apple source already ships the correct,
#   honest answer for "no shared cache" -- `#if !SUPPORT_PREOPT`,
#   "Preoptimization not supported on this platform" -- and this
#   project's kernel boots real per-file .dylibs off an SD card with NO
#   dyld shared cache at all, ever, so that's a real, permanent fact
#   here, not a workaround. Forcing it needs a full, byte-for-byte
#   verbatim copy of objc-config.h (objc4_config_override.h) because
#   objc-config.h's own #define is unconditional (no #ifndef per-macro
#   guard) and always reached via a same-directory quoted #include (so
#   -I ordering can't shadow it) -- -include poisons the _OBJC_CONFIG_H_
#   guard before objc-opt.mm's own normal include chain runs. This
#   leaves 3 header_info methods (nlclslist/catlist/catlist2) undefined
#   at link time (they're only defined in objc-opt.mm's SUPPORT_PREOPT=1
#   branch) -- closed by objc4_preopt_fallback_compat.mm, a real,
#   hand-extracted copy of exactly what those methods' own real bodies
#   already reduce to when isPreoptimized() is false (calls straight
#   through to the real, unmodified _getObjc2NonlazyClassList/
#   _getObjc2CategoryList/_getObjc2CategoryList2 from objc-file.mm,
#   already built) -- not a different, fabricated implementation.
#   getSharedCachePreoptimizedProtocol() has the same "only defined in
#   the SUPPORT_PREOPT=1 branch" problem; its one real call site is
#   itself gated behind `headerIsPreoptimized` (always false here), so
#   a real nil stand-in (objc4_misc_compat.c) is provably correct, not
#   a guess.
# - HAVE_TASK_RESTARTABLE_RANGES (objc-cache.mm ONLY, via
#   objc4_config_override_no_restartable.h, same -include-poisoning
#   technique): real gap, DIFFERENT class -- this project's pinned
#   xnu-7195 DOES genuinely implement task_restartable_ranges_register/
#   _synchronize server-side (osfmk/kern/restartable.c, confirmed via
#   grep), but no real MIG client stub for osfmk/mach/restartable.defs
#   has ever been generated anywhere in this project. That's real,
#   legitimate, separately-scoped future work (same class as a fresh
#   library port), not required to prove basic message dispatch works.
#   HAVE_TASK_RESTARTABLE_RANGES=0 selects real Apple's OWN documented
#   fallback in objc-cache.mm's _collecting_in_critical() (thread-
#   suspend-and-inspect via thread_get_state(), used on every real
#   platform/config where restartable ranges aren't registered) -- not
#   a fabricated behavior. That fallback suspends each other thread and
#   reads its PC with thread_get_state(), which libobjc binds from
#   libsystem_kernel's real MIG client (DAR-236; objc4_misc_compat.c used
#   to carry a KERN_FAILURE stand-in).
# - getsectiondata()/getsegmentdata()/getsectdatafromheader_64(): real,
#   public, stable <mach-o/getsect.h> API objc-file.mm/objc-class.mm/
#   objc-block-trampolines.mm call to find each loaded image's
#   __DATA,__objc_classlist/__objc_catlist/etc sections -- genuinely
#   needed (not vestigial), but this project's pinned cctools only ships
#   these as part of a much larger legacy file (libmacho/getsecbyname.c,
#   pulling in RLD/_dyld_lookup_and_bind()/OPENSTEP branches this
#   project has never built any of). A real, from-scratch reimplementation
#   of the same well-documented, public LC_SEGMENT_64 load-command
#   format (objc4_getsect_compat.c) -- same precedent
#   libunwind_dyld_shim.c already established in this exact project for
#   the same class of problem.
# - _Exit()/fls()/flsl()/malloc_zone_*()/backtrace family
#   (objc4_misc_compat.c): real, narrow, honestly-documented stand-ins
#   -- see that file's own per-function header comments. malloc_zone_*
#   forward to this project's real, working single-allocator malloc/
#   calloc (this project has never implemented multi-zone allocation);
#   backtrace family is an honest "no unwinder wired up here, empty
#   result" (same class already established for SCDPlugin.c/SCDOpen.c
#   by build_libsystemconfiguration_client.sh); fls/flsl are real,
#   correct bit-scan implementations (__builtin_clz-based, not
#   approximations).
# - libsystem_m.dylib (real QEMU boot-test finding, not a compile/link-
#   time gap): the real link succeeded and bind_target_audit.sh/
#   gap_audit.sh both reported clean without it -- but libsystem_c.dylib
#   ITSELF carries a real undefined `_asin` reference (confirmed via
#   `nm -u`, a real pre-existing gap in that dylib, not introduced by
#   this script) that dyld-940's chained-fixups eager binding resolves
#   for every loaded image at process-launch time, not just the ones a
#   given target's own code actually calls. Real per-process resolution,
#   not caught by a per-image-only undefined-symbol check (same class
#   CLAUDE.md's bind_target_audit.sh section already documents, just one
#   level deeper -- a transitive dependency's OWN undefined symbol, not
#   this script's target's). Found by a real QEMU boot test
#   (`dyld[16]: symbol not found in flat namespace '_asin'`, Abort trap:
#   6) after gap_audit.sh/bind_target_audit.sh both reported clean --
#   fixed by adding the real, already-built libsystem_m.dylib (already a
#   real sibling dependency in build_configd.sh's own link line) to both
#   real link steps below.
# - libSystem.B.dylib (second real QEMU boot-test finding, executable
#   ONLY, not the dylib): real dyld-940 refuses to run
#   objc4_smoketest.macho at all with a real, distinct error --
#   `dyld[13]: program does not link with libSystem.B.dylib` -- even
#   though every individual libsystem_*.dylib piece it needs was already
#   present and correctly bound. Real dyld-940 apparently enforces "the
#   executable's own LC_LOAD_DYLIB list must include libSystem.B.dylib"
#   as a real, separate invariant from "every undefined symbol
#   resolves" -- same real precedent build_syslogd.sh's own link line
#   already carries (explicitly listing libSystem.B.dylib alongside the
#   individual libsystem_*.dylib pieces, not relying on one or the
#   other alone). Added to the smoketest's own link line only --
#   libobjc.A.dylib itself is a dylib, not an executable, and this
#   invariant does not apply to it (confirmed: the real link/load
#   succeeded before this fix, only the executable failed).
# - _pthread_cond_wait$UNIX2003: real UNIX2003 aliasing gap, same
#   -Wl,-alias precedent build_configd.sh/build_libsystem_pthread.sh
#   already established -- this project's libsystem_pthread.dylib
#   exports the real, unaliased _pthread_cond_wait.
# - typeof (objc-runtime-new.mm): real GNU-extension keyword objc4
#   expects available in C++ mode; -std=gnu++17 (not strict c++17)
#   enables it, matching real Apple's own compiler-default language
#   dialect for this kind of target.
# - @try/@throw (objc-exception.mm/objc-initialize.mm): needs
#   -fobjc-exceptions (this file IS the real implementation of ObjC
#   exception support, so of course it needs the front-end feature
#   enabled -- -fno-objc-exceptions was this script's own over-eager
#   first guess, corrected once these 2 files' real errors showed why).
#
# ============================================================
# NOT built this pass (real, deliberately out of scope -- see DAR-236's
# own ticket comments for full detail)
# ============================================================
# - A real MIG client for restartable.defs (see HAVE_TASK_RESTARTABLE_
#   RANGES above) -- real, legitimate future work, not required for
#   correct basic operation.
# - Wiring this dylib into configd.macho/IPMonitor.m/QoSMarking.m --
#   that's DAR-170's job to resume separately once this ticket is done;
#   deliberately not attempted this pass per explicit scope boundary.
# NOTE (2026-10-09): left at -O0 on purpose. Built at -O2 (like the string, malloc, pthread, dispatch, dyld and libm tiers, which all went from
# -O0 to -O2 the same day) the link succeeds but the post-link markgc step fails: "libobjc.A.dylib has no __objc_init_offs section".
# Whoever takes this on needs to see why -O2 drops that section (the _objc_init initializer offsets) first.
set -e
cd "$(dirname "$0")"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
CLANGXX="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE)" >&2
    exit 1
fi

OBJC4="../../third_party/objc4/runtime"
if [ ! -d "$OBJC4" ]; then
    echo "error: $OBJC4 not found -- run setup_third_party.sh first (objc4 pinned at objc4-818.2, commit 6ef00b3)" >&2
    exit 1
fi
LIBCXX="../../third_party/llvm-project/libcxx"
LIBCXXABI="../../third_party/llvm-project/libcxxabi"
XNU="../../third_party/xnu-7195"
DYLD="../../third_party/dyld"
PLATFORM="../../third_party/libplatform"
LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
DISPATCH="../../third_party/libdispatch"
CLOSURE="../../third_party/libclosure"
OUT="libc_build"
COMPAT="$OUT/objc4_compat_include"
PUBHDRS="$OUT/objc4_public_headers"
OBJDIR="$OUT/objc4_obj"

for dylib in libsystem_kernel libsystem_platform libsystem_blocks libsystem_pthread \
             libsystem_malloc libsystem_c libsystem_notify libsystem_m libc++ libunwind libdyld libSystem.B; do
    if [ ! -f "$OUT/system/$dylib.dylib" ]; then
        echo "error: $OUT/system/$dylib.dylib not found -- run its build_*_dylib.sh first" >&2
        exit 1
    fi
done
if [ ! -f "$OUT/libc_features_gen/arm64/libc-features.h" ]; then
    echo "error: $OUT/libc_features_gen/arm64/libc-features.h not found -- run build_launchd_core.sh first (it generates this)" >&2
    exit 1
fi

rm -rf "$OBJDIR" "$COMPAT" "$PUBHDRS"
mkdir -p "$OBJDIR" "$COMPAT/os" "$COMPAT/System" "$PUBHDRS/objc"

# ------------------------------------------------------------------
# Real public <objc/*.h> header staging: every real top-level header
# under third_party/objc4/runtime symlinked into $PUBHDRS/objc/,
# matching real Apple's own Headers-build-phase flattening into
# usr/include/objc/. Absolute-path symlinks (relative depth math from
# this nested a directory is easy to get off-by-one on, confirmed the
# hard way this session).
# ------------------------------------------------------------------
REPO_ABS="$(cd ../.. && pwd)"
for h in "$REPO_ABS"/third_party/objc4/runtime/*.h; do
    ln -sf "$h" "$PUBHDRS/objc/$(basename "$h")"
done

# ------------------------------------------------------------------
# Real, narrow compat headers (see this script's own header comment
# above for the full justification behind each one).
# ------------------------------------------------------------------
cat > "$COMPAT/objc-probes.h" <<'EOF'
#ifndef OBJC4_COMPAT_OBJC_PROBES_H
#define OBJC4_COMPAT_OBJC_PROBES_H
#define OBJC_RUNTIME_OBJC_EXCEPTION_THROW(arg0) do {} while(0)
#define OBJC_RUNTIME_OBJC_EXCEPTION_THROW_ENABLED() 0
#define OBJC_RUNTIME_OBJC_EXCEPTION_RETHROW() do {} while(0)
#define OBJC_RUNTIME_OBJC_EXCEPTION_RETHROW_ENABLED() 0
#endif
EOF

cat > "$COMPAT/objc-bp-assist.h" <<'EOF'
/* Genuinely vestigial in this pinned snapshot -- see build_objc4.sh's
 * own header comment. */
EOF

cat > "$COMPAT/System/pthread_machdep.h" <<'EOF'
/* Intentionally empty -- see build_objc4.sh's own header comment
 * (real Apple's own objc-os.h already has the correct fallback,
 * gated behind `#if defined(__PTK_FRAMEWORK_OBJC_KEY0)`). */
EOF

cat > "$COMPAT/os/feature_private.h" <<'EOF'
#ifndef OBJC4_COMPAT_OS_FEATURE_PRIVATE_H
#define OBJC4_COMPAT_OS_FEATURE_PRIVATE_H
#define os_feature_enabled_simple(ns, feature, default_value) (default_value)
#endif
EOF

cat > "$COMPAT/os/variant_private.h" <<'EOF'
#ifndef OBJC4_COMPAT_OS_VARIANT_PRIVATE_H
#define OBJC4_COMPAT_OS_VARIANT_PRIVATE_H
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
bool os_variant_has_internal_diagnostics(const char *subsystem);
#ifdef __cplusplus
}
#endif
#endif
EOF

cat > "$COMPAT/objc4_dyld_version_compat.h" <<'EOF'
#ifndef OBJC4_COMPAT_DYLD_VERSION_COMPAT_H
#define OBJC4_COMPAT_DYLD_VERSION_COMPAT_H
#include <mach-o/dyld_priv.h>
#define OBJC4_COMPAT_PLATFORM_IOS      2u
#define OBJC4_COMPAT_PLATFORM_MACOS    1u
#define OBJC4_COMPAT_PLATFORM_TVOS     3u
#define OBJC4_COMPAT_PLATFORM_WATCHOS  4u
#define OBJC4_COMPAT_PLATFORM_BRIDGEOS 5u
static const dyld_build_version_t dyld_fall_2018_os_versions = { OBJC4_COMPAT_PLATFORM_IOS, 0x000C0000u };
static const dyld_build_version_t dyld_fall_2020_os_versions = { OBJC4_COMPAT_PLATFORM_IOS, 0x000E0000u };
static const dyld_build_version_t dyld_platform_version_macOS_10_11  = { OBJC4_COMPAT_PLATFORM_MACOS, 0x000A0B00u };
static const dyld_build_version_t dyld_platform_version_macOS_10_12  = { OBJC4_COMPAT_PLATFORM_MACOS, 0x000A0C00u };
static const dyld_build_version_t dyld_platform_version_macOS_10_13  = { OBJC4_COMPAT_PLATFORM_MACOS, 0x000A0D00u };
static const dyld_build_version_t dyld_platform_version_iOS_10_0     = { OBJC4_COMPAT_PLATFORM_IOS, 0x000A0000u };
static const dyld_build_version_t dyld_platform_version_tvOS_10_0    = { OBJC4_COMPAT_PLATFORM_TVOS, 0x000A0000u };
static const dyld_build_version_t dyld_platform_version_watchOS_3_0  = { OBJC4_COMPAT_PLATFORM_WATCHOS, 0x00030000u };
static const dyld_build_version_t dyld_platform_version_bridgeOS_2_0 = { OBJC4_COMPAT_PLATFORM_BRIDGEOS, 0x00020000u };
#endif
EOF

# objc4_config_override.h / objc4_config_override_no_restartable.h are
# real, tracked, hand-maintained files (byte-for-byte copies of the real
# pinned objc-config.h except one macro each -- see their own header
# comments) -- NOT regenerated here, checked in directly so a diff
# against the real upstream file stays meaningful.
for f in objc4_config_override.h objc4_config_override_no_restartable.h; do
    if [ ! -f "$f" ]; then
        echo "error: $f not found (should be checked in next to this script)" >&2
        exit 1
    fi
    cp "$f" "$COMPAT/$f"
done

# No -ffreestanding/-fno-builtin here (DAR-237, 2026-09-28), unlike most of
# this tree's libc builds. Apple builds objc4 hosted, and objc4 depends on
# it: Settings[] (runtime/objc-runtime.mm:85-89) is initialized with
# strlen("OBJC_...") and must be constant-initialized, because
# environ_init() reads it first thing in _objc_init() (objc-os.mm:929),
# before static_init() runs any C++ initializer. With -fno-builtin, clang
# can't fold strlen, so Settings[] became a dynamic initializer that ran
# too late. Every OBJC_* variable was ignored, and OBJC_PRINT_OPTIONS
# crashed every process at objc-runtime.mm:465. Checked with
# `nm objc-runtime.o | grep __GLOBAL__sub_I`, which is now empty.
CXXFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -fno-stack-protector -O0 \
    -std=gnu++17 -fno-exceptions -fno-objc-arc -fobjc-exceptions \
    -DPRIVATE=1 -DNDEBUG \
    -DSUPPORT_PREOPT=0 \
    -D__BRIDGEOS_DEPRECATED\(...\)= -D__BRIDGEOS_UNAVAILABLE= \
    -D__API_UNAVAILABLE_PLATFORM_bridgeos=bridgeos,unavailable \
    -D__API_AVAILABLE_PLATFORM_bridgeos\(x\)=ios,introduced=x \
    -nostdinc++ -isystem "$LIBCXX/include" -I "$LIBCXXABI/include" \
    -I "$OBJC4" -I "$OBJC4/Messengers.subproj" -I "$OBJC4/OldClasses.subproj" \
    -I "$COMPAT" -I "$PUBHDRS" \
    -I "$XNU/bsd" -I "$DYLD/include" \
    -I "$PLATFORM/private" -I "$PLATFORM/include" \
    -I "$LIBC/include" -I "$LIBC/darwin" -I "$LIBC/locale" -I "$LIBC/locale/FreeBSD" -I "$LIBC/stdtime/FreeBSD" \
    -I "$PTHREAD/include" -I "$PTHREAD/private" \
    -I "$OUT/compat_include" -I "$OUT/libc_features_gen/arm64" \
    -I "$XNU/libsyscall" -I "$XNU/libsyscall/mach" -I "$XNU/libkern" \
    -I "$DISPATCH" -I "$DISPATCH/private" \
    -I "$CLOSURE" -I "$LIBC" -I "$LIBC/os" \
    -include "objc4_dyld_version_compat.h" \
    -idirafter "$XNU/osfmk" -idirafter "$XNU/EXTERNAL_HEADERS" \
    -Wno-nullability-completeness -Wno-availability -Wno-deprecated-declarations)

MM_FILES="NSObject objc-accessors objc-auto objc-block-trampolines objc-cache-old objc-cache \
  objc-class-old objc-class objc-errors objc-exception objc-file-old objc-file objc-initialize \
  objc-layout objc-load objc-loadmethod objc-lockdebug objc-opt objc-os objc-references \
  objc-runtime-new objc-runtime-old objc-runtime objc-sel-old objc-sel-set objc-sel objc-sync \
  objc-typeencoding objc-weak objc-zalloc Object Protocol hashtable2 maptable"

ok=0
fail=0
for base in $MM_FILES; do
    f="$OBJC4/$base.mm"
    out="$OBJDIR/${base}.o"
    extra=()
    case "$base" in
        objc-opt) extra=(-include "objc4_config_override.h") ;;
        objc-cache) extra=(-include "objc4_config_override_no_restartable.h") ;;
    esac
    if "$CLANGXX" "${CXXFLAGS[@]}" "${extra[@]}" "$f" -o "$out" 2>"$OBJDIR/${base}.err"; then
        ok=$((ok + 1)); rm -f "$OBJDIR/${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OBJDIR/${base}.err")"
    fi
done
# List.m (OldClasses.subproj) -- real Apple compiles it as Objective-C++
# too (same MM_FILES-style flags, just a .m extension).
f="$OBJC4/OldClasses.subproj/List.m"
out="$OBJDIR/List.o"
if "$CLANGXX" "${CXXFLAGS[@]}" -x objective-c++ "$f" -o "$out" 2>"$OBJDIR/List.err"; then
    ok=$((ok + 1)); rm -f "$OBJDIR/List.err"
else
    fail=$((fail + 1))
    echo "  gap: List -- $(grep -m1 'error:' "$OBJDIR/List.err")"
fi

# Real objc4_preopt_fallback_compat.mm needs the SAME include set (it
# includes objc-private.h directly) but the REAL, unmodified
# SUPPORT_PREOPT=1 -- no per-file override.
if "$CLANGXX" "${CXXFLAGS[@]}" objc4_preopt_fallback_compat.mm -o "$OBJDIR/objc4_preopt_fallback_compat.o" 2>"$OBJDIR/objc4_preopt_fallback_compat.err"; then
    ok=$((ok + 1)); rm -f "$OBJDIR/objc4_preopt_fallback_compat.err"
else
    fail=$((fail + 1))
    echo "  gap: objc4_preopt_fallback_compat -- $(grep -m1 'error:' "$OBJDIR/objc4_preopt_fallback_compat.err")"
fi

echo "objc4 (.mm/.m): $ok compiled, $fail gaps (see $OBJDIR/*.err for full detail)"
if nm "$OBJDIR/objc-runtime.o" | grep -q "__GLOBAL__sub_I"; then
    echo "error: objc-runtime.o has a dynamic initializer; Settings[] must be constant-initialized (see CXXFLAGS note)" >&2
    exit 1
fi
if [ "$fail" -ne 0 ]; then
    exit 1
fi

# Real arm64 message-send trampoline assembly -- confirmed empirically
# this session to need NO portable-C fallback (see this script's own
# header comment).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    -I "$OBJC4" -I "$OBJC4/Messengers.subproj" \
    "$OBJC4/Messengers.subproj/objc-msg-arm64.s" -o "$OBJDIR/objc-msg-arm64.o"
echo "objc-msg-arm64.s: assembled clean (no fallback needed)"

# Real, narrow C compat bodies (getsect.h reimplementation, misc
# libc/libmalloc/Mach-trap stand-ins) -- see their own header comments.
CFLAGS_COMPAT=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -DPRIVATE=1 -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 \
    -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -I "$LIBC/include" -I "$LIBC/darwin" -I "$OUT/compat_include" \
    -I "$XNU/bsd" -idirafter "$XNU/libsyscall" -idirafter "$XNU/libsyscall/mach" -idirafter "$XNU/osfmk" -idirafter "$XNU/EXTERNAL_HEADERS" \
    -Wno-nullability-completeness -Wno-availability -Wno-deprecated-declarations)
for base in objc4_getsect_compat objc4_misc_compat; do
    if "$CLANG" "${CFLAGS_COMPAT[@]}" "$base.c" -o "$OBJDIR/$base.o" 2>"$OBJDIR/$base.err"; then
        rm -f "$OBJDIR/$base.err"
    else
        echo "  gap: $base -- $(grep -m1 'error:' "$OBJDIR/$base.err")"
        exit 1
    fi
done
echo "objc4 compat .c files: compiled clean"

rm -f "$OUT/libobjc4_runtime.a"
ar rcs "$OUT/libobjc4_runtime.a" "$OBJDIR"/*.o
echo "wrote $OUT/libobjc4_runtime.a ($(ls "$OBJDIR"/*.o | wc -l | tr -d ' ') objects)"

# ------------------------------------------------------------------
# Real dylib link, matching real Apple's own product name/install path
# (/usr/lib/libobjc.A.dylib) and this project's "prefer real dylibs"
# rule. -force_load is load-bearing (see this script's own header
# comment) -- without it, nothing from the archive gets pulled in.
# ------------------------------------------------------------------
rm -f "$OUT/libobjc.A.dylib"
"$CLANGXX" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -B"$NEWLD_BINDIR" \
    -dynamiclib \
    -install_name /usr/lib/libobjc.A.dylib \
    -o "$OUT/libobjc.A.dylib" \
    -Wl,-force_load,"$OUT/libobjc4_runtime.a" \
    -Wl,-alias,_pthread_cond_wait,'_pthread_cond_wait$UNIX2003' \
    "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_platform.dylib" \
    "$OUT/system/libsystem_blocks.dylib" \
    "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_c.dylib" \
    "$OUT/system/libsystem_notify.dylib" \
    "$OUT/system/libsystem_m.dylib" \
    "$OUT/system/libc++.dylib" \
    "$OUT/system/libunwind.dylib" \
    "$OUT/system/libdyld.dylib" \
    2>"$OUT/objc4_link.err" \
    && echo "real link: 0 undefined symbols -- libobjc.A.dylib fully closed" \
    || { n=$(grep -c 'Undefined symbol' "$OUT/objc4_link.err" || true); echo "real link found $n undefined symbol group(s):"; grep '^  "' "$OUT/objc4_link.err" | sed 's/,.*//' | sort -u; tail -80 "$OUT/objc4_link.err" >&2; exit 1; }

# markgc (DAR-237, 2026-09-28): Apple's own post-link step for this
# target, "Run Script (markgc)" (objc.xcodeproj/project.pbxproj:754-768),
# builds third_party/objc4/markgc.cpp for the host and runs it on the
# dylib. It renames __TEXT,__init_offsets to __objc_init_offs (and
# __mod_init_func/__mod_term_func likewise) and clears the section type
# (markgc.cpp:385-409), so dyld no longer runs libobjc's C++ static
# initializers. _objc_init() runs them itself through static_init()
# (runtime/objc-os.mm:652-669), before environ_init() reads Settings[] and
# before map_images. _objc_init() runs inside libSystem's initializer,
# before dyld would reach libobjc's own. Without this, every OBJC_*
# environment option was silently ignored (Settings[] still zero), and
# OBJC_PRINT_OPTIONS crashed every process (objc-runtime.mm:465).
MARKGC="$OUT/objc4_markgc"
xcrun -sdk macosx clang++ -Wall -std=c++11 ../../third_party/objc4/markgc.cpp -o "$MARKGC"
"$MARKGC" "$OUT/libobjc.A.dylib"
if otool -l "$OUT/libobjc.A.dylib" | grep -qE "sectname __(init_offsets|mod_init_func)$"; then
    echo "error: markgc left an __init_offsets/__mod_init_func section in libobjc.A.dylib" >&2
    exit 1
fi
otool -l "$OUT/libobjc.A.dylib" | grep -q "sectname __objc_init_offs" || {
    echo "error: libobjc.A.dylib has no __objc_init_offs section after markgc" >&2
    exit 1
}

./bind_target_audit.sh "$OUT/libobjc.A.dylib" || {
    echo "error: libobjc.A.dylib has an unresolvable two-level bind -- it would abort at dyld load" >&2
    exit 1
}

# ------------------------------------------------------------------
# libobjc-trampolines.dylib (DAR-236, 2026-09-28): objc.xcodeproj's
# separate "objc-trampolines" PBXNativeTarget (product
# libobjc-trampolines.dylib, INSTALL_PATH /usr/lib). Its Sources phase is
# the four per-arch objc-blocktramps-*.s files, of which arm64 is ours;
# OTHER_CFLAGS -fdollars-in-identifiers, OTHER_LDFLAGS
# -not_for_dyld_shared_cache -nodefaultlibs (project.pbxproj:866-876,
# 1295-1323). imp_implementationWithBlock() dlopen()s it by that path and
# vm_remap()s its TEXT segment (runtime/objc-block-trampolines.mm:173,
# 376-386); it imports nothing. It is linked against libSystem.B.dylib
# anyway because the daily-driver ld refuses any dylib that doesn't ("ld:
# dynamic executables or dylibs must link with libSystem.dylib"), which
# Apple's -nodefaultlibs link predates; every process already has
# libSystem.B loaded, so the extra load command changes nothing at runtime.
# ------------------------------------------------------------------
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -fdollars-in-identifiers \
    -I "$OBJC4" -idirafter "$XNU/osfmk" \
    "$OBJC4/objc-blocktramps-arm64.s" -o "$OBJDIR/objc-blocktramps-arm64.o"
rm -f "$OUT/libobjc-trampolines.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -nodefaultlibs \
    -B"$NEWLD_BINDIR" \
    -dynamiclib -install_name /usr/lib/libobjc-trampolines.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -o "$OUT/libobjc-trampolines.dylib" \
    "$OBJDIR/objc-blocktramps-arm64.o" \
    "$OUT/system/libSystem.B.dylib"
for sym in __objc_blockTrampolineImpl __objc_blockTrampolineStart __objc_blockTrampolineLast; do
    nm -gUj "$OUT/libobjc-trampolines.dylib" | grep -qx "$sym" || {
        echo "error: libobjc-trampolines.dylib does not export $sym" >&2
        exit 1
    }
done
echo "wrote $OUT/libobjc-trampolines.dylib"

# libdispatch.dylib is linked (upward) against libobjc_for_libdispatch.tbd
# before this dylib exists (DAR-237). Check that the real libobjc exports
# everything that stub promises.
for sym in $(sed -n '/symbols:/,/]/p' libobjc_for_libdispatch.tbd | tr -d '[],' | sed 's/symbols://' | tr ' ' '\n' | grep .) \
           '_OBJC_CLASS_$_NSObject' '_OBJC_METACLASS_$_NSObject'; do
    nm -gUj "$OUT/libobjc.A.dylib" | grep -qxF "$sym" || {
        echo "error: libobjc.A.dylib does not export $sym, which libobjc_for_libdispatch.tbd promises libdispatch.dylib" >&2
        exit 1
    }
done

# ------------------------------------------------------------------
# Real, minimal, standalone Objective-C smoketest.
# ------------------------------------------------------------------
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -fno-objc-arc -fobjc-exceptions \
    -I "$PUBHDRS" -I "$LIBC/include" -I "$LIBC/darwin" -I "$OUT/libc_features_gen/arm64" \
    -Wno-nullability-completeness \
    objc4_smoketest.m -o "$OUT/objc4_smoketest_obj.o"

rm -f objc4_smoketest.macho
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o objc4_smoketest.macho \
    -DCRT_NO_PTHREAD_BOOTSTRAP ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT/objc4_smoketest_obj.o" \
    "$OUT/libobjc.A.dylib" \
    "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_platform.dylib" \
    "$OUT/system/libsystem_blocks.dylib" \
    "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_c.dylib" \
    "$OUT/system/libsystem_notify.dylib" \
    "$OUT/system/libsystem_m.dylib" \
    "$OUT/system/libc++.dylib" \
    "$OUT/system/libunwind.dylib" \
    "$OUT/system/libdyld.dylib" \
    "$OUT/system/libSystem.B.dylib" \
    2>"$OUT/objc4_smoketest_link.err" \
    && echo "real link: 0 undefined symbols -- objc4_smoketest.macho fully closed" \
    || { n=$(grep -c 'Undefined symbol' "$OUT/objc4_smoketest_link.err" || true); echo "real link found $n undefined symbol group(s):"; grep '^  "' "$OUT/objc4_smoketest_link.err" | sed 's/,.*//' | sort -u; tail -80 "$OUT/objc4_smoketest_link.err" >&2; exit 1; }

./bind_target_audit.sh objc4_smoketest.macho || {
    echo "error: objc4_smoketest.macho has an unresolvable two-level bind" >&2
    exit 1
}

echo "wrote $OUT/libobjc.A.dylib and objc4_smoketest.macho"
