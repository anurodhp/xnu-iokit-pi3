#!/bin/bash
# Stage 2 of the real ICU port (see build_icu_host.sh's own header comment
# for stage 1, and docs/backlog.md item d7's "Real ICU port started" entry
# for the full writeup). This is the real target build: cross-compiles
# ICU4C for arm64-apple-ios14.4 (this project's usual Xcode-12 iPhoneOS
# SDK target) using ICU4C's own real, upstream `--with-cross-build=<dir>`
# two-stage cross-compile mechanism, pointed at build_icu_host.sh's
# output (needed because generating locale/charset data and running
# code-generator tools like genrb/gencnv only works with host-native
# binaries -- they can't run on this build machine for the target arch).
#
# config/mh-darwin (ICU's Darwin platform makefile fragment) is already
# generic Darwin, not macOS-specific (confirmed by reading it -- no
# macOS-only assumptions, just -dynamiclib/DYLD_LIBRARY_PATH/etc that
# apply equally to any Darwin-family target), so no new platform
# fragment is needed, just real cross CC/CXX/CPPFLAGS.
#
# Real gap found and fixed here: this project's usual `-ffreestanding`
# flag (every other build_*.sh in this directory uses it) breaks ICU's
# own `./configure` outright. Root cause, confirmed with a standalone
# repro: per the C++ standard, `-ffreestanding` tells the compiler
# `main()` has no special significance (a freestanding implementation
# doesn't require one), so clang++ mangles it as an ordinary function
# (`__Z4mainv`) instead of emitting the unmangled `_main` a real linked
# executable's entry stub expects. This project's other real per-file-
# loop libraries never hit this because they either compile pure C
# (no such special-casing exists in C) or never ask a build system to
# link a full standalone "int main()" test executable -- but ICU's
# `configure` does exactly that repeatedly, for its own compiler-sanity
# checks (AC_PROG_CXX, the namespace-support probe, etc), all of which
# fail to link with "_main ... referenced ... implicit entry/start" once
# `main` silently becomes `_Z4mainv`. `-fno-builtin`/`-fno-stack-protector`
# alone don't cause this (confirmed) -- only `-ffreestanding` does, so
# this build simply drops it and keeps the other two, for both the
# configure-time sanity checks and the real library compiles: samples/
# tests/extras (the only real ICU components that ever link a full
# executable) are already disabled above, so nothing in the actual
# target build needs `-ffreestanding`'s freestanding-main semantics.
#
# `--disable-tools`: this target build's only real goal is the runtime
# libraries (libicuuc/libicui18n/libicudata); `tools/pkgdata.cpp` calls
# the real `system()` libc function, which the iPhoneOS SDK marks
# `__IOS_PROHIBITED` (a hard unavailable-on-iOS error, not just a
# warning) -- and there's no real reason to fix that for target-arch
# copies of build-time-only tool binaries that would never run on this
# project's actual boot target anyway. Confirmed safe: `data/Makefile.in`
# invokes `$(TOOLBINDIR)/pkgdata` for real data packaging, and
# `config/icucross.mk` (stage 1's output) already points `TOOLBINDIR` at
# the host build's own real, already-working pkgdata -- this target
# build was never going to use its own copy regardless.
set -e
cd "$(dirname "$0")"

ICU_SRC="$(cd ../../third_party/ICU/icuSources && pwd)"
HOST_BUILD="$(pwd)/icu_build/host"
OUT="$(pwd)/icu_build/target"

if [ ! -f "$HOST_BUILD/config/icucross.mk" ]; then
    echo "error: $HOST_BUILD/config/icucross.mk not found -- run build_icu_host.sh first" >&2
    exit 1
fi

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
CLANGXX="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

GMAKE=$(command -v gmake || true)
if [ -z "$GMAKE" ]; then
    echo "error: gmake (GNU make) not found -- install via 'brew install make'" >&2
    exit 1
fi

TARGET_FLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -fno-builtin -fno-stack-protector"

# Real gap found booting launchd as PID 1 (docs/backlog.md item d10):
# ICU's own real autoconf/gmake build never passed -Wl,-fixup_chains,
# unlike every other real dylib in this project (all hand-linked via a
# single clang invocation that always includes it) -- so ICU's dylibs
# link with the older, classic LC_DYLD_INFO lazy-binding format, which
# needs a real `dyld_stub_binder` this project's dyld doesn't provide
# (every other dylib here sidesteps this entirely via chained fixups,
# which bind everything eagerly, no lazy stub/binder needed at all).
# Same real ld64-609-crashes-under-fixup_chains issue as everywhere else
# in this project (docs/backlog.md item d9) applies here too -- LDFLAGS
# routes both -B (the daily-driver Xcode's newer ld) and -fixup_chains
# itself to ICU's own real link steps (autoconf-generated Makefiles
# honor LDFLAGS for the final .dylib link).
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi
ICU_LDFLAGS="-B$NEWLD_BINDIR -Wl,-fixup_chains"

mkdir -p "$OUT"
cd "$OUT"

if [ ! -f config.status ]; then
    echo "+ configuring ICU for target (arm64-apple-ios14.4, cross build against $HOST_BUILD)"
    CC="$CLANG $TARGET_FLAGS" CXX="$CLANGXX $TARGET_FLAGS" \
    CPPFLAGS="-DU_SHOW_CPLUSPLUS_API=1" LDFLAGS="$ICU_LDFLAGS" \
        "$ICU_SRC/runConfigureICU" MacOSX \
        --host=arm64-apple-darwin --with-cross-build="$HOST_BUILD" \
        --disable-extras --disable-tests --disable-samples --disable-tools
else
    echo "= already configured (found config.status), skipping runConfigureICU"
fi

# Real gap found and fixed here: `--disable-tools` above turned out to
# also disable the real `data` subdir entirely -- confirmed in
# Makefile.in: `@TOOLS_TRUE@DATASUBDIR = data` ties data packaging to the
# same TOOLS conditional as the target-arch tool binaries, even though
# `data/Makefile.in`'s own real packaging rule (`$(TOOLBINDIR)/pkgdata`)
# already resolves through `config/icucross.mk`'s `TOOLBINDIR` to the
# host build's tools regardless -- this project's own cross build never
# needed local target-arch tools for data packaging to begin with, so
# this coupling is a real ICU build-system gap for the cross-compile
# case specifically (not something upstream needed to solve, since Apple
# never builds pkgdata for an iOS device target in their public source
# drops). Fixed by uncommenting the generated (not the checked-in
# Makefile.in template) top-level Makefile's DATASUBDIR line directly --
# scoped to this one build tree, not filed as a general ICU.patch change,
# since it's specific to combining --disable-tools with
# --with-cross-build, not a general upstream inconsistency.
sed -i '' 's/^#DATASUBDIR = data$/DATASUBDIR = data/' Makefile

NCPU=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
echo "+ building ($GMAKE -j$NCPU)"
"$GMAKE" -j"$NCPU"

echo "OK: target build complete"
ls -la lib/ 2>/dev/null || true
