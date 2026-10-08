#!/bin/bash
# Builds gnustep-gui (GNUstep's AppKit, libgnustep-gui.dylib) for arm64
# Darwin on objc4 (DAR-390, X11GS 6) and installs it into
# libc_build/gnustep/root beside gnustep-base (gnustep_common.sh has the
# layout and the link recipe).
#
# Source: third_party/gnustep-gui, gnustep/libs-gui tag gui-0_32_0, released
# with base 1.31 (both 2025-02). Image formats: TIFF (required,
# configure.ac:307, build_libtiff.sh), PNG (build_libpng.sh) and JPEG
# (build_libjpeg.sh, libjpeg-turbo; NSBitmapImageRep reads .jpg/.jpeg). GIF,
# ImageMagick, ICU, aspell, sound, speech and CUPS are off: none of their
# libraries is built here. gui's configure has no run tests, so no
# cross-config file is needed.
#
# The tree is copied into libc_build/gnustep/gui-src first because gui
# configures and builds in-tree.
cd "$(dirname "$0")"
. ./gnustep_common.sh
GUI_SRC="$HERE/../../third_party/gnustep-gui"
X11_INC="$OUT/x11/include"
[ -f "$GUI_SRC/configure" ] || { echo "error: $GUI_SRC not found -- run setup_third_party.sh" >&2; exit 1; }
for f in "$GS_SYS_LIBS/libgnustep-base.dylib" "$X11_INC/tiffio.h" "$X11_INC/png.h" "$X11_INC/jpeglib.h" "$SYS/libjpeg.dylib"; do
    [ -e "$f" ] || { echo "error: $f not found -- run build_gnustep_base.sh, build_libtiff.sh, build_libpng.sh and build_libjpeg.sh first" >&2; exit 1; }
done
gs_setup_link "$SYS/libtiff.dylib" "$SYS/libpng16.dylib" "$SYS/libjpeg.dylib" "${GS_BASE_SYSLIBS[@]}"
# configure.ac:326 checks -lpng.
ln -s "$SYS/libpng16.dylib" "$LINKDIR/libpng.dylib"
ln -s "$SYS/libjpeg.dylib" "$LINKDIR/libjpeg.dylib"
gs_build_env

# -I root/'s headers: gnustep-make points at the target's
# /usr/GNUstep/System/Library/Headers, which does not exist here. Given to
# make as well (and LDFLAGS): the Model bundle's sub-make does not read
# configure's.
# TARGET is in CPPFLAGS too: the AC_HEADER_STDC and AC_CHECK_HEADERS
# preprocessor runs use $CPP with CPPFLAGS only, and did not find string.h.
# -D_FORTIFY_SOURCE=0: this project's libc has no __*_chk entry points.
GUI_CPPFLAGS="${TARGET[*]} -D_FORTIFY_SOURCE=0 -I$GS_SYS_HEADERS -I$OUT/objc4_public_headers -I$X11_INC -I$OUT/fontstack/usr_include"

rsync -a --delete --exclude .git "$GUI_SRC/" "$G/gui-src/"
cd "$G/gui-src"
# ac_cv_prog_HAVE_LIBPNG_CONFIG=no: configure.ac:319 would otherwise run the
# host's libpng-config and link the host libpng; the -lpng path below
# (configure.ac:334-335) finds ours in LINKDIR.
./configure --host=aarch64-apple-darwin20 ac_cv_prog_HAVE_LIBPNG_CONFIG=no \
    --enable-jpeg --disable-ungif --disable-libgif --disable-imagemagick --disable-icu \
    --disable-aspell --disable-sound --disable-speech --disable-speech-recognizer --disable-cups \
    CC="$CLANG" OBJC="$CLANG" CXX="$CLANG++" \
    CPPFLAGS="$GUI_CPPFLAGS" \
    CFLAGS="${TARGET[*]}" OBJCFLAGS="${TARGET[*]}" \
    LDFLAGS="${TARGET[*]} $LINK" LIBS="$LIBFLAGS" > configure.out 2>&1 ||
    { tail -20 configure.out; echo "error: gnustep-gui configure failed, see $G/gui-src/configure.out" >&2; exit 1; }
echo "configured gnustep-gui"
# DYLIB_EXTRA_FLAGS= drops gnustep-make's "-undefined dynamic_lookup";
# GNUSTEP_INSTALLATION_DOMAIN=SYSTEM as for base (install name).
# LDFLAGS carries the owners' -l flags too, so the Tools, bundles and
# ColorPickers/TextConverters link against them. (Not ADDITIONAL_*_LIBS on
# the command line: that would override the subprojects' own
# "ADDITIONAL_TOOL_LIBS += -lgnustep-gui".)
make -k DYLIB_EXTRA_FLAGS= GNUSTEP_INSTALLATION_DOMAIN=SYSTEM CPPFLAGS="$GUI_CPPFLAGS" \
    LDFLAGS="${TARGET[*]} $LINK $LIBFLAGS" > make.out 2>&1 ||
    { grep -E "error:|Undefined|^ld: " make.out | head -30 || true; echo "error: gnustep-gui build failed, see $G/gui-src/make.out" >&2; exit 1; }
rm -rf "$GS_SYS_LIBS/gnustep-gui" "$GS_SYS_LIBS/libgnustep-gui"*
make install DESTDIR="$ROOT" GNUSTEP_INSTALLATION_DOMAIN=SYSTEM > install.out 2>&1 ||
    { tail -20 install.out; echo "error: gnustep-gui install failed" >&2; exit 1; }
gs_relocate_makefiles
LIB="$GS_SYS_LIBS/libgnustep-gui.dylib"
[ -e "$LIB" ] || { echo "error: $LIB not installed" >&2; exit 1; }
ID="$(otool -D "$LIB" | tail -1)"
case "$ID" in "$PREFIX"/*) ;; *) echo "error: install name '$ID' is not under $PREFIX" >&2; exit 1 ;; esac
gs_audit_tree "$ROOT$PREFIX"
echo "installed gnustep-gui into $ROOT$PREFIX (install name $ID)"
