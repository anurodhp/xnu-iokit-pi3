#!/bin/bash
# Builds libc_build/libCoreFoundation.a: a real, minimal subset of
# swift-corelibs-foundation's CoreFoundation/ (third_party/
# swift-corelibs-foundation, pinned in setup_third_party.sh) -- Apple's
# own real, actively-maintained fork of the same real CF codebase, used
# here instead of apple-oss-distributions/CF because that repo's own
# last real tag (CF-855.17) is dated 2014-08-15 and CF is not part of
# the real macos-112 distribution-macOS manifest at all (both confirmed
# via real clones, not assumed) -- see this project's docs/backlog.md
# and setup_third_party.sh's own header comment for the full story.
#
# ============================================================
# HONEST SCOPE STATEMENT -- read this first
# ============================================================
# History: started 2026-08-25 as a genuinely minimal subset (just what
# launchd/core.c and IOKitUser called). Real ICU landed 2026-08-26/27
# (docs/backlog.md item d7), which unblocked CharacterSet/UniChar/
# StringUtilities collation/Locale/NumberFormatter/Calendar/TimeZone.
# Once those were in, a full sweep of every remaining real .c file in
# this checkout (2026-08-27, "just try compiling it" -- the same method
# that already proved CFError/CFDate/CFURL/CFCalendar/etc were free)
# found almost everything else compiles clean too: RunLoop/MachPort/
# Socket/Stream, PlugIn (bundles), DateFormatter family, URLComponents,
# AttributedString, RegularExpression, RunArray, StringTransform. The
# earlier assumption that RunLoop/Stream/Socket/MachPort structurally
# needed new kqueue+Mach port infrastructure was never actually
# verified by trial compile -- it didn't, this project's existing real
# syscall layer was already sufficient. This is now a large real
# majority of CoreFoundation, not a narrow subset -- but still not
# ALL of it; see the real, structural exclusions below.
#
# NOT INCLUDED -- each a real, verified (not assumed) compile failure,
# not a scope guess:
# - CFUserNotification.c (AppServices.subproj): real GUI alert-dialog
#   API: kCFUserNotificationIconURLKey/SoundURLKey/LocalizationURLKey
#   are hard `unavailable: not available on iOS` SDK errors. This
#   project has no GUI to show a user-notification dialog on anyway.
# - CFPreferences.c / CFXMLPreferencesDomain.c (Preferences.subproj):
#   ADDED 2026-08-28 -- previously excluded for a real, multi-part gap,
#   now closed. Root cause was narrower than first diagnosed: lines
#   24-27 gate `#include <unistd.h>`/`#include <CoreFoundation/CFUUID.h>`
#   behind `#if TARGET_OS_OSX`, while the code needing them (gethostuuid/
#   CFUUIDBytes/CFUUIDRef/etc, ~15 lines below) is gated behind the
#   broader `#if TARGET_OS_MAC` -- a real upstream guard mismatch, same
#   class of bug as this file's own already-documented __CFTSDInitialize
#   TARGET_OS_OSX/TARGET_OS_MAC mismatch. Fixed via a third commit in
#   the existing `swift-corelibs-foundation.patch`: widened the include
#   guard to match (closes the CFUUID-family errors for free), plus
#   `gethostuuid`'s own real "unavailable on iOS" SDK annotation
#   (separate issue -- the real kernel syscall, #142, genuinely exists
#   in this project's own xnu-7195 fork; the SDK header's restriction is
#   an iOS-app-store policy artifact, not a real functional gap) is
#   overridden via a new `cf_compat_headers/gethostuuid.h` forwarding
#   shim, same pattern as this directory's `mach/mach_vm.h`. A separate
#   real bug, an undeclared `prefDir` used outside its own declaration's
#   `#if TARGET_OS_OSX || TARGET_OS_WIN32 || TARGET_OS_LINUX` guard, is
#   fixed the same "make the guards agree" way (wrapped the one cleanup
#   line in the matching guard instead). CFXMLPreferencesDomain.c's
#   `__CFMilliSleep()` separately had no nanosleep(2) branch for
#   TARGET_OS_MAC (only TARGET_OS_OSX/LINUX/BSD) despite nanosleep being
#   ordinary, always-available Darwin/POSIX -- widened the same way.
#   Real preference-file storage (`CFPreferencesCopyAppValue` et al) now
#   compiles and links for real -- not yet QEMU-verified end-to-end
#   against a real flat preferences file on disk, just closes the real
#   trial-link gap this was blocking (see `docs/daemon-roadmap.md`'s
#   launchd gap-closure entries).
# - CFURLSessionInterface.c (URL.subproj): real libcurl dependency
#   (`curl/curl.h` not found) -- this project has already decided
#   against libcurl entirely (see docs/apple-oss-catalog.md).
# - CFWindowsUtilities.c (Base.subproj): Windows-only, structurally
#   inapplicable to a Darwin target.
#
# CFXMLInterface.c (Parsing.subproj) ADDED 2026-08-27: the earlier
# "conflicting types" compile failure was never a real libxml2
# dependency at all -- a real, tiny, unrelated bug in this exact
# swift-corelibs-foundation checkout (CFXMLInterface.h declares 3
# functions returning `Boolean`, CFXMLInterface.c defines them
# returning `bool`), fixed via a second commit in the existing
# swift-corelibs-foundation.patch. This project does have a real
# libxml2 now (third_party/libxml2, tag libxml2-34.9, matching both the
# macos-112 manifest and the Xcode 12 iPhoneOS SDK's own bundled
# headers exactly -- real upstream libxml2 2.9.4 either way) --
# tools/userland_staging/build_libxml2.sh builds it as a real per-
# library dylib, linked in below.
#
# Real fixes discovered getting the original 17 files to compile (all
# documented at point of use below): a real upstream bool/Boolean
# prototype-conflict bug in CFPriv.h (patched via third_party-patches/
# swift-corelibs-foundation.patch, applied by setup_third_party.sh); a
# real upstream TARGET_OS_OSX/TARGET_OS_MAC guard mismatch around
# __CFTSDInitialize (forward-declared via a compat header, not patched,
# since the real fix is "these two guards should agree" and forward-
# declaring is simpler+equally correct); DEPLOYMENT_RUNTIME_C=1 (not
# _SWIFT) + INCLUDE_OBJC=1 (this project has zero Swift/ObjC runtime,
# but real objc.h type-only declarations are still needed and available
# from the pinned iPhoneOS SDK) -- both match build.py's own real,
# alternate non-CMake build path (the actual CMake default assumes
# DEPLOYMENT_RUNTIME_SWIFT, which needs a Swift runtime this project
# doesn't have); the pinned iPhoneOS SDK's own real <mach/mach_vm.h>
# being "#error unsupported" (same real gap build_libmalloc.sh already
# found -- reused its libc_build/compat_include/mach/mach_vm.h, adding
# one more real declaration for mach_vm_region); and
# OS_LOG_SUBSYSTEM_RUNTIME_ISSUES missing from this SDK's older
# <os/log.h> (real value hand-supplied via a small compat header).
#
# Two more real gaps found in the 2026-08-27 full-sweep pass, both
# fixed via small additions to the existing compat headers (never a
# source patch): `<mach-o/dyld_priv.h>` (already stubbed empty for
# CFLocale.c's own vestigial include, see the ICU-port-era comment at
# point of use) needed one more real declaration,
# `dyld_image_path_containing_address`, for CFBundle_Binary.c's real
# dlfcn-based bundle-loading introspection; and CFSocket.c's
# `#include <libc.h>` (a real macOS-only umbrella header the iPhoneOS
# SDK never shipped) needed a small compat header re-including the
# handful of specific standard headers (`unistd.h`/`fcntl.h`/
# `string.h`/`stdlib.h`/`sys/socket.h`) it actually pulls symbols from.
set -e
cd "$(dirname "$0")"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi

CF="../../third_party/swift-corelibs-foundation/CoreFoundation"
XNU="../../third_party/xnu-7195"
DISPATCH="../../third_party/libdispatch"
PD="../../third_party/PureDarwin/src/Libraries/XPC"
CR="../../third_party/PureDarwin/src/Libraries/CrashReporterClient"
ICU="../../third_party/ICU/icuSources"
LIBC="../../third_party/Libc"
ICU_LIB="icu_build/target/lib"
OUT="libc_build"
COMPAT="cf_compat_headers"

if [ ! -d "$CF/Base.subproj" ]; then
    echo "error: $CF not found -- run setup_third_party.sh first" >&2
    exit 1
fi
if [ ! -f "$ICU_LIB/libicuuc.dylib" ]; then
    echo "error: $ICU_LIB/libicuuc.dylib not found -- run build_icu_host.sh then build_icu_target.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libxml2.dylib" ]; then
    echo "error: $OUT/system/libxml2.dylib not found -- run build_libxml2.sh first" >&2
    exit 1
fi

# Real header-staging step, replicating what CoreFoundationAddFramework
# .cmake's add_framework() does at build time for the real CMake build
# (copies scattered per-.subproj public/private headers into one flat
# CoreFoundation.framework/Headers/ dir) -- done here via symlinks
# instead of copies, and once per subproj dir (-maxdepth 2) rather than
# the full recursive tree, since deeper subdirs (icu/, SwiftRuntime/)
# hold platform variants this project doesn't use.
rm -rf "$OUT/cf_headers/CoreFoundation"
mkdir -p "$OUT/cf_headers/CoreFoundation"
find "$CF" -maxdepth 2 -iname "*.h" -exec ln -sf "$(pwd)/{}" "$OUT/cf_headers/CoreFoundation/" \;
# Real Darwin's own TargetConditionals.h, not this repo's own portability
# shim (Base.subproj/SwiftRuntime/TargetConditionals.h) -- this project
# targets real Darwin, not a cross-platform fallback.
ln -sf "$SDK/usr/include/TargetConditionals.h" "$OUT/cf_headers/CoreFoundation/TargetConditionals.h"

CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -O0 \
    -Wno-nullability-completeness -Wno-shorten-64-to-32 -Wno-deprecated-declarations -Wno-unreachable-code \
    -Wno-conditional-uninitialized -Wno-unused-variable -Wno-int-conversion -Wno-unused-function \
    -DDEPLOYMENT_TARGET_MACOSX=0 -DDEPLOYMENT_RUNTIME_OBJC=0 -DDEPLOYMENT_RUNTIME_C=1 -DINCLUDE_OBJC=1 \
    -DCF_BUILDING_CF=1 -DU_SHOW_DRAFT_API=1 -fconstant-cfstrings \
    -include "$CF/Base.subproj/CoreFoundation_Prefix.h" \
    -include "$CF/Base.subproj/ForSwiftFoundationOnly.h" \
    -include "$COMPAT/cf_tsd_forward_decl.h" \
    -include "$COMPAT/cf_os_log_compat.h" \
    -I "$COMPAT/libc_umbrella" \
    -I "$COMPAT" \
    -I "$CF/Base.subproj" -I "$CF" -I "$CF/Collections.subproj" -I "$CF/NumberDate.subproj" -I "$CF/String.subproj" \
    -I "$CF/Parsing.subproj" -I "$CF/StringEncodings.subproj" -I "$CF/Locale.subproj" -I "$CF/PlugIn.subproj" \
    -I "$CF/Error.subproj" -I "$CF/URL.subproj" -I "$CF/RunLoop.subproj" -I "$CF/Stream.subproj" \
    -I "$CF/Preferences.subproj" \
    -I "$ICU/common" -I "$ICU/i18n" \
    -idirafter "$LIBC/stdtime/FreeBSD" \
    -I "$OUT/cf_headers" -I "$DISPATCH" -I "$PD/libxpc/include" -I "$CR/include" \
    -idirafter "$XNU/libsyscall/wrappers/libproc" \
    -idirafter "$XNU/bsd" -idirafter "$XNU/osfmk" -idirafter "$XNU/libkern")

FILES=(Base.subproj/CFBase.c Base.subproj/CFRuntime.c Base.subproj/CFUtilities.c Base.subproj/CFUUID.c \
    Base.subproj/CFPlatform.c Base.subproj/CFKnownLocations.c Base.subproj/CFSortFunctions.c \
    Base.subproj/CFFileUtilities.c Base.subproj/CFSystemDirectories.c \
    Collections.subproj/CFArray.c Collections.subproj/CFDictionary.c Collections.subproj/CFData.c \
    Collections.subproj/CFBasicHash.c Collections.subproj/CFSet.c Collections.subproj/CFBag.c \
    Collections.subproj/CFBinaryHeap.c Collections.subproj/CFBitVector.c Collections.subproj/CFStorage.c \
    Collections.subproj/CFTree.c \
    NumberDate.subproj/CFNumber.c NumberDate.subproj/CFDate.c NumberDate.subproj/CFTimeZone.c \
    NumberDate.subproj/CFBigNumber.c \
    String.subproj/CFString.c String.subproj/CFStringEncodings.c String.subproj/CFStringUtilities.c \
    String.subproj/CFCharacterSet.c String.subproj/CFStringScanner.c String.subproj/CFBurstTrie.c \
    String.subproj/CFAttributedString.c String.subproj/CFRegularExpression.c String.subproj/CFRunArray.c \
    String.subproj/CFStringTransform.c \
    StringEncodings.subproj/CFUniChar.c StringEncodings.subproj/CFUnicodeDecomposition.c \
    StringEncodings.subproj/CFUnicodePrecomposition.c \
    StringEncodings.subproj/CFStringEncodingConverter.c StringEncodings.subproj/CFBuiltinConverters.c \
    StringEncodings.subproj/CFStringEncodingDatabase.c StringEncodings.subproj/CFICUConverters.c \
    StringEncodings.subproj/CFPlatformConverters.c \
    Parsing.subproj/CFPropertyList.c Parsing.subproj/CFBinaryPList.c Parsing.subproj/CFOldStylePList.c \
    Parsing.subproj/CFXMLInterface.c \
    Error.subproj/CFError.c URL.subproj/CFURL.c URL.subproj/CFURLComponents.c \
    URL.subproj/CFURLComponents_URIParser.c URL.subproj/CFURLAccess.c \
    Locale.subproj/CFLocale.c Locale.subproj/CFLocaleIdentifier.c Locale.subproj/CFLocaleKeys.c \
    Locale.subproj/CFNumberFormatter.c Locale.subproj/CFCalendar.c Locale.subproj/CFCalendar_Enumerate.c \
    Locale.subproj/CFDateFormatter.c Locale.subproj/CFDateComponents.c Locale.subproj/CFDateInterval.c \
    Locale.subproj/CFDateIntervalFormatter.c \
    RunLoop.subproj/CFRunLoop.c RunLoop.subproj/CFMachPort.c RunLoop.subproj/CFMachPort_Lifetime.c \
    RunLoop.subproj/CFMessagePort.c RunLoop.subproj/CFSocket.c \
    Stream.subproj/CFStream.c Stream.subproj/CFSocketStream.c Stream.subproj/CFConcreteStreams.c \
    PlugIn.subproj/CFBundle.c PlugIn.subproj/CFBundle_Binary.c PlugIn.subproj/CFBundle_DebugStrings.c \
    PlugIn.subproj/CFBundle_Executable.c PlugIn.subproj/CFBundle_Grok.c PlugIn.subproj/CFBundle_InfoPlist.c \
    PlugIn.subproj/CFBundle_Locale.c PlugIn.subproj/CFBundle_Main.c PlugIn.subproj/CFBundle_ResourceFork.c \
    PlugIn.subproj/CFBundle_Resources.c PlugIn.subproj/CFBundle_SplitFileName.c \
    PlugIn.subproj/CFBundle_Strings.c PlugIn.subproj/CFBundle_Tables.c PlugIn.subproj/CFPlugIn.c \
    Preferences.subproj/CFApplicationPreferences.c \
    Preferences.subproj/CFPreferences.c Preferences.subproj/CFXMLPreferencesDomain.c)

