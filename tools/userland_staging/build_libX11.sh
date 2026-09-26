#!/bin/bash
# Builds Xlib from real, unmodified libX11-1.8.13 (third_party/libX11,
# freedesktop xorg/lib/libx11). DAR-383, X11GS 2. Toolchain, layout and
# helper functions: x11_common.sh's header.
#
# Output (libc_build/system/):
#   libX11.dylib      /usr/X11/lib/libX11.6.dylib      (-version-number 6:4:0)
#   libX11-xcb.dylib  /usr/X11/lib/libX11-xcb.1.dylib  (-version-number 1:0:0)
#
# Source list: read straight out of the automake files with
# x11_am_sources, for the configuration x11_config/libX11/config.h
# describes (XLOCALE, XCMS, XKB all on; --enable-loadable-i18n off, so
# the lc/im/om modules are linked into libX11 itself the way
# src/xlibi18n/Makefile.am's non-XLIB_LOADABLE_I18N branch does it):
#   src/Makefile.am              libX11_la_SOURCES
#   src/xcms/Makefile.am         libxcms_la_SOURCES
#   src/xkb/Makefile.am          libxkb_la_SOURCES
#   src/xlibi18n/Makefile.am     libi18n_la_SOURCES   (XI18N_DL_SOURCES empty)
#   modules/im/ximcp             ximcp_la_SOURCES
#   modules/lc/{def,gen,Utf8}    xlcDef/xlibi18n/xlcUTF8Load_la_SOURCES
#   modules/om/generic           xomGeneric_la_SOURCES
#   + src/reallocarray.c         (AC_REPLACE_FUNCS, see config.h)
# with each directory's own AM_CPPFLAGS (-D_BSD_SOURCE everywhere,
# -DX11_t -DTRANS_CLIENT in src/, -DXIM_t -DTRANS_CLIENT in ximcp,
# -DXCMSDIR in xcms).
#
# ks_tables.h is generated the way src/Makefile.am's ks_tables.h rule
# does: build src/util/makekeys.c with the HOST compiler (CC_FOR_BUILD)
# and run it over configure.ac's KEYSYMDEFS list (keysymdef.h
# XF86keysym.h Sunkeysym.h DECkeysym.h HPkeysym.h from xorgproto).
#
# Links libxcb (Xlib-on-xcb transport since libX11 1.4 -- xcb_io.c),
# libXau/libXdmcp (libxcb's own deps; nothing in Xlib calls them directly
# so they are not on this link line), libinfo (getaddrinfo, getpwnam_r,
# getpwuid_r) and libresolv (inet_ntop, gethostname).
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

SRC="$TP/libX11"
x11_require_source "$SRC/src/xcb_io.c"
if [ ! -f "$X11_INC/X11/keysymdef.h" ]; then
    echo "error: $X11_INC/X11/keysymdef.h not found -- run build_xorg_headers.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libxcb.dylib" ]; then
    echo "error: $OUT/system/libxcb.dylib not found -- run build_libxcb.sh first" >&2
    exit 1
fi
for d in libsystem_kernel libsystem_c libsystem_pthread libsystem_malloc libsystem_platform libdyld libinfo; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2; exit 1; }
done
if [ ! -f "$OUT/system/libresolv.dylib" ]; then
    echo "error: $OUT/system/libresolv.dylib not found -- run build_libresolv_dylib.sh first" >&2
    exit 1
fi
x11_require_sys_dylibs

GEN="$X11_ROOT/gen/libX11"
rm -rf "$GEN"
mkdir -p "$GEN"

# --- public headers (include/Makefile.am x11include_HEADERS +
# x11extinclude_HEADERS + the configured XlibConf.h) ---
x11_stage_headers X11 "$SRC"/include/X11/*.h "$X11_CONFIG_DIR/libX11/X11/XlibConf.h"
x11_stage_headers X11/extensions "$SRC"/include/X11/extensions/*.h

# --- ks_tables.h via the host-built makekeys ---
/usr/bin/cc -I "$X11_INC" -o "$GEN/makekeys" "$SRC/src/util/makekeys.c"
"$GEN/makekeys" "$X11_INC/X11/keysymdef.h" "$X11_INC/X11/XF86keysym.h" \
    "$X11_INC/X11/Sunkeysym.h" "$X11_INC/X11/DECkeysym.h" "$X11_INC/X11/HPkeysym.h" \
    > "$GEN/ks_tables.h"
[ -s "$GEN/ks_tables.h" ] || { echo "error: makekeys produced an empty ks_tables.h" >&2; exit 1; }

COMMON=(-I "$X11_CONFIG_DIR/libX11" -I "$GEN"
    -I "$SRC/include" -I "$SRC/include/X11"
    -I "$SRC/src/xcms" -I "$SRC/src/xkb" -I "$SRC/src/xlibi18n" -I "$SRC/src"
    -D_BSD_SOURCE)

OBJ="$X11_ROOT/obj/libX11"
rm -rf "$OBJ"
mkdir -p "$OBJ"

compile_am() {  # <subdir> <am-variable> [extra flags...]
    local dir="$1" var="$2"
    shift 2
    local f n=0
    for f in $(x11_am_sources "$SRC/$dir/Makefile.am" "$var"); do
        x11_compile "$OBJ" "$SRC/$dir/$f" "${COMMON[@]}" "$@"
        n=$((n + 1))
    done
    [ "$n" -gt 0 ] || { echo "error: no sources read from $dir/Makefile.am $var" >&2; exit 1; }
}
compile_am src libX11_la_SOURCES -DX11_t -DTRANS_CLIENT
x11_compile "$OBJ" "$SRC/src/reallocarray.c" "${COMMON[@]}"
compile_am src/xcms libxcms_la_SOURCES "-DXCMSDIR=\"$X11_PREFIX/share/X11\""
compile_am src/xkb libxkb_la_SOURCES
compile_am src/xlibi18n libi18n_la_SOURCES
compile_am modules/im/ximcp ximcp_la_SOURCES -DXIM_t -DTRANS_CLIENT
compile_am modules/lc/def xlcDef_la_SOURCES
compile_am modules/lc/gen xlibi18n_la_SOURCES
compile_am modules/lc/Utf8 xlcUTF8Load_la_SOURCES
compile_am modules/om/generic xomGeneric_la_SOURCES
x11_compile_report libX11

x11_link_dylib libX11.dylib libX11 6:4:0 "$OBJ" \
    "$OUT/system/libxcb.dylib" "$OUT/system/libinfo.dylib" "$OUT/system/libresolv.dylib"

# --- libX11-xcb (src/Makefile.am libX11_xcb_la_SOURCES = x11_xcb.c) ---
X11_OK=0 X11_FAIL=0
XOBJ="$X11_ROOT/obj/libX11-xcb"
rm -rf "$XOBJ"
mkdir -p "$XOBJ"
x11_compile "$XOBJ" "$SRC/src/x11_xcb.c" "${COMMON[@]}" -DX11_t -DTRANS_CLIENT
x11_compile_report libX11-xcb
x11_link_dylib libX11-xcb.dylib libX11-xcb 1:0:0 "$XOBJ" "$OUT/system/libX11.dylib"
