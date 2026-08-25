#!/bin/bash
# Builds libc_build/dyld_core.a: real, UNMODIFIED Apple dyld-940 source --
# the exact 22-file set (21 real .cpp/.c + 1 real .s) Apple's own
# dyld.xcodeproj compiles into the actual `dyld` tool target (verified by
# reading the real Xcode project file's PBXSourcesBuildPhase for the
# `dyld` PBXNativeTarget directly -- F9ED4C970630A76000DF4E74 -- not
# guessed from directory listings). Phase 6.5 Phase C
# (docs/daemon-roadmap.md).
#
# Real re-pin, not a first attempt: this project's dyld pin was
# `dyld-832.7.3` (matching the macos-112 manifest) until 2026-08-25, when
# porting dyld2.cpp/ImageLoader* (~31K LOC, all real, all compiled clean)
# hit a permanent wall -- `dyld3::Loader`, constructed in dyld2.cpp's
# closure-mode launch path, does not exist in ANY released dyld snapshot
# ever (checked the full git history from dyld-832.7.3 through the
# latest dyld-1378). User decision: retarget to `dyld-940`, the earliest
# tag with Apple's real dyld4 rewrite (`dyld/Loader.cpp`+
# `JustInTimeLoader.cpp`+`PrebuiltLoader.cpp`) -- a different,
# incompatible architecture from dyld-832.7.3's dyld2/dyld3/ImageLoader
# split, not an extension of it. See setup_third_party.sh's own header
# comment and docs/daemon-roadmap.md for the full story. This is a
# ground-up rewrite of this script, not an incremental patch --
# essentially none of the old dyld2/ImageLoader-era file list or
# reasoning carries over (though most of the SDK/XNU header-compat
# infrastructure below does, since that's about this project's toolchain
# constraints, not dyld's own architecture).
#
# The 22 real files (all compile clean, zero fabricated stubs beyond the
# narrowly-scoped compat headers documented below):
#   dyld/dyldStartup.s, dyldMain.cpp, DyldProcessConfig.cpp,
#     DyldRuntimeState.cpp, Loader.cpp, JustInTimeLoader.cpp,
#     PrebuiltLoader.cpp, DyldDelegates.cpp, DebuggerSupport.cpp,
#     SharedCacheRuntime.cpp, DyldAPIs.cpp, PrebuiltObjC.cpp, Tracing.cpp,
#     glue.c -- the real dyld4 engine + its own glue.
#   common/OptimizerSwift.cpp, Allocator.cpp, DyldSharedCache.cpp,
#     MachOFile.cpp, MachOLoaded.cpp, MachOAnalyzer.cpp, Diagnostics.cpp,
#     PerfectHash.cpp -- the shared Mach-O parsing/diagnostics/allocation
#     engine, used by both `dyld` and (separately, not built here)
#     `libdyld.dylib`/`dsc_extractor`/other real Xcode targets in this
#     same repo.
# Deliberately NOT built (confirmed, by reading the real Xcode project,
# to belong to OTHER targets -- `libdyld.dylib`, `dsc_extractor`, etc --
# not the `dyld` tool itself, not a gap): LibSystemHelpers.cpp (dyld only
# needs its header -- an abstract interface whose concrete implementation
# is registered into dyld at runtime by libSystem.dylib, never statically
# linked into dyld itself), dyld_process_info.cpp/
# dyld_process_info_notify.cpp (external debugger/Instruments remote
# process introspection -- this target has no such consumer),
# libdyldGlue.cpp, PathOverrides.cpp, RootsChecker (doesn't exist in this
# tag), ClosureFileSystemPhysical/Null.cpp, Closure/ClosureBuilder/
# ClosureWriter.cpp (the whole dyld3-era closure-file-on-disk format is
# gone in dyld4 -- JustInTimeLoader/PrebuiltLoader replace it with a
# different in-memory/on-disk model entirely).
#
# Real, narrowly-scoped compat gaps found and closed this round (same
# "real source first, small honest shim only where truly needed"
# standard as every prior tier):
#   - dyld_compat/vproc_priv.h: dyld/DyldDelegates.cpp includes launchd's
#     real public header (launchd itself never open-sourced, already-
#     documented project fact) but calls zero vproc_* symbols in that
#     file (grepped in full) -- real dead include, empty placeholder.
#   - dyld_compat/System/atexit.h: real, standard Itanium C++ ABI
#     `__cxa_range_t` (same layout in every real implementation, not
#     Apple-internal) -- Apple's own real `<System/atexit.h>` is a
#     Libsystem-internal header never open-sourced standalone, but
#     dyld/DyldRuntimeState.cpp's own usage (addr+length fields for
#     `__cxa_finalize_ranges`) fully confirms this exact, standard shape.
#   - dyld_compat/PrebuiltLoader_version.h: real Apple build step
#     (build-scripts/prebuilt-loader-hash.sh) computes this as an md5 of
#     several generated .ast files, purely so a dyld build refuses a
#     PrebuiltLoader cache written by a DIFFERENT build of itself -- this
#     target never reads/writes real Apple prebuilt caches, so any fixed
#     stable value is correct (not a real port of the hash script).
#   - dyld_compat/dyld_platform_compat.h: `PLATFORM_IOSMAC` is a real
#     historical alias for `PLATFORM_MACCATALYST` (this project's pinned
#     iOS 14.4 SDK only has the newer name, same real value 6, confirmed
#     against <mach-o/loader.h>) -- not a new platform. Separately,
#     `task_dyld_process_info_notify_get()` is a real mach trap added to
#     a newer xnu than this project's pinned xnu-7195 (confirmed absent
#     from every .defs file there) -- used only by dyld's
#     RemoteNotificationResponder (external debugger attach support,
#     no consumer on this target); the real call site already handles
#     failure gracefully, so a real, honest failure-return stub is
#     correct here, not a stand-in for missing logic.
#   - dyld_compat/derived/{dyld.h,dyld_priv.h}: real Apple's own dyld
#     build has a Run Script build phase
#     ("cp ${SRCROOT}/include/mach-o/dyld.h ${DSTROOT}/.../dyld.h") that
#     installs these two headers under a flat name for other targets'
#     `#include "dyld.h"` (quoted, relative-search) to find -- real
#     content, just replicating that exact same real copy step, not
#     guessing why DyldAPIs.cpp's quoted include needs it.
#   - dyld_compat/libamfi.h, sandbox/private.h, CrashReporterClient.h,
#     dyld_deny_list_compat.h, mach/semaphore.h: same real gaps/fixes
#     already established porting dyld-832.7.3 (AMFI/sandbox/
#     CrashReporter don't exist on this target; `inDenyList()`-class
#     per-app compat overrides don't apply to a from-scratch port;
#     semaphore.h is real xnu-7195 source, copied to dodge the same
#     `-I`-vs-`-idirafter` shadowing risk documented below).
#   - MachOLoaded.cpp's forEachCDHashOfCodeSignature() still needs real
#     SHA-1/256/384 behind corecrypto's real ccdigest_info API --
#     dyld_ccdigest_compat.cpp (unchanged from the dyld-832.7.3 port,
#     same real gap, same real fix).
#
# Real, reproducible MIG regeneration + AvailabilityVersions dependency:
#   - 19 real mach/*.h headers regenerated fresh from xnu-7195 .defs
#     (same as the dyld-832.7.3 port -- `-DLIBSYSCALL_INTERFACE=1` for
#     plain routine names, `-DKERNEL_PRIVATE` for upl/memory_object_
#     control's upl_t family).
#   - `<dyld/VersionMap.h>` (real OS-version compatibility data table,
#     `dyld3::sVersionMap`) comes from a REAL, separate
#     apple-oss-distributions repo, `AvailabilityVersions`, that this
#     project's own kernel build (tools/build_xnu_7195.sh) already
#     clones+builds as part of constructing a local SDK -- this script
#     reuses that real, already-generated output
#     (build/xnu-latest-build/AvailabilityVersions/dst/usr/local/include)
#     rather than re-vendoring it, and fails fast with a clear message
#     if that build hasn't been run yet.
#
# Every XNU/libplatform/libpthread/Libc include path below is
# `-idirafter`, never `-I` -- porting dyld-832.7.3 found the hard way
# that a high-priority `-I` into those trees lets a libsyscall-internal
# minimal `string.h` (or similar) silently shadow the SDK's real one,
# breaking `<cstring>`/`<string.h>` content project-wide. Same discipline
# kept here even though this is a fresh file list.
set -e
cd "$(dirname "$0")"

