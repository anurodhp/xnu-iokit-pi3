#!/bin/bash
# Builds gnustep-make and gnustep-base (DAR-388) for arm64 Darwin on
# Apple's objc4 runtime: gnustep-make's apple-gnu-gnu library combo
# (DAR-382 chose objc4; see build_gnustep_objc4_smoketest.sh's header).
#
# Target layout: gnustep-make's "gnustep" layout under /usr/GNUstep, with
# the config file at /etc/GNUstep/GNUstep.conf. Both packages install
# with DESTDIR into libc_build/gnustep/root, a copy of the target's tree
# that inject_into_sd_image.sh copies onto the image. Because the prefix
# is the target path, libgnustep-base's install name and the paths it
# compiles in are the ones the target uses
# (--enable-absolute-install-paths).
#
#   1. gnustep-make (third_party/gnustep-make) is configured, built and
#      installed into root/.
#   2. gnustep-base (third_party/gnustep-base, the anurodhp/libs-base
#      iokit-port fork) is configured as a cross build, built and
#      installed into root/. The answers to configure's run tests come
#      from x11_config/gnustep/cross-config-arm64-darwin, which
#      build_gnustep_config_probes.sh --run produced by running the
#      probes on the target.
#
# Linking: -nostdlib against this project's own dylibs, owners listed
# first, with no -undefined dynamic_lookup (gnustep-make's darwin
# default). That way a symbol missing on the target fails the link here,
# and every two-level bind names the dylib that owns the symbol. Checked
# at the end by bind_target_audit.sh.
#
# Both trees are copied into libc_build/gnustep/{make,base}-src first,
# because both packages configure and build in-tree and would otherwise
# dirty third_party/.
#
#   build_gnustep_base.sh           # full build
#   GNUSTEP_BASE_ONLY=1 build_...   # skip step 1 (gnustep-make already in root/)
set -euo pipefail
cd "$(dirname "$0")"
HERE="$(pwd -P)"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
[ -x "$CLANG" ] && [ -d "$SDK" ] || { echo "error: Xcode-12.app not found at $XCODE12" >&2; exit 1; }
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
[ -x "$NEWLD_BINDIR/ld" ] || { echo "error: no usable 'ld' under $DAILY_XCODE" >&2; exit 1; }
OUT="$(cd "$HERE/libc_build" && pwd -P)"
G="$OUT/gnustep"
ROOT="$G/root"
PREFIX=/usr/GNUstep
CONF=/etc/GNUstep/GNUstep.conf
GM_SRC="$HERE/../../third_party/gnustep-make"
GB_SRC="$HERE/../../third_party/gnustep-base"
CROSS="${GNUSTEP_CROSS_CONFIG:-$HERE/x11_config/gnustep/cross-config-arm64-darwin}"
SYS="$OUT/system"
# Owners first (bind_target_audit.sh records each bind against the first
# listed dylib that exports the symbol), libSystem.B last.
SYSLIBS=("$SYS/libsystem_kernel.dylib" "$SYS/libsystem_platform.dylib" "$SYS/libsystem_pthread.dylib"
         "$SYS/libsystem_malloc.dylib" "$SYS/libsystem_c.dylib" "$SYS/libsystem_m.dylib"
         "$SYS/libsystem_blocks.dylib" "$SYS/libdispatch.dylib" "$SYS/libdyld.dylib"
         "$SYS/libc++.dylib" "$SYS/libunwind.dylib" "$OUT/libobjc.A.dylib"
         "$SYS/libffi.dylib" "$SYS/libiconv.dylib" "$SYS/libz.dylib"
         `# getpwnam/getaddrinfo (libinfo) and inet_ntop/inet_pton/getifaddrs` \
         `# (libresolv) are not in libSystem.B's reexports here; ping and` \
         `# ifconfig link these two the same way (DAR-182).` \
         "$SYS/libinfo.dylib" "$SYS/libresolv.dylib" "$SYS/libSystem.B.dylib")
for f in "$GM_SRC/configure" "$GB_SRC/configure" "$CROSS" "$OUT/objc4_public_headers/objc/runtime.h" \
         "$OUT/libffi_include/ffi.h" "${SYSLIBS[@]}"; do
    [ -e "$f" ] || { echo "error: $f not found" >&2; exit 1; }
done
TARGET=(-isysroot "$SDK" -target arm64-apple-ios14.4)
# Every dylib gets a -l name in LINKDIR, so -lobjc/-lffi/-liconv/-lz from
# configure resolve to our dylibs (the SDK's .tbd stubs are never linked
# against) and LIBS holds only -l flags: configure compiles some tests with
# "-x objective-c" in CPPFLAGS, which would make clang read a .dylib path
# in LIBS as source.
LINKDIR="$G/linkdir"
rm -rf "$LINKDIR"; mkdir -p "$LINKDIR"
LIBFLAGS=""
for d in "${SYSLIBS[@]}"; do
    n="$(basename "$d" .dylib)"
    case "$n" in libobjc.A) n=libobjc ;; libSystem.B) n=libSystem ;; esac
    ln -s "$d" "$LINKDIR/$n.dylib"
    LIBFLAGS="$LIBFLAGS -l${n#lib}"