mkdir -p "$OUT/cf_obj"
for f in "${FILES[@]}"; do
    base=$(basename "$f" .c)
    "$CLANG" "${CFLAGS[@]}" "$CF/$f" -o "$OUT/cf_obj/$base.o"
done
# Real embedded Unicode data CFCharacterSet.c/CFUniChar.c need at
# runtime (property/bitmap tables, NOT ICU-generated -- see this
# script's own header comment): real Apple `.incbin` assembly
# (String.subproj/CFCharacterSetData.S/CFUniCharPropertyDatabase.S)
# wrapping the real binary blobs already checked into this fork
# (CharacterSets/CFCharacterSetBitmaps.bitmap/CFUniCharPropertyDatabase
# .data). CF_CHARACTERSET_BITMAP/CF_CHARACTERSET_UNICHAR_DB are the same
# defines build.py's own real build passes, just with an absolute path
# (robust regardless of this script's cwd, unlike build.py's
# cwd-relative one).
#
# DAR-253 correction (2026-09-06): these two objects' own `.incbin`
# symbols (__CFCharacterSetBitmapData/__CFUniCharPropertyDatabase) are
# NOT what CFUniChar.c's __CFUniCharLoadFile() actually reads on this
# TARGET_OS_MAC build -- that macro forces USE_MACHO_SEGMENT=1
# unconditionally (CFUniChar.c line ~44), which looks up real Mach-O
# `__UNICODE,__csbitmaps`/`__properties`/`__data` segment/section names
# instead (only the non-Darwin USE_RAW_SYMBOL path reads these two
# objects' symbols). Left compiled here anyway -- harmless, real data,
# just currently inert on this target -- rather than deleting, since
# the real fix (creating the actual `__UNICODE` segment at link time,
# matching swift-corelibs-foundation's own CMakeLists.txt Darwin
# `-sectcreate` recipe) belongs at the FINAL dylib link, not here; see
# build_corefoundation_dylib.sh's own header for the real root-cause
# writeup and fix.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    -I "$OUT/cf_headers" \
    -DCF_CHARACTERSET_BITMAP="\"$(cd "$CF" && pwd)/CharacterSets/CFCharacterSetBitmaps.bitmap\"" \
    "$CF/String.subproj/CFCharacterSetData.S" -o "$OUT/cf_obj/CFCharacterSetData.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    -I "$OUT/cf_headers" \
    -DCF_CHARACTERSET_UNICHAR_DB="\"$(cd "$CF" && pwd)/CharacterSets/CFUniCharPropertyDatabase.data\"" \
    "$CF/String.subproj/CFUniCharPropertyDatabase.S" -o "$OUT/cf_obj/CFUniCharPropertyDatabase.o"
