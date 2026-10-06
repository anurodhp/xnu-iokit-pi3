#!/bin/bash
# Builds gnustep-back (DAR-390, X11GS 6), GNUstep's display backend, as the
# x11 server with the cairo graphics backend, for arm64 Darwin on objc4. It
# installs into libc_build/gnustep/root as gnustep-gui's backend bundle
# (System/Library/Bundles/libgnustep-back-032.bundle, which gui loads at
# run time through NSBundle), plus the gpbs pasteboard server and font_cacher.
#
# Source: third_party/gnustep-back, gnustep/libs-back tag back-0_32_0,
# released with gui-0_32_0. Libraries: the X client libraries
# (build_libX11.sh and friends, X11GS 2), cairo, Xft, fontconfig and
# freetype (X11GS 3). back's configure finds cairo, Xft, fontconfig and
# freetype with pkg-config (configure.ac:274, 287, 465-470); the *_CFLAGS/
# *_LIBS variables below give PKG_CHECK_MODULES its answer, since this
# project installs no .pc files. back's configure has no run tests.
#
# GLX: on (NSOpenGLView / NSOpenGLContext, Source/x11/XGGLContext.m and
# XGGLFormat.m), against Mesa's libGL from build_mesa.sh (client-side GLX,
# softpipe, or the V3D with MESA_VC4=1). configure wants -lGL with
# glXMakeContextCurrent, GL/glx.h and GLX_RGBA_TYPE (configure.ac:335-359).
# Mesa's xlib GLX implements every GLX call back makes, including
# glXQueryServerString, so the X server needs no GLX extension. The bundle
# links libGL, so every GNUstep app loads it (as AppKit links OpenGL).
cd "$(dirname "$0")"
. ./gnustep_common.sh
BACK_SRC="$HERE/../../third_party/gnustep-back"
X11_INC="$OUT/x11/include"
[ -f "$BACK_SRC/configure" ] || { echo "error: $BACK_SRC not found -- run setup_third_party.sh" >&2; exit 1; }
[ -e "$GS_SYS_LIBS/libgnustep-gui.dylib" ] || { echo "error: gnustep-gui not installed -- run build_gnustep_gui.sh first" >&2; exit 1; }
[ -f "$X11_INC/cairo/cairo-xlib.h" ] || { echo "error: cairo headers not staged -- run build_cairo.sh first" >&2; exit 1; }
[ -f "$X11_INC/GL/glx.h" ] && [ -f "$OUT/system/libGL.dylib" ] || { echo "error: libGL not built -- run build_mesa.sh first" >&2; exit 1; }
# Owners first: back's own libraries, then what they sit on.
gs_setup_link "$SYS/libGL.dylib" "$SYS/libcairo.dylib" "$SYS/libXft.dylib" "$SYS/libfontconfig.dylib" "$SYS/libfreetype.dylib" \
    "$SYS/libpixman-1.dylib" "$SYS/libpng16.dylib" "$SYS/libexpat.dylib" \
    "$SYS/libXrandr.dylib" "$SYS/libXrender.dylib" "$SYS/libXcursor.dylib" "$SYS/libXfixes.dylib" \
    "$SYS/libXmu.dylib" "$SYS/libXt.dylib" "$SYS/libSM.dylib" "$SYS/libICE.dylib" "$SYS/libXext.dylib" \
    "$SYS/libX11.dylib" "$SYS/libxcb.dylib" "$SYS/libXau.dylib" "$SYS/libXdmcp.dylib" \
    "$SYS/libtiff.dylib" "${GS_BASE_SYSLIBS[@]}"
gs_build_env
# See build_gnustep_gui.sh for why these go to configure and to make.
BACK_CPPFLAGS="${TARGET[*]} -D_FORTIFY_SOURCE=0 -I$GS_SYS_HEADERS -I$OUT/objc4_public_headers -I$X11_INC -I$X11_INC/freetype2 -I$OUT/fontstack/usr_include"

rsync -a --delete --exclude .git "$BACK_SRC/" "$G/back-src/"
cd "$G/back-src"
./configure --host=aarch64-apple-darwin20 --enable-server=x11 --enable-graphics=cairo --enable-glx \
    --x-includes="$X11_INC" --x-libraries="$LINKDIR" \
    CAIRO_CFLAGS="-I$X11_INC/cairo" CAIRO_LIBS="-lcairo" \
    CAIRO_FT_CFLAGS="-I$X11_INC/cairo" CAIRO_FT_LIBS="-lcairo" \
    CAIRO_XLIB_CFLAGS="-I$X11_INC/cairo" CAIRO_XLIB_LIBS="-lcairo" \
    FONTCONFIG_CFLAGS="" FONTCONFIG_LIBS="-lfontconfig" \
    FREETYPE_CFLAGS="-I$X11_INC/freetype2" FREETYPE_LIBS="-lfreetype" \
    XFT_CFLAGS="-I$X11_INC/freetype2" XFT_LIBS="-lXft" \
    CC="$CLANG" OBJC="$CLANG" CXX="$CLANG++" \
    CPPFLAGS="$BACK_CPPFLAGS" CFLAGS="${TARGET[*]}" OBJCFLAGS="${TARGET[*]}" \
    LDFLAGS="${TARGET[*]} $LINK" LIBS="$LIBFLAGS" > configure.out 2>&1 ||
    { tail -20 configure.out; echo "error: gnustep-back configure failed, see $G/back-src/configure.out" >&2; exit 1; }
grep -q "^BUILD_SERVER=x11" config.make && grep -q "^BUILD_GRAPHICS=cairo" config.make ||
    { grep "^BUILD_" config.make; echo "error: configure did not pick the x11 server + cairo graphics" >&2; exit 1; }
# configure.ac:356 AC_DEFINEs HAVE_GLX, which gates XGGLContext.m/XGGLFormat.m.
grep -q "^#define HAVE_GLX 1" config.h ||
    { grep -i -A3 "glXMakeContextCurrent\|GL/glx.h\|GLX_RGBA_TYPE" configure.out | head -20
      echo "error: configure did not enable GLX" >&2; exit 1; }
echo "configured gnustep-back (x11 + cairo + GLX)"
make -k DYLIB_EXTRA_FLAGS= GNUSTEP_INSTALLATION_DOMAIN=SYSTEM CPPFLAGS="$BACK_CPPFLAGS" \
    LDFLAGS="${TARGET[*]} $LINK $LIBFLAGS" > make.out 2>&1 ||
    { grep -E "error:|Undefined|^ld: |^  \"" make.out | grep -v "ld: warning" | head -30 || true
      echo "error: gnustep-back build failed, see $G/back-src/make.out" >&2; exit 1; }
rm -rf "$ROOT$PREFIX/System/Library/Bundles/libgnustep-back"*
make install DESTDIR="$ROOT" GNUSTEP_INSTALLATION_DOMAIN=SYSTEM > install.out 2>&1 ||
    { tail -20 install.out; echo "error: gnustep-back install failed" >&2; exit 1; }
gs_relocate_makefiles
B="$(ls -d "$ROOT$PREFIX"/System/Library/Bundles/libgnustep-back-*.bundle 2>/dev/null | head -1)"
[ -n "$B" ] || { echo "error: backend bundle not installed" >&2; exit 1; }
gs_audit_tree "$ROOT$PREFIX"
echo "installed gnustep-back into $ROOT$PREFIX ($(basename "$B"))"