done
# As on Apple, libdl/libpthread/libm are libSystem (configure adds -ldl
# and -lpthread). Without these, -ldl found the SDK's libdl.tbd -- the
# SDK's libSystem -- and symbols we own in libinfo got bound to it.
for n in libdl libpthread libm; do ln -s "$SYS/libSystem.B.dylib" "$LINKDIR/$n.dylib"; done
# libSystem.B reexports dylibs by install path (/usr/lib/system/...,
# /usr/lib/libresolv.9.dylib, ...), which ld would look up inside the SDK
# sysroot. -dylib_file maps each one to our build of it, found by its own
# LC_ID_DYLIB.
DYLIB_MAP=""
for p in $(otool -L "$SYS/libSystem.B.dylib" | awk '/reexport/ {print $1}'); do
    f=""
    for c in "$SYS"/*.dylib "$OUT/libobjc.A.dylib"; do
        [ "$(otool -D "$c" 2>/dev/null | tail -1)" = "$p" ] && { f="$c"; break; }
    done
    [ -n "$f" ] || { echo "error: no dylib in $SYS has install name $p (reexported by libSystem.B)" >&2; exit 1; }
    DYLIB_MAP="$DYLIB_MAP -Wl,-dylib_file,$p:$f"
done
# -Wl,-Z: never search the SDK's /usr/lib for a -l, so a library this
# project does not build is a link error, not a silent SDK stub.
LINK="-nostdlib -B$NEWLD_BINDIR -Wl,-Z -Wl,-fixup_chains -L$LINKDIR$DYLIB_MAP"

if [ -z "${GNUSTEP_BASE_ONLY:-}" ]; then
    rsync -a --delete --exclude .git "$GM_SRC/" "$G/make-src/"
    rm -rf "$ROOT"
    (cd "$G/make-src" &&
     ./configure --host=aarch64-apple-darwin20 --prefix="$PREFIX" --with-config-file="$CONF" \
        --with-library-combo=apple-gnu-gnu --with-layout=gnustep --enable-absolute-install-paths \
        CC="$CLANG" OBJC="$CLANG" CXX="$CLANG++" \
        CFLAGS="${TARGET[*]}" OBJCFLAGS="${TARGET[*]}" LDFLAGS="${TARGET[*]}" > configure.out &&
     make > make.out && make install DESTDIR="$ROOT" > install.out) ||
        { echo "error: gnustep-make failed, see $G/make-src/*.out" >&2; exit 1; }
    echo "installed gnustep-make into $ROOT$PREFIX"
fi
[ -f "$ROOT$CONF" ] || { echo "error: $ROOT$CONF missing -- run without GNUSTEP_BASE_ONLY" >&2; exit 1; }

# Build-time view of the installed gnustep-make: its makefiles and
# gnustep-config from root/, but the config file's paths are the target's,
# so everything built here is configured for /usr/GNUstep.
# GNUstep.sh (which configure sources) takes GNUSTEP_MAKEFILES from the
# config file, so the build uses a copy that differs only in that line.
BUILDCONF="$G/GNUstep-build.conf"
sed "s|^GNUSTEP_MAKEFILES=.*|GNUSTEP_MAKEFILES=$ROOT$PREFIX/System/Library/Makefiles|" "$ROOT$CONF" > "$BUILDCONF"
grep -q "^GNUSTEP_MAKEFILES=$ROOT" "$BUILDCONF" || { echo "error: no GNUSTEP_MAKEFILES line in $ROOT$CONF" >&2; exit 1; }
export GNUSTEP_MAKEFILES="$ROOT$PREFIX/System/Library/Makefiles"
export GNUSTEP_CONFIG_FILE="$BUILDCONF"
export PATH="$ROOT$PREFIX/System/Tools:$PATH"

# Apple's ffi.h needs the same availability-macro fixes as build_libffi.sh.
# They go in a forced-include header, not -D flags: gnustep-make pastes
# CPPFLAGS unquoted into shell recipes, and the parentheses in
# -DSPI_AVAILABLE(...)= are a shell syntax error there.
cat > "$G/ffi_availability_compat.h" <<'EOH'
/* Generated by build_gnustep_base.sh -- see build_libffi.sh's CFLAGS comment. */
#define SPI_AVAILABLE(...)
#define SPI_DEPRECATED(...)
#define __API_AVAILABLE_PLATFORM_iosmac(x) macCatalyst,introduced=x
#define __API_DEPRECATED_PLATFORM_iosmac(x,y) macCatalyst,introduced=x,deprecated=y
EOH