# Real fortify/compiler-rt symbols the newly-added CFStringEncodingConverter.c/
# CFOldStylePList.c/CFCalendar.c chain calls (___memmove_chk/__sprintf_chk/
# __strlcat_chk/__strncpy_chk/__udivti3) -- same real files
# build_launchd_core.sh already uses for the identical real gap (Libc's own
# secure/*.c fortified-libc sources, udivti3_compat.c a real standard
# 128-bit division algorithm already checked into this directory), just
# compiled here too rather than sharing launchd_core's own object dir.
# -D_FORTIFY_SOURCE=0 on these secure/ compiles is load-bearing, not
# hygiene: without it the fortify inlines in <stdio.h>/<string.h> rewrite
# each __X_chk()'s own internal fall-through call to plain X() back into
# __builtin___X_chk(), i.e. into itself, unconditionally (the rewritten
# call passes __darwin_obsz(dest) == (size_t)-1 for a pointer parameter,
# and every secure/ file's guard treats "unbounded" as "call the plain
# function"). That is infinite recursion -> stack exhaustion -> SIGSEGV.
# This was the real root cause of launchd bug #6; see
# build_launchd_core.sh's fuller write-up above its own secure/ compiles.
"$CLANG" "${CFLAGS[@]}" -D_FORTIFY_SOURCE=0 "$LIBC/secure/memmove_chk.c" -o "$OUT/cf_obj/memmove_chk.o"
"$CLANG" "${CFLAGS[@]}" -D_FORTIFY_SOURCE=0 "$LIBC/secure/sprintf_chk.c" -o "$OUT/cf_obj/sprintf_chk.o"
"$CLANG" "${CFLAGS[@]}" -D_FORTIFY_SOURCE=0 "$LIBC/secure/strlcat_chk.c" -o "$OUT/cf_obj/strlcat_chk.o"
"$CLANG" "${CFLAGS[@]}" -D_FORTIFY_SOURCE=0 "$LIBC/secure/strncpy_chk.c" -o "$OUT/cf_obj/strncpy_chk.o"
"$CLANG" "${CFLAGS[@]}" udivti3_compat.c -o "$OUT/cf_obj/udivti3_compat.o"
# Real, minimal CFRuntimeClass stub descriptors for CF subsystems not
# ported yet (RunLoop/Stream/Socket/MachPort/URL/DateComponents/
# Formatters/AttributedString/Bundle/PlugIn/Preferences) -- see
# cf_compat_headers/cf_class_stubs.c's own header comment for the full
# rationale (safe because nothing in this project's real call graph
# instantiates these types yet; tracked as docs/backlog.md P2 d7).
"$CLANG" "${CFLAGS[@]}" "$COMPAT/cf_class_stubs.c" -o "$OUT/cf_obj/cf_class_stubs.o"
# Real, small, individually-verified compat gaps found via the full
# launchd critical-path trial link (docs/backlog.md item d6/d7):
# malloc_zone_*/malloc_default_zone/malloc_good_size (CFAllocator's own
# real default-allocator implementation calls these; this project's
# build_libmalloc.sh deliberately never built the real
# zone-registration API surface -- see malloc_zone_compat.c),
# _NSGetArgc (real Apple crt_externs.c's own one-line accessor,
# extracted standalone to avoid a duplicate-symbol conflict with this
# project's existing _NSGetArgv/etc -- see nsgetargc_compat.c).
#
# UPDATED 2026-08-27: fmod/modf/pow (CFNumber.c's __CFNumberHash(),
# CFUtilities.c's millisecond-logging path, CFString.c's
# scientific-notation threshold check) now come from libc_build/libm.a
# (real Apple Libm, tag Libm-2026 -- see setup_third_party.sh's own
# header comment and build_libm.sh) instead of cf_compat_headers/
# libm_compat.c's hand-written approximations -- linked in at the two
# trial-link steps below (both static and dynamic), not compiled into
# cf_obj/, since libm.a is a real archive of its own, not a per-file
# compat source. libm_compat.c itself is left in the tree (not deleted
# -- confirmed via grep that nothing else references it) but is no
# longer compiled into this tier; note real libm's pow() also handles
# fractional exponents, unlike libm_compat.c's own documented
# integer-exponent-only limitation, a strict correctness improvement
# for CFString.c's real call site.
"$CLANG" "${CFLAGS[@]}" "$COMPAT/malloc_zone_compat.c" -o "$OUT/cf_obj/malloc_zone_compat.o"
"$CLANG" "${CFLAGS[@]}" "$COMPAT/nsgetargc_compat.c" -o "$OUT/cf_obj/nsgetargc_compat.o"

