#!/bin/bash
# Stage 1 of the real ICU port (docs/backlog.md item d7's "ICU wall" --
# CFCharacterSet/CFUniChar/CFStringUtilities collation/CFLocale/
# CFNumberFormatter in the CoreFoundation port). third_party/ICU (pinned
# ICU-66109, see setup_third_party.sh's own header comment for the
# macos-112 manifest confirmation) is Apple's real, unmodified ICU4C fork:
# icuSources/ is stock upstream ICU4C with its own real autotools
# configure/make build system, not this project's usual from-scratch
# clang-per-file-loop template (see build_libc_regex.sh) -- ICU generates
# its locale/charset/normalization data tables and builds host-native
# tools (genrb, gencnv, gennorm2, icupkg, ...) as part of its own build,
# so reinventing that by hand would mean reimplementing autotools badly.
#
# Confirmed via icuSources/configure.ac: upstream ICU4C's real two-stage
# cross-compile path is `--with-cross-build=<dir>`, where <dir> is the
# build root of an ICU already built *for the host running the build*
# (it needs to actually run genrb/gencnv/etc. to produce data during its
# own build, which only works with host-native binaries). This script is
# that first stage only: a plain native macOS build of ICU, producing
# icu_build/host/config/icucross.mk + the host tool binaries stage 2
# (not yet written -- the real arm64-apple-ios14.4 cross build via
# --with-cross-build=../icu_build/host, using this project's usual
# Xcode-12 iPhoneOS SDK/-ffreestanding recipe for CC/CXX) will point at.
# Not itself used by the kernel/userland target -- this build's outputs
# never get linked into anything that ships.
#
# Real gap found and fixed here: a plain `runConfigureICU MacOSX && make`
# fails to compile common/cmemory.h ("no template named 'LocalPointerBase'")
# on any current host clang. Root cause (confirmed in
# common/unicode/utypes.h): Apple's own fork flips ICU's normal upstream
# default of `U_SHOW_CPLUSPLUS_API` from 1 to 0 (deliberately -- Apple's
# comment there says client code linking the shipped OS dylib must not
# rely on ICU's C++ API, only the plain-C API is ABI-stable), but ICU's
# own internal .cpp files (cmemory.h's LocalMemory<T> among others) are
# real C++-API consumers and need it back on to build the library itself.
# Confirmed via `ICU.xcodeproj/project.pbxproj`: Apple's own real internal
# build passes `U_SHOW_CPLUSPLUS_API=1` as a preprocessor define when
# building ICU -- the generic autotools path just doesn't know to. Not a
# source patch; passing it through CPPFLAGS is the minimal real fix,
# matching Apple's own recipe.
set -e
cd "$(dirname "$0")"

ICU_SRC="$(cd ../../third_party/ICU/icuSources && pwd)"
OUT="$(pwd)/icu_build/host"

if [ ! -x "$ICU_SRC/runConfigureICU" ]; then
    echo "error: $ICU_SRC/runConfigureICU not found -- run setup_third_party.sh first" >&2
    exit 1
fi

GMAKE=$(command -v gmake || true)
if [ -z "$GMAKE" ]; then
    echo "error: gmake (GNU make) not found -- ICU's own runConfigureICU requires it (BSD /usr/bin/make is not sufficient); install via 'brew install make'" >&2
    exit 1
fi

mkdir -p "$OUT"
cd "$OUT"

if [ ! -f config.status ]; then
    echo "+ configuring ICU for host (MacOSX)"
    # --disable-extras/--disable-tests/--disable-samples: this build's only
    # real goal is a --with-cross-build reference root (config/icucross.mk
    # + host-native tools/data for stage 2's target build to invoke) --
    # extra/'s sample CLI tools (uconv et al) and the test suite are not
    # part of that and not needed. Confirmed real, separate gap found
    # 2026-08-26: extra/uconv/uconv.cpp doesn't compile on any current
    # host clang (UChar/char16_t pointer-type mismatch, an ICU-66-era
    # source vs. modern-libc++ incompatibility unrelated to this
    # project) -- disabling extras sidesteps a real but irrelevant gap
    # rather than fixing dead-to-this-project sample code.
    CPPFLAGS="-DU_SHOW_CPLUSPLUS_API=1" "$ICU_SRC/runConfigureICU" MacOSX \
        --disable-extras --disable-tests --disable-samples
else
    echo "= already configured (found config.status), skipping runConfigureICU"
fi

NCPU=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
echo "+ building ($GMAKE -j$NCPU) -- this is a full native ICU build, expect several minutes"
"$GMAKE" -j"$NCPU"

if [ -f config/icucross.mk ]; then
    echo "OK: host build complete, config/icucross.mk present -- usable as --with-cross-build target for stage 2"
else
    echo "error: build finished but config/icucross.mk is missing -- something is wrong with this build root" >&2
    exit 1
fi