DYLD="../../third_party/dyld"
XNU="../../third_party/xnu-7195"
PTHREAD="../../third_party/libpthread"
PLATFORM="../../third_party/libplatform"
LIBC="../../third_party/Libc"
LIBCXX="../../third_party/llvm-project/libcxx"
LIBCXXABI="../../third_party/llvm-project/libcxxabi"
AVAIL_VERSIONS="../../build/xnu-latest-build/AvailabilityVersions/dst/usr/local/include"
OUT="libc_build"
# Real bug found the hard way: this directory held .o files from the
# old dyld-832.7.3 architecture (dyld2.o, ImageLoaderMachO.o,
# ClosureBuilder.o, ...) that a bare `mkdir -p` never cleared, and
# `ar rcs .../*.o` silently swept them into the new archive alongside
# the real dyld-940 objects -- every confusing "undefined symbol" from
# a real trial-link afterward traced back to THESE stale objects, not
# a real new gap. Always start this directory fresh.
rm -rf "$OUT/dyld_core"
mkdir -p "$OUT/dyld_core" "$OUT/dyld_mig"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANGXX="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANGXX" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -d "$DYLD/dyld" ]; then
    echo "error: $DYLD/dyld not found -- run ./setup_third_party.sh first" >&2
    exit 1