# Real mach_vm_region() (CFUtilities.c's own __CFFindPointer() calls it
# directly, cf_compat_headers/mach/mach_vm.h already declares it for the
# compiler -- this closes the real symbol itself). Real Apple source has
# NO hand-written mach_vm_region() at all (unlike mach_vm_allocate/
# _deallocate/_map, which build_libmalloc.sh's own libmalloc.a already
# provides) -- it's a pure MIG client stub, auto-generated from the same
# real mach_vm.defs build_libmalloc.sh regenerates for a different
# reason (its own mach_vm_internal.h). Self-contained real regen here
# (not reused from build_libmalloc.sh's own intermediate output, to
# avoid an undocumented cross-script ordering dependency): real `mig`
# against the same real .defs file produces a whole mach_vmUser.c with
# ALL real mach_vm_* MIG routines, including real duplicate
# `_kernelrpc_`-unprefixed mach_vm_allocate/etc that WOULD conflict with
# libmalloc.a's own -- so only mach_vm_region is extracted via a real
# `ld -r -exported_symbol` partial link (confirmed via nm: this real
# routine has no separate `_kernelrpc_` fast-path variant in the real
# .defs file, unlike allocate/map/protect, so it doesn't collide).
MIG="$(which mig)"
if [ -z "$MIG" ]; then
    echo "error: mig tool not found (needed to regenerate mach_vm_region -- see build_libmalloc.sh's own header comment for the same real mig-path quirk)" >&2
    exit 1
fi
LD="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
mkdir -p "$OUT/cf_mach_vm_mig/mach"
MACH_VM_DEFS="$(cd "$(dirname "$XNU/osfmk/mach/mach_vm.defs")" && pwd)/$(basename "$XNU/osfmk/mach/mach_vm.defs")"
"$MIG" -arch arm64 \
    -header "$OUT/cf_mach_vm_mig/mach/mach_vm_internal.h" \
    -user "$OUT/cf_mach_vm_mig/mach_vmUser.c" -server "$OUT/cf_mach_vm_mig/mach_vmServer.c" -sheader "$OUT/cf_mach_vm_mig/mach_vmServerHeader.h" \
    "$MACH_VM_DEFS" >/dev/null
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -DPRIVATE=1 \
    -I "$OUT/cf_mach_vm_mig" -I "$OUT/cf_mach_vm_mig/mach" \
    -idirafter "$XNU/osfmk" -idirafter "$XNU/libkern" -idirafter "$XNU/bsd" \
    "$OUT/cf_mach_vm_mig/mach_vmUser.c" -o "$OUT/cf_mach_vm_mig/mach_vmUser.o"
"$LD" -r -arch arm64 -exported_symbol _mach_vm_region \
    "$OUT/cf_mach_vm_mig/mach_vmUser.o" -o "$OUT/cf_obj/mach_vm_region.o"
# NOTE: this real routine's own real _NDR_record dependency is already
# satisfied by libmalloc.a's own copy (real osfmk/mach/arm/ndr_def.h,
# confirmed via nm before assuming a second copy was needed -- adding
# one here would be a real duplicate-symbol conflict, not a fix).

# Real BSD socket raw syscall wrappers CFSocket.c/CFRunLoop.c need --
# see cf_socket_syscalls.c's own header comment for the full real
# syscall-number cross-check and this project's established
# "each consumer gets its own small hand-rolled wrapper file" convention
# (ifconfig_extra_syscalls.c/libresolv_socket_syscalls.c are the closest
# analogs). 12 real symbols: socket/bind/listen/accept/connect/
# getsockname/getsockopt/setsockopt/socketpair/recvfrom/sendto/recv.
"$CLANG" "${CFLAGS[@]}" "$COMPAT/cf_socket_syscalls.c" -o "$OUT/cf_obj/cf_socket_syscalls.o"

# Real mach_port_* client wrappers CFMachPort.c/CFRunLoop.c need -- see
# cf_mach_port_syscalls.c's own header comment for the full real
# extraction rationale (7 functions copied verbatim from
# libsyscall/mach/mach_port.c, each citing its real origin line, rather
# than compiling that whole file -- its OTHER functions pull in a
# genuinely different, unneeded MIG subsystem, mach_voucher.defs).
CFLAGS_MACH_PORT=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I "$XNU/libsyscall/mach" -I "$XNU/libsyscall/os" -idirafter "$XNU/osfmk" \
    -I "$LIBC/include" -I "$LIBC/darwin")
"$CLANG" "${CFLAGS_MACH_PORT[@]}" cf_mach_port_syscalls.c -o "$OUT/cf_obj/cf_mach_port_syscalls.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c mach_port_traps.s -o "$OUT/cf_obj/mach_port_traps.o"
# Real, honest stub for the MIG-fallback half of the above (the trap
# entry points these fall back FROM are all confirmed backed by a real
# kernel-side implementation, osfmk/ipc/mach_kernelrpc.c -- so this path
# is dead code on a real working boot). Real mach_msg()/MIG marshaling
# needs this project's still-unbuilt real Mach IPC (docs/backlog.md
# item 7); see cf_mach_port_mig_fallback_stub.c's own header comment for
# the full reasoning and the real fix once that lands.
"$CLANG" "${CFLAGS[@]}" cf_mach_port_mig_fallback_stub.c -o "$OUT/cf_obj/cf_mach_port_mig_fallback_stub.o"

