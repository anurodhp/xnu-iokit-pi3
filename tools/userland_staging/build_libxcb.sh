#!/bin/bash
# Builds libxcb from real, unmodified libxcb-1.17.0 (third_party/libxcb)
# and xcb-proto-1.17.0 (third_party/xcb-proto), freedesktop xorg/lib/libxcb
# and xorg/proto/xcbproto. DAR-383, X11GS 2. Toolchain, layout and helper
# functions: x11_common.sh's header.
#
# Output (libc_build/system/):
#   libxcb.dylib         /usr/X11/lib/libxcb.1.dylib         (core)
#   libxcb-shm.dylib     /usr/X11/lib/libxcb-shm.0.dylib     (MIT-SHM)
#   libxcb-render.dylib  /usr/X11/lib/libxcb-render.0.dylib  (RENDER)
#   libxcb-{composite,damage,xfixes,randr,shape,present,sync}.dylib
#                        /usr/X11/lib/libxcb-<ext>.<major>.dylib
# shm/render are the two extension libraries cairo's xcb surface uses
# (cairo-xcb-shm / cairo-xcb pkg-config requirements). composite, damage,
# xfixes, randr, shape and present are extension libraries for programs that
# speak XCB directly (they were first built for a window manager that has since
# been dropped); sync is present.xml's <import> (present.h includes sync.h), and dri3 is imported
# too, so only its header is generated and staged (no libxcb-dri3). Every
# other XCB_EXTENSION(...) in configure.ac:222-248 is left for whoever first
# needs it -- each is the same one-generated-file shape as these.
#
# Protocol C is generated on the host, exactly as src/Makefile.am:271-275
# does it: `$(PYTHON) c_client.py -c "$(PACKAGE_STRING)" -l
# "$(XORG_MAN_PAGE)" -s "$(LIB_MAN_SUFFIX)" -p $(XCBPROTO_XCBPYTHONDIR)
# <xcb-proto>/src/<ext>.xml`, with xcb-proto's own xcbgen package as the
# python dir. The man pages it also writes are discarded.
#
# Core sources: libxcb_la_SOURCES (xcb_conn xcb_out xcb_in xcb_ext xcb_xid
# xcb_list xcb_util xcb_auth) + nodist xproto.c bigreq.c xc_misc.c.
# Links libXau (xcb_auth.c's XauGetBestAuthByAddr) and libXdmcp
# (XdmcpWrap, HASXDMAUTH). config: x11_config/libxcb/config.h.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

SRC="$TP/libxcb/src"
PROTO="$TP/xcb-proto"
x11_require_source "$SRC/c_client.py"
x11_require_source "$PROTO/xcbgen/state.py"
if [ ! -f "$X11_INC/X11/Xmd.h" ]; then
    echo "error: $X11_INC/X11/Xmd.h not found -- run build_xorg_headers.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libXau.dylib" ]; then
    echo "error: $OUT/system/libXau.dylib not found -- run build_libXau.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libXdmcp.dylib" ]; then
    echo "error: $OUT/system/libXdmcp.dylib not found -- run build_libXdmcp.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libinfo.dylib" ]; then
    echo "error: $OUT/system/libinfo.dylib not found -- run build_libinfo.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libresolv.dylib" ]; then
    echo "error: $OUT/system/libresolv.dylib not found -- run build_libresolv_dylib.sh first" >&2
    exit 1
fi
for d in libsystem_kernel libsystem_c libsystem_pthread libsystem_malloc libsystem_platform libdyld; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2; exit 1; }
done
x11_require_sys_dylibs
command -v python3 >/dev/null || { echo "error: python3 not found (needed for c_client.py)" >&2; exit 1; }

GEN="$X11_ROOT/gen/libxcb"
rm -rf "$GEN"
mkdir -p "$GEN"
for x in xproto bigreq xc_misc shm render composite damage xfixes randr shape present sync dri3; do
    (cd "$GEN" && python3 "$OLDPWD/$SRC/c_client.py" -c "libxcb 1.17.0" -l "X Version 11" -s 3 \
        -p "$OLDPWD/$PROTO" "$OLDPWD/$PROTO/src/$x.xml") \
        || { echo "error: c_client.py failed on $x.xml" >&2; exit 1; }
done
rm -rf "$GEN/man"
x11_stage_headers xcb "$SRC/xcb.h" "$SRC/xcbext.h" "$GEN"/*.h

XCB_FLAGS=(-I "$X11_CONFIG_DIR/libxcb" -I "$SRC" -I "$X11_INC/xcb")

OBJ="$X11_ROOT/obj/libxcb"
rm -rf "$OBJ"
mkdir -p "$OBJ"
for f in xcb_conn xcb_out xcb_in xcb_ext xcb_xid xcb_list xcb_util xcb_auth; do
    x11_compile "$OBJ" "$SRC/$f.c" "${XCB_FLAGS[@]}"
done
for f in xproto bigreq xc_misc; do
    x11_compile "$OBJ" "$GEN/$f.c" "${XCB_FLAGS[@]}"
done
x11_compile_report libxcb
# version-info 2:0:1 (src/Makefile.am:21): current 2, age 1 -> libtool
# darwin major 1, compatibility/current version 3. Expressed as the
# equivalent version-number 1:1:0 for x11_link_dylib.
x11_link_dylib libxcb.dylib libxcb 1:1:0 "$OBJ" "$OUT/system/libXau.dylib" "$OUT/system/libXdmcp.dylib" \
    "$OUT/system/libinfo.dylib" "$OUT/system/libresolv.dylib"

# <ext>:<version-number M:m:r>. libtool -version-info current:revision:age
# (src/Makefile.am:30-174) maps to M=current-age, m=age, r=revision:
# randr 1:0:1 -> 0:1:0, sync 1:0:0 -> 1:0:0, every other one 0:0:0.
for xv in shm:0:0:0 render:0:0:0 composite:0:0:0 damage:0:0:0 xfixes:0:0:0 \
          randr:0:1:0 shape:0:0:0 present:0:0:0 sync:1:0:0; do
    x=${xv%%:*} vn=${xv#*:}
    X11_OK=0 X11_FAIL=0
    EOBJ="$X11_ROOT/obj/libxcb-$x"
    rm -rf "$EOBJ"
    mkdir -p "$EOBJ"
    x11_compile "$EOBJ" "$GEN/$x.c" "${XCB_FLAGS[@]}"
    x11_compile_report "libxcb-$x"
    x11_link_dylib "libxcb-$x.dylib" "libxcb-$x" "$vn" "$EOBJ" "$OUT/system/libxcb.dylib"
done