fi
if [ ! -f "$AVAIL_VERSIONS/dyld/VersionMap.h" ]; then
    echo "error: $AVAIL_VERSIONS/dyld/VersionMap.h not found -- run tools/build_xnu_7195.sh first (it clones+builds AvailabilityVersions as part of constructing a local SDK; see this script's header comment)" >&2
    exit 1
fi

# System/sys/{codesign,csr,reason,content_protection,kdebug,mman}.h +
# System/machine/cpu_capabilities.h + System/kern/cs_blobs.h: real
# xnu-7195 bsd/sys and osfmk source, copied under a "System/" prefix
# directory so `#include <System/sys/foo.h>` resolves -- these files are
# only ever installed under that framework prefix in a real macOS SDK,
# but the real content is identical either way.
mkdir -p "$OUT/dyld_compat_sys/System/sys" "$OUT/dyld_compat_sys/System/machine" "$OUT/dyld_compat_sys/System/kern"
for f in codesign csr reason content_protection kdebug mman; do
    cp "$XNU/bsd/sys/$f.h" "$OUT/dyld_compat_sys/System/sys/$f.h"
done
cp "$XNU/osfmk/arm/cpu_capabilities.h" "$OUT/dyld_compat_sys/System/machine/cpu_capabilities.h"
cp "$XNU/osfmk/kern/cs_blobs.h" "$OUT/dyld_compat_sys/System/kern/cs_blobs.h"

# Real, targeted override: the iPhoneOS SDK's own <sys/proc_info.h> (a
# private header, shared verbatim from macOS) references `struct
# kevent_qos_s`, but this SDK's own <sys/event.h> is an older/smaller
# public subset that never defines it at all -- real xnu-7195
# bsd/sys/{event,proc_info}.h have the matching, complete pair.
mkdir -p "$OUT/dyld_compat_sys_override/sys"
cp "$XNU/bsd/sys/event.h" "$OUT/dyld_compat_sys_override/sys/event.h"
cp "$XNU/bsd/sys/proc_info.h" "$OUT/dyld_compat_sys_override/sys/proc_info.h"

# Real Apple build step, replicated: dyld.xcodeproj's libdyld "Headers"
# build phase does `cp include/mach-o/dyld.h $DST/dyld.h` (and
# dyld_priv.h) so other targets' quoted `#include "dyld.h"` finds a flat
# copy -- see this script's header comment.
mkdir -p "$OUT/dyld_compat_derived"
cp "$DYLD/include/mach-o/dyld.h" "$OUT/dyld_compat_derived/dyld.h"
cp "$DYLD/include/mach-o/dyld_priv.h" "$OUT/dyld_compat_derived/dyld_priv.h"