rsync -a --delete --exclude .git "$GB_SRC/" "$G/base-src/"
cd "$G/base-src"
# Off for the first cut: TLS (gnutls), ICU, libxml2/libxslt, zeroconf and
# NSURLSession (libcurl). Each is its own later step.
# PKG_CONFIG_LIBDIR: an empty directory, so configure's pkg-config probes
# (libffi, ...) cannot find the host SDK's .pc files and add host include
# paths such as MacOSX.sdk/usr/include/ffi.
mkdir -p "$G/pkgconfig-empty"
PKG_CONFIG_LIBDIR="$G/pkgconfig-empty" \
./configure --host=aarch64-apple-darwin20 --with-cross-compilation-info="$CROSS" \
    --with-config-file="$CONF" --with-default-config="$ROOT$CONF" --with-installation-domain=SYSTEM \
    --disable-tls --disable-icu --disable-xml --disable-xslt --disable-zeroconf --disable-nsurlsession \
    CC="$CLANG" OBJC="$CLANG" CXX="$CLANG++" \
    `# -D_FORTIFY_SOURCE=0: this project's libc has no __*_chk entry points` \
    `# (same as every build_*.sh; build_libc_locale.sh documents the recipe).` \
    CPPFLAGS="-D_FORTIFY_SOURCE=0 -I$OUT/objc4_public_headers -I$OUT/libffi_include -include $G/ffi_availability_compat.h" \
    CFLAGS="${TARGET[*]}" OBJCFLAGS="${TARGET[*]}" \
    LDFLAGS="${TARGET[*]} $LINK" LIBS="$LIBFLAGS" > configure.out 2>&1 ||
    { tail -20 configure.out; echo "error: gnustep-base configure failed, see $G/base-src/configure.out" >&2; exit 1; }
echo "configured gnustep-base"
# DYLIB_EXTRA_FLAGS= drops gnustep-make's "-undefined dynamic_lookup".
# GNUSTEP_INSTALLATION_DOMAIN: configure's --with-installation-domain only
# sets a default; the library's install name comes from this at link time.
# ADDITIONAL_TOOL_LIBS: the Tools' own link lines (gdomap is plain C) do
# not include configure's library list, so give them the same owners.
make -k DYLIB_EXTRA_FLAGS= GNUSTEP_INSTALLATION_DOMAIN=SYSTEM ADDITIONAL_TOOL_LIBS="$LIBFLAGS" > make.out 2>&1 ||
    { grep -E "error:|Undefined|^ld: " make.out | head -20; echo "error: gnustep-base build failed, see $G/base-src/make.out" >&2; exit 1; }
rm -rf "$ROOT$PREFIX/System/Library/Libraries/gnustep-base" "$ROOT$PREFIX/System/Library/Libraries/libgnustep-base"* \
    "$ROOT$PREFIX/Local"   # base installs only into the SYSTEM domain
make install DESTDIR="$ROOT" GNUSTEP_INSTALLATION_DOMAIN=SYSTEM > install.out 2>&1 ||
    { tail -20 install.out; echo "error: gnustep-base install failed" >&2; exit 1; }
# base.make goes to $GNUSTEP_MAKEFILES/Additional, which during the build
# is root/'s own host path, so DESTDIR nests it under root/<host path>.
# Move it to the target's makefiles directory.
NESTED="$ROOT$GNUSTEP_MAKEFILES"
HOSTTOP="$(echo "$ROOT" | cut -d/ -f2)"     # e.g. "Users"
[ -n "$HOSTTOP" ] || { echo "error: cannot take the top directory of $ROOT" >&2; exit 1; }
if [ -d "$NESTED" ]; then
    cp -R "$NESTED/." "$GNUSTEP_MAKEFILES/"
    rm -rf "${ROOT:?}/$HOSTTOP"
fi
[ ! -e "$ROOT/$HOSTTOP" ] || { echo "error: host path still nested under $ROOT" >&2; exit 1; }
LIB="$ROOT$PREFIX/System/Library/Libraries/libgnustep-base.dylib"
[ -e "$LIB" ] || { echo "error: $LIB not installed" >&2; exit 1; }
ID="$(otool -D "$LIB" | tail -1)"
case "$ID" in "$PREFIX"/*) ;; *) echo "error: install name '$ID' is not under $PREFIX" >&2; exit 1 ;; esac
cd "$HERE"
# Every Mach-O installed (the library and the Tools: gdomap, plutil,
# defaults, ...), not just the library.
n=0
while IFS= read -r f; do
    ./bind_target_audit.sh "$f" >/dev/null || { ./bind_target_audit.sh "$f"; echo "error: $f fails bind_target_audit" >&2; exit 1; }
    n=$((n+1))
done < <(find "$ROOT$PREFIX" -type f -perm -u+x -exec sh -c 'file -b "$1" | grep -q Mach-O && echo "$1"' _ {} \;)
echo "bind_target_audit: $n images clean"
echo "installed gnustep-base into $ROOT$PREFIX (install name $ID)"
