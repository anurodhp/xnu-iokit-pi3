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
# This is NOT a full CoreFoundation build. It is the minimal real
# subset needed to close the specific undefined symbols launchd/core.c
# and IOKitUser actually call: CFArray/CFDictionary/CFData/CFBasicHash/
# CFSet/CFBag (Collections.subproj), CFNumber+CFBoolean (NumberDate.
# subproj), CFString (String.subproj), CFPropertyList+CFBinaryPList
# (Parsing.subproj -- XML plist parsing, i.e. CFPropertyListCreateFrom
# XMLData, is explicitly NOT included; only the binary-plist + basic
# CFPropertyList API surface these two files provide), plus CFBase/
# CFRuntime/CFUtilities/CFUUID/CFPlatform/CFKnownLocations/
# CFSortFunctions (Base.subproj) those all transitively need. Real,
# unmodified upstream source for all 17 files -- zero fabricated stubs.
#
# NOT INCLUDED (real, separate subprojs this project doesn't need yet):
# RunLoop, Stream, PlugIn (bundles), Locale/NumberFormatter/
# DateFormatter (real ICU dependency, out of scope), URL, Preferences,
# Error, CharacterSet's Unicode data tables (see CFUniChar.c / the
# CFCharacterSetBitmaps.bitmap/CFUniCharPropertyDatabase.data sectcreate
# data build.py wires in for full builds -- NOT done here).
#
# Real fixes discovered getting these 17 files to compile (all
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
OUT="libc_build"
COMPAT="cf_compat_headers"

if [ ! -d "$CF/Base.subproj" ]; then
    echo "error: $CF not found -- run setup_third_party.sh first" >&2
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
    -I "$COMPAT" \
    -I "$CF/Base.subproj" -I "$CF" -I "$CF/Collections.subproj" -I "$CF/NumberDate.subproj" -I "$CF/String.subproj" \
    -I "$CF/Parsing.subproj" -I "$CF/StringEncodings.subproj" -I "$CF/Locale.subproj" -I "$CF/PlugIn.subproj" \
    -I "$CF/Error.subproj" \
    -I "$OUT/cf_headers" -I "$DISPATCH" -I "$PD/libxpc/include" -I "$CR/include" \
    -idirafter "$XNU/libsyscall/wrappers/libproc" \
    -idirafter "$XNU/bsd" -idirafter "$XNU/osfmk" -idirafter "$XNU/libkern")

FILES=(Base.subproj/CFBase.c Base.subproj/CFRuntime.c Base.subproj/CFUtilities.c Base.subproj/CFUUID.c \
    Base.subproj/CFPlatform.c Base.subproj/CFKnownLocations.c Base.subproj/CFSortFunctions.c \
    Collections.subproj/CFArray.c Collections.subproj/CFDictionary.c Collections.subproj/CFData.c \
    Collections.subproj/CFBasicHash.c Collections.subproj/CFSet.c Collections.subproj/CFBag.c \
    Collections.subproj/CFBinaryHeap.c Collections.subproj/CFBitVector.c Collections.subproj/CFStorage.c \
    Collections.subproj/CFTree.c \
    NumberDate.subproj/CFNumber.c String.subproj/CFString.c \
    Parsing.subproj/CFPropertyList.c Parsing.subproj/CFBinaryPList.c \
    Error.subproj/CFError.c)

mkdir -p "$OUT/cf_obj"
for f in "${FILES[@]}"; do
    base=$(basename "$f" .c)
    "$CLANG" "${CFLAGS[@]}" "$CF/$f" -o "$OUT/cf_obj/$base.o"
done
# Real, minimal CFRuntimeClass stub descriptors for CF subsystems not
# ported yet (RunLoop/Stream/Socket/MachPort/URL/Locale/Calendar/
# TimeZone/Date family/Formatters/CharacterSet/AttributedString/Bundle/
# PlugIn/Preferences/Error) plus two init-hook no-ops -- see
# cf_compat_headers/cf_class_stubs.c's own header comment for the full
# rationale (safe because nothing in this project's real call graph
# instantiates these types yet; tracked as docs/backlog.md P2 d7).
"$CLANG" "${CFLAGS[@]}" "$COMPAT/cf_class_stubs.c" -o "$OUT/cf_obj/cf_class_stubs.o"
echo "compiled: $((${#FILES[@]} + 1)) real/compat CoreFoundation source files (zero errors)"