# Real vintage mismatch, patched on a COPY (never the real AvailabilityVersions
# checkout): that repo's AvailabilityInternalPrivate.h unconditionally
# (no #ifndef guard) redefines SPI_AVAILABLE/SPI_AVAILABLE_BEGIN to
# dispatch through a 13-platform-argument macro family (__API_AVAILABLE13
# etc, added for newer platforms like visionOS) that this project's
# pinned iOS 14.4 SDK's own Availability.h never defines at all -- two
# real, separately-versioned Apple components that were never meant to
# be mixed like this. This target enforces no deployment-target/
# availability checks at all (no App Store, no real users), so blanking
# just these two macros in our own copy is the correct real fix -- every
# other real line in this generated header (crucially, the actual
# `dyld3::sVersionMap` real version-compatibility DATA this whole
# dependency exists for) is untouched.
mkdir -p "$OUT/dyld_compat_avail"
sed -E 's/#define (SPI_AVAILABLE|__SPI_AVAILABLE)(_BEGIN)?\(\.\.\.\).*/#define \1\2(...)/' \
    "$AVAIL_VERSIONS/AvailabilityInternalPrivate.h" > "$OUT/dyld_compat_avail/AvailabilityInternalPrivate.h"

# Real MIG regeneration (see this script's header comment) -- same
# `mig -header`/`-sheader` invocation shape as build_libmach_ipc.sh /
# the dyld-832.7.3 port.
MIG="$(which mig)"
if [ -z "$MIG" ] || [ ! -x "$MIG" ]; then
    echo "error: mig tool not found (needed to regenerate mach/task.h etc from real xnu-7195 .defs)" >&2
    exit 1
fi
XNU_ABS="$(cd "$XNU" && pwd)"
MIGOUT="$OUT/dyld_mig"
mkdir -p "$MIGOUT/mach"
for f in clock clock_priv host_priv host_security lock_set mach_host \
         mach_port processor processor_set task thread_act vm_map; do
    "$MIG" -arch arm64 -DLIBSYSCALL_INTERFACE=1 -I"$XNU_ABS/osfmk" \
        -header "$MIGOUT/mach/$f.h" \
        -user "$MIGOUT/${f}User.c" -server "$MIGOUT/${f}Server.c" -sheader "$MIGOUT/${f}Server.h" \
        "$XNU_ABS/osfmk/mach/$f.defs" >/dev/null
done
for f in memory_object_control upl; do
    "$MIG" -arch arm64 -DKERNEL_PRIVATE -DLIBSYSCALL_INTERFACE=1 -I"$XNU_ABS/osfmk" \
        -header "$MIGOUT/mach/$f.h" \
        -user "$MIGOUT/${f}User.c" -server "$MIGOUT/${f}Server.c" -sheader "$MIGOUT/${f}Server.h" \
        "$XNU_ABS/osfmk/mach/$f.defs" >/dev/null
done
for f in clock_reply exc mach_exc notify memory_object_default; do
    "$MIG" -arch arm64 -DLIBSYSCALL_INTERFACE=1 -I"$XNU_ABS/osfmk" \
        -header "$MIGOUT/mach/${f}.h" \
        -user "$MIGOUT/${f}User.c" -server "$MIGOUT/${f}Server.c" \
        -sheader "$MIGOUT/mach/${f}_server.h" \
        "$XNU_ABS/osfmk/mach/$f.defs" >/dev/null
done
echo "mig: regenerated 19 real mach/*.h headers from xnu-7195 .defs"

# Real, honest no-op CrashReporterClient implementation (no CrashReporter/
# ReportCrash daemon exists on this target).
cat > "$OUT/dyld_core/dyld_crash_reporter_compat.c" << 'EOF'
void CRSetCrashLogMessage(const char *msg) { (void)msg; }
void CRSetCrashLogMessage2(const char *msg) { (void)msg; }

/* Real, honest failure return -- see dyld_compat/dyld_platform_compat.h's
 * header comment (real mach trap this project's pinned xnu-7195 doesn't
 * have; no external-debugger-attach consumer exists on this target
 * anyway; the real call site already handles failure gracefully). */