# Real mk_timer_* Mach trap stubs CFRunLoop.c's timer sources need --
# plain traps, no MIG fallback at all (see mk_timer_traps.s's own header
# comment).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c mk_timer_traps.s -o "$OUT/cf_obj/mk_timer_traps.o"

# Real pthread_attr_* functions CFRunLoop.c/CFMachPort.c call -- see
# cf_pthread_attr_syscalls.c's own header comment for the full real
# extraction rationale (4 functions copied verbatim from
# third_party/libpthread/src/pthread.c, same "extract just what's
# needed" pattern as cf_mach_port_syscalls.c, since that whole file
# hits a real header conflict elsewhere -- build_libpthread.sh's own
# header comment has the story).
PTHREAD="$XNU/../libpthread"
PLATFORM="$XNU/../libplatform"
MALLOC="$XNU/../libmalloc"
CFLAGS_PTHREAD_ATTR=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -Wno-availability -Wno-deprecated-declarations \
    -D__LIBC__ -D__POSIX_LIB__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_NON_CANCELABLE=1 \
    -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 -D__PTHREAD_BUILDING_PTHREAD__=1 -D__PTHREAD_EXPOSE_INTERNALS__ \
    -DOS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY=1 -DPRIVATE=1 \
    -D_os_xbs_chrooted=0 \
    '-D__API_UNAVAILABLE_PLATFORM_bridgeos=ios,unavailable' \
    '-D__API_AVAILABLE_PLATFORM_bridgeos(x)=ios,introduced=x' \
    -I "$PTHREAD" -I "$PTHREAD/src" -I "$PTHREAD/src/resolver" -I "$PTHREAD/private" -I "$PTHREAD/include" -I "$PTHREAD/kern" \
    -I "$PLATFORM/internal" -I "$PLATFORM/private" -I "$PLATFORM/include" \
    -I "$OUT/compat_include" \
    -I "$MALLOC/private" \
    -I "$XNU/libsyscall" -I "$XNU/libsyscall/mach" -I "$XNU/bsd" \
    -idirafter "$XNU/osfmk" -idirafter "$XNU/libkern")
"$CLANG" "${CFLAGS_PTHREAD_ATTR[@]}" cf_pthread_attr_syscalls.c -o "$OUT/cf_obj/cf_pthread_attr_syscalls.o"

# Real __proc_info raw syscall + proc_pidinfo()/pthread_setname_np() --
# see cf_libproc_syscalls.c's and cf_pthread_setname_syscalls.c's own
# header comments for the full real extraction rationale (both closing
# docs/backlog.md item d7's own "need one more real syscall" note, a
# real twofer since both live off the same __proc_info trap). The trap
# itself is generated fresh here, same real create-syscalls.pl recipe
# build_libxpc_syscalls.sh already established for this exact symbol
# (real xnu-7195 syscalls.master, not hand-guessed).
CF_SYSCALL_GEN="$(mktemp -d)"
trap 'rm -rf "$CF_SYSCALL_GEN"' EXIT
ARCHS=arm64 perl "$XNU/libsyscall/xcodescripts/create-syscalls.pl" \
    "$XNU/bsd/kern/syscalls.master" \
    "$XNU/libsyscall/custom" \
    "$XNU/libsyscall/Platforms" \
    iPhoneOS \
    "$CF_SYSCALL_GEN" >/dev/null
mkdir -p "$OUT/cf_sys_asm_compat/mach/arm64"
cat > "$OUT/cf_sys_asm_compat/mach/arm64/asm.h" <<'EOF'
#include <arm64/asm.h>
EOF
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -arch arm64 \
    -I "$OUT/cf_sys_asm_compat" -I "$XNU/libsyscall/custom" -I "$XNU/libsyscall/mach" -I "$XNU/osfmk" \
    "$CF_SYSCALL_GEN/___proc_info.s" -o "$OUT/cf_obj/proc_info_trap.o"
rm -rf "$CF_SYSCALL_GEN"
trap - EXIT
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -O0 \
    cf_libproc_syscalls.c -o "$OUT/cf_obj/cf_libproc_syscalls.o"
"$CLANG" "${CFLAGS_PTHREAD_ATTR[@]}" cf_pthread_setname_syscalls.c -o "$OUT/cf_obj/cf_pthread_setname_syscalls.o"