rm -f "$OUT/libCoreFoundation.a"
ar rcs "$OUT/libCoreFoundation.a" "$OUT"/cf_obj/*.o
echo "wrote $OUT/libCoreFoundation.a ($(ls "$OUT"/cf_obj/*.o | wc -l | tr -d ' ') objects)"

# Real, informational trial link: prove this archive's own real content
# links against this project's existing curated stack (libdispatch as a
# real per-library .dylib per this project's 2026-08-25 dylib-first
# directive, everything else still the pre-existing static archives).
# -force_load again, same "empty reference list proves nothing" reason
# as build_launchd_core.sh's own trial link.
cat > "$OUT/cf_trial_main.c" <<'EOF'
#include <CoreFoundation/CFArray.h>
int main(void) {
    CFArrayRef a = CFArrayCreate(0, 0, 0, 0);
    return (int)CFArrayGetCount(a);
}
EOF
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -O0 \
    -I "$OUT/cf_headers" -idirafter "$XNU/libkern" -o "$OUT/cf_trial_main.o" "$OUT/cf_trial_main.c"

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -o "$OUT/cf_trial.macho" \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    -Wl,-force_load,"$OUT/libCoreFoundation.a" \
    "$OUT/cf_trial_main.o" \
    "$OUT/system/libdispatch.dylib" \
    "$OUT/libmach_ipc.a" "$OUT/libclosure.a" "$OUT/libmalloc.a" "$OUT/libplatform.a" \
    "$OUT/libpthread.a" "$OUT/libc_gen.a" "$OUT/libc_string.a" "$OUT/libc_stdio.a" "$OUT/libc_printf.a" \
    "$OUT/libc_locale_for_stdio.a" \
    2>"$OUT/cf_trial_link.err" \
    && echo "real STATIC trial link: 0 undefined symbols -- fully closed" \
    || { n=$(grep -c 'Undefined symbol' "$OUT/cf_trial_link.err" || true); echo "real STATIC trial link found $n undefined symbol group(s) -- NOTE some of these (dispatch_*/os_log_*/__os_alloc_once* etc) are a link-mode artifact, not real gaps, see the dynamic trial link below and docs/backlog.md item d7:"; grep '^  "' "$OUT/cf_trial_link.err" | sed 's/,.*//' | sort -u; }

# Real, informational DYNAMIC trial link -- real ld64 cannot pull
# symbols from a real MH_DYLIB (libdispatch.dylib) into a fully static
# link at all, which made the static trial link above over-report
# dispatch_*/os_log_*/__os_alloc_once* etc as gaps when they're actually
# fine (found+documented 2026-08-26, docs/backlog.md item d7). Same real
# recipe build_dyld_test_exec.sh already established: drop `-static`
# (keep `-nostdlib`), link plain dylibs on the command line, real
# `-Wl,-fixup_chains` (this project's pinned dyld-940 needs chained
# fixups, not the older bind-opcode format). This is the more accurate
# gap count going forward -- prefer this one's output over the static
# link's above.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -Wl,-fixup_chains \
    -o "$OUT/cf_trial_dyn.macho" \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT/cf_trial_main.o" \
    "$OUT/libCoreFoundation.a" \
    "$OUT/system/libdispatch.dylib" \
    "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib" \
    2>"$OUT/cf_trial_dyn.err" \
    && echo "real DYNAMIC trial link: 0 undefined symbols -- fully closed" \
    || { n=$(grep -c 'Undefined symbol' "$OUT/cf_trial_dyn.err" || true); echo "real DYNAMIC trial link found $n undefined symbol group(s) -- the accurate current gap count:"; grep '^  "' "$OUT/cf_trial_dyn.err" | sed 's/,.*//' | sort -u; }