typedef int kern_return_t;
typedef unsigned int mach_port_t;
#define KERN_FAILURE 5
kern_return_t task_dyld_process_info_notify_get(mach_port_t *names, unsigned int *namesCnt) {
    (void)names;
    if (namesCnt) *namesCnt = 0;
    return KERN_FAILURE;
}
EOF
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    "$OUT/dyld_core/dyld_crash_reporter_compat.c" -o "$OUT/dyld_core/dyld_crash_reporter_compat.o"

# Real, honest no-op sandbox_check() (no sandbox subsystem on this
# target; "not sandboxed" is the correct real answer).
cat > "$OUT/dyld_core/dyld_sandbox_compat.c" << 'EOF'
#include <sys/types.h>
int sandbox_check(pid_t pid, const char *operation, int type, ...) {
    (void)pid; (void)operation; (void)type;
    return 0;
}
EOF
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    "$OUT/dyld_core/dyld_sandbox_compat.c" -o "$OUT/dyld_core/dyld_sandbox_compat.o"

INCLUDES="-I $DYLD/dyld -I $DYLD/common -I $DYLD/include -I $DYLD/libdyld -I $DYLD/cache-builder \
    -I dyld_compat -I dyld_compat/sandbox \
    -I $OUT/dyld_compat_derived \
    -I $OUT/dyld_compat_avail \
    -I $OUT/dyld_compat_sys_override \
    -I $OUT/dyld_compat_sys \
    -I $MIGOUT -I $MIGOUT/mach \
    -idirafter $AVAIL_VERSIONS \
    -idirafter $XNU/libsyscall/wrappers/libproc \
    -idirafter $XNU/bsd -idirafter $XNU/osfmk -idirafter $XNU/libsyscall -idirafter $XNU/libsyscall/mach \
    -idirafter $PTHREAD/include -idirafter $PTHREAD/private \
    -idirafter $PLATFORM/private -idirafter $PLATFORM/include -idirafter $PLATFORM/internal \
    -idirafter $XNU/BUILD.hdrs/dst/usr/local/include \
    -idirafter $XNU/EXTERNAL_HEADERS \
    -idirafter $LIBC/darwin"

# bridgeos availability-macro gap: this iOS SDK's own Availability.h
# mishandles the bridgeos platform tuple form real xnu/libplatform
# headers use -- same real fix build_libc_locale.sh already established.
# _LIBCPP_NO_EXCEPTIONS=1: real Apple dyld.xcconfig flag (confirmed by
# reading it directly) -- dyld's own C++ code is built without
# exceptions, consistent with it using Diagnostics-style error returns
# throughout rather than throw/catch.
# SPI_AVAILABLE(...): AvailabilityVersions' own AvailabilityInternalPrivate.h
# dispatches to a 13-platform-argument macro family (__API_AVAILABLE13
# etc, for newer platforms like visionOS) this project's pinned iOS 14.4
# SDK's own Availability.h never defines at all -- a real vintage
# mismatch between the two real, separately-versioned components (same
# class of issue as the bridgeos macro fix below, just a newer/bigger
# one), not a dyld bug. This target enforces no deployment-target/
# availability checks at all (no App Store, no real users), so blanking
# the annotation entirely is the correct real fix, not a workaround for
# a real gap.
DEFS="-DBUILDING_DYLD=1 -DDYLD_VERSION=940 -DPRIVATE=1 -D_LIBCPP_NO_EXCEPTIONS=1 \
    -D__API_UNAVAILABLE_PLATFORM_bridgeos=ios,unavailable \
    -D__API_AVAILABLE_PLATFORM_bridgeos(x)=ios,introduced=x \
    -DSPI_AVAILABLE(...)= -D__SPI_AVAILABLE(...)= \
    -include dyld_compat/dyld_deny_list_compat.h \
    -include dyld_compat/dyld_platform_compat.h"

CXX_FLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -std=c++17 -funwind-tables $DEFS \
    -nostdinc++ -isystem $LIBCXX/include -I $LIBCXXABI/include \
    $INCLUDES \
    -Wno-macro-redefined -Wno-nullability-completeness -Wno-deprecated-declarations -Wno-availability"