# Real NXFindBestFatArch()/NXGetLocalArchInfo() CFBundle_Grok.c/
# CFBundle_Binary.c need -- see setup_third_party.sh's own header comment
# for the full real cctools-973.0.1 pin rationale (not part of the
# macos-112 manifest; matched instead against this project's own pinned
# Xcode 12's real `otool --version`). Real, unmodified
# third_party/cctools/libmacho/arch.c, confirmed by a real trial compile
# to need nothing this project doesn't already have (host_info/
# mach_host_self from libmach_ipc.a, mach_port_deallocate/
# mach_task_self_ from libmalloc.a).
CCTOOLS="../../third_party/cctools"
if [ ! -f "$CCTOOLS/libmacho/arch.c" ]; then
    echo "error: $CCTOOLS/libmacho/arch.c not found -- run setup_third_party.sh first" >&2
    exit 1
fi
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -O0 \
    -I "$CCTOOLS/include" -I "$CCTOOLS" \
    "$CCTOOLS/libmacho/arch.c" -o "$OUT/cf_obj/cctools_arch.o"

# DAR-252: real, from-scratch CFFileDescriptor implementation --
# CFFileDescriptor.c has never been open-sourced by Apple (confirmed
# against apple-oss-distributions/CF's real tree, not just this fork's
# swift-corelibs-foundation), but the real public API/header IS shipped
# in every real SDK, so this is a real implementation of a real,
# documented contract, not a guess -- see the file's own header comment
# for the full writeup (modeled on this exact codebase's own real
# CFSocket.c manager-thread mechanism). Compiled with the same CFLAGS
# as every real CF file above so it shares CFRuntime/CFInternal
# internals, but lives in this project's own tree (not the vendored
# fork) since there is no real Apple source to port -- same convention
# as configd_plugin_sha256_compat.c and friends.
"$CLANG" "${CFLAGS[@]}" "cf_filedescriptor_compat.c" \
    -o "$OUT/cf_obj/cf_filedescriptor_compat.o"

# DAR-300: CFURLResourceIsReachable / CFURLCopyResourcePropertyForKey /
# kCFURLFileSizeKey -- three real, public CF URL APIs with no native-C
# body anywhere in the pinned swift-corelibs-foundation tree (two are
# DEPLOYMENT_RUNTIME_SWIFT-only, one has no implementation at all). They
# used to live outside CF entirely: two deferred out of
# libSystemConfiguration.dylib with `-Wl,-U` and defined separately
# inside configd.macho/ipconfig.macho/scutil.macho, one compiled into
# libSystemConfiguration.dylib itself. Both workarounds are deleted now
# that they live where real Apple puts them. Same "no Apple source to
# port, so it lives in this project's own tree" convention as
# cf_filedescriptor_compat.c above; see the file's own header comment.
"$CLANG" "${CFLAGS[@]}" "cf_cfurl_resource_compat.c" \
    -o "$OUT/cf_obj/cf_cfurl_resource_compat.o"

echo "compiled: $((${#FILES[@]} + 24)) real/compat CoreFoundation source files (zero errors)"

rm -f "$OUT/libCoreFoundation.a"
ar rcs "$OUT/libCoreFoundation.a" "$OUT"/cf_obj/*.o
echo "wrote $OUT/libCoreFoundation.a ($(ls "$OUT"/cf_obj/*.o | wc -l | tr -d ' ') objects)"

# DAR-195 (2026-09-03): the STATIC and DYNAMIC -force_load trial links
# that used to live here (linking libCoreFoundation.a whole into a
# throwaway cf_trial.macho/cf_trial_dyn.macho to print an undefined-
# symbol gap count) are REMOVED, not just moved -- they were this
# script's own real remaining static-archive "consumer" per DAR-195's
# own 6-item list, and closing that list is the point of this ticket.
# They did real, useful work during CF's active porting phase
# (2026-08-26/27, gap count 183->109 etc, see docs/backlog.md item d7)
# but that job is superseded now by two real, better checks:
#   - build_corefoundation_dylib.sh's own duplicate-symbol pre-audit
#     (checks every member object against the real sibling-dylib union
#     before linking, not just "does *a* link close")
#   - build_corefoundation_dylib_test.sh's cf_dylib_smoketest.macho,
#     which links against the real, already-built libCoreFoundation.dylib
#     (not this archive) and is real-QEMU-boot-verified, exercising
#     __CFInitialize's own constructor at real dyld load time -- a
#     strictly stronger proof than "an -force_load link closes".
# Re-adding an -force_load trial link here would just reintroduce the
# exact static-consumer this ticket exists to remove: this script's own
# job ends at producing libCoreFoundation.a as raw material for
# build_corefoundation_dylib.sh's `ar x` extraction step, never linking
# it whole into any executable of its own. See Linear DAR-195.
echo "libCoreFoundation.a: DAR-195 -- no longer test-linked here, see build_corefoundation_dylib.sh / build_corefoundation_dylib_test.sh for real link verification"
