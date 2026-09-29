#!/bin/bash
# Builds GWorkspace (DAR-392, X11GS 9), GNUstep's file manager and desktop,
# from third_party/gnustep-gworkspace (our fork anurodhp/apps-gworkspace,
# branch iokit-port, off gnustep/apps-gworkspace tag gworkspace-1_0_0,
# 2025-02, released with base 1.31 / gui 0.32), and installs it into
# libc_build/gnustep/root under /usr/GNUstep/System:
#   Applications/GWorkspace.app (+ its Finder modules, Thumbnailer bundles)
#   Applications/Recycler.app
#   Library/Frameworks/{FSNode,DBKit}.framework, Inspector and Operation
#   Library/Bundles (Inspector content viewers)
#   Tools/{ddbd,fswatcher,lsfupdater,searchtool,wopen}
# Run it on the target under an X server (gdnc must be running):
# /usr/GNUstep/System/Applications/GWorkspace.app/GWorkspace
#
# Optional pieces that need libraries this port does not build are off:
# GWMetadata (--enable-gwmetadata, needs SQLite), fswatcher-inotify
# (--with-inotify; Linux only), and the Inspector's PDF viewer (PDFKit is
# not built here; Inspector/configure.ac sets have_pdfkit=no on its own).
#
# The tree is copied into libc_build/gnustep/gworkspace-src first because
# GWorkspace configures and builds in-tree (top configure plus the
# FSNode and Inspector sub-configures).
# Link recipe and audit: gnustep_common.sh.
cd "$(dirname "$0")"
. ./gnustep_common.sh
GW_SRC="$HERE/../../third_party/gnustep-gworkspace"
[ -f "$GW_SRC/configure" ] || { echo "error: $GW_SRC not found -- run setup_third_party.sh" >&2; exit 1; }
# gen_build_order.py sees the edge: run build_gnustep_host_tools.sh and
# build_gnustep_back.sh first (gnustep-gui, back and base with them).
[ -d "$ROOT$PREFIX/System/Library/Bundles/libgnustep-back-032.bundle" ] ||
    { echo "error: gnustep-back not installed -- run build_gnustep_back.sh first" >&2; exit 1; }
[ -e "$GS_SYS_LIBS/libgnustep-gui.dylib" ] ||
    { echo "error: libgnustep-gui.dylib not installed -- run build_gnustep_gui.sh first" >&2; exit 1; }
gs_setup_link "${GS_BASE_SYSLIBS[@]}"
gs_build_env
# The subprojects link the sibling frameworks as -lFSNode/-lInspector/
# -lOperation with -L<name>.framework (GWorkspace/GNUmakefile.preamble:32-37,
# Inspector/GNUmakefile.preamble:15-18), which assumes a lib<name> file in
# the framework directory. gnustep-make's Darwin framework is
# <name>.framework/Versions/<n>/<name> with no lib<name>.dylib, so ld
# reports "library 'FSNode' not found". LINKDIR gets a link to each, dangling
# until the framework is built (the aggregate builds FSNode, Inspector and
# Operation before GWorkspace links them). DBKit is a real library and is
# found by the preambles' own -L.
for f in FSNode Inspector Operation; do
    ln -s "$G/gworkspace-src/$f/$f.framework/Versions/Current/$f" "$LINKDIR/lib$f.dylib"
done

GW_CPPFLAGS="${TARGET[*]} -D_FORTIFY_SOURCE=0 -I$GS_SYS_HEADERS -I$OUT/objc4_public_headers"
GW_LDFLAGS="${TARGET[*]} $LINK $LIBFLAGS"

rsync -a --delete --exclude .git "$GW_SRC/" "$G/gworkspace-src/"
cd "$G/gworkspace-src"
# Inspector's configure would otherwise find the host's `file` and `sh`:
# --with-file/--with-sh give the target's paths (Inspector/configure.ac:57-79
# writes them into config.h as FILEPATH and SHPATH).
./configure --host=aarch64-apple-darwin20 \
    --with-file=/usr/bin/file --with-sh=/bin/sh \
    CC="$CLANG" OBJC="$CLANG" CXX="$CLANG++" \
    CPPFLAGS="$GW_CPPFLAGS" CFLAGS="${TARGET[*]}" OBJCFLAGS="${TARGET[*]}" \
    LDFLAGS="$GW_LDFLAGS" LIBS="$LIBFLAGS" > configure.out 2>&1 ||
    { tail -20 configure.out; echo "error: gworkspace configure failed, see $G/gworkspace-src/configure.out" >&2; exit 1; }
echo "configured gworkspace"

make DYLIB_EXTRA_FLAGS= GNUSTEP_INSTALLATION_DOMAIN=SYSTEM CPPFLAGS="$GW_CPPFLAGS" \
    LDFLAGS="$GW_LDFLAGS" > make.out 2>&1 ||
    { grep -E "error:|Undefined|^ld: |^  \"" make.out | grep -v "ld: warning" | head -30 || true
      echo "error: gworkspace build failed, see $G/gworkspace-src/make.out" >&2; exit 1; }
rm -rf "$ROOT$PREFIX/System/Applications/GWorkspace.app" "$ROOT$PREFIX/System/Applications/Recycler.app" \
       "$GS_SYS_LIBS/FSNode.framework" "$ROOT$PREFIX/System/Library/Frameworks/FSNode.framework" \
       "$ROOT$PREFIX/System/Library/Frameworks/DBKit.framework"
make install DESTDIR="$ROOT" GNUSTEP_INSTALLATION_DOMAIN=SYSTEM > install.out 2>&1 ||
    { tail -20 install.out; echo "error: gworkspace install failed" >&2; exit 1; }
gs_relocate_makefiles
APP="$ROOT$PREFIX/System/Applications/GWorkspace.app"
[ -x "$APP/GWorkspace" ] || { echo "error: $APP/GWorkspace not installed" >&2; exit 1; }
otool -l "$APP/GWorkspace" | grep -q "cmd LC_MAIN" || { echo "error: $APP/GWorkspace has no LC_MAIN" >&2; exit 1; }
gs_audit_tree "$ROOT$PREFIX/System/Applications"
gs_audit_tree "$ROOT$PREFIX/System/Library/Frameworks"
gs_audit_tree "$ROOT$PREFIX/System/Tools"
echo "installed GWorkspace into $ROOT$PREFIX"