C_FLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    $DEFS $INCLUDES \
    -Wno-macro-redefined -Wno-nullability-completeness -Wno-deprecated-declarations -Wno-availability"

ok=0
fail=0
# Real dyld/*.cpp -- the exact PBXSourcesBuildPhase list for the `dyld`
# PBXNativeTarget (see this script's header comment), minus dyldMain.cpp
# and glue.c (compiled separately below: one is the sole real .s file,
# the other is real C not C++).
for base in DyldProcessConfig DyldRuntimeState Loader JustInTimeLoader PrebuiltLoader \
            DyldDelegates DebuggerSupport SharedCacheRuntime DyldAPIs PrebuiltObjC Tracing dyldMain; do
    out="$OUT/dyld_core/${base}.o"
    if "$CLANGXX" $CXX_FLAGS "$DYLD/dyld/${base}.cpp" -o "$out" 2>"$OUT/dyld_core/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/dyld_core/${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OUT/dyld_core/${base}.err")"
    fi
done
# Real common/*.cpp -- shared Mach-O/diagnostics/allocation engine.
for base in OptimizerSwift Allocator DyldSharedCache MachOFile MachOLoaded MachOAnalyzer Diagnostics PerfectHash; do
    out="$OUT/dyld_core/${base}.o"
    if "$CLANGXX" $CXX_FLAGS "$DYLD/common/${base}.cpp" -o "$out" 2>"$OUT/dyld_core/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/dyld_core/${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OUT/dyld_core/${base}.err")"
    fi
done
# Real dyld/glue.c (plain C).
out="$OUT/dyld_core/glue.o"
if "$CLANG" $C_FLAGS "$DYLD/dyld/glue.c" -o "$out" 2>"$OUT/dyld_core/glue.err"; then
    ok=$((ok + 1))
    rm -f "$OUT/dyld_core/glue.err"
else
    fail=$((fail + 1))
    echo "  gap: glue -- $(grep -m1 'error:' "$OUT/dyld_core/glue.err")"
fi
# Real dyld/dyldStartup.s (the real entry point).
out="$OUT/dyld_core/dyldStartup.o"
if "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c "$DYLD/dyld/dyldStartup.s" -o "$out" 2>"$OUT/dyld_core/dyldStartup.err"; then
    ok=$((ok + 1))
    rm -f "$OUT/dyld_core/dyldStartup.err"
else
    fail=$((fail + 1))
    echo "  gap: dyldStartup -- $(cat "$OUT/dyld_core/dyldStartup.err")"
fi
# Real SHA-1/256/384 behind corecrypto's real ccdigest_info API -- see
# dyld_ccdigest_compat.cpp's own header comment. Compiled as C, not C++,
# for THIS dyld tag: dyld-940's MachOLoaded.cpp wraps its own corecrypto
# includes in a real `extern "C" { ... }` block (unlike dyld-832.7.3's
# version, which had no such guard -- see this file's own header comment
# on why it was originally made a .cpp), so it expects plain C symbol
# names here, not C++-mangled ones. `-x c` forces C language mode
# despite the .cpp extension -- the file's own content (designated
# struct initializers, no other C++ features) is valid C99 either way,
# confirmed by this real compile succeeding.
out="$OUT/dyld_core/dyld_ccdigest_compat.o"
if "$CLANG" $C_FLAGS -x c dyld_ccdigest_compat.cpp -o "$out" 2>"$OUT/dyld_core/dyld_ccdigest_compat.err"; then
    ok=$((ok + 1))
    rm -f "$OUT/dyld_core/dyld_ccdigest_compat.err"
else
    fail=$((fail + 1))
    echo "  gap: dyld_ccdigest_compat -- $(grep -m1 'error:' "$OUT/dyld_core/dyld_ccdigest_compat.err")"
fi
echo "dyld_core: $ok compiled, $fail gaps (see libc_build/dyld_core/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi

rm -f "$OUT/dyld_core.a"
ar rcs "$OUT/dyld_core.a" "$OUT"/dyld_core/*.o
echo "wrote $OUT/dyld_core.a"
