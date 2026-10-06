#!/bin/bash
# Builds Window Maker 0.96.0 (third_party/wmaker, tag wmaker-0.96.0,
# unmodified upstream window-maker/wmaker). DAR-424.
#   libc_build/system/libwraster.dylib  (/usr/X11/lib/libwraster.6.dylib)
#   libc_build/system/libWUtil.dylib    (/usr/X11/lib/libWUtil.5.dylib)
#   libc_build/system/libWINGs.dylib    (/usr/X11/lib/libWINGs.3.dylib)
#   libc_build/x11/server/root/usr/X11/bin/wmaker
#   libc_build/x11/server/root/usr/X11/share/{WindowMaker,WINGs}
#   libc_build/x11/server/root/usr/X11/etc/WindowMaker   (global defaults)
# inject_into_sd_image.sh copies that staged tree and every
# libc_build/system dylib with a /usr/X11/lib install name, so it needs no
# entry of its own. The graphical login that starts wmaker at boot is
# gsdm (build_gsdm.sh).
#
# Toolchain, layout and helpers: x11_common.sh / fontstack_common.sh. As
# with the other X11 ports, configure is not run. Instead:
#   config: x11_config/wmaker/config.h + config-paths.h. Every define cites
#           the configure.ac check it answers; config-paths.h is what
#           Makefile.am:10-34 generates for --prefix=/usr/X11.
#   sources: each Makefile.am's own lists (x11_am_sources). Its automake
#           conditionals are resolved below, per config.h.
#   generated headers: wraster.h and WINGsP.h come from their .in
#           templates via upstream's own script/replace-ac-keywords.sh,
#           exactly as wrlib/Makefile.am and WINGs/WINGs/Makefile.am run
#           it. src/wconfig.h.in has no @keywords@, so config.status would
#           copy it unchanged.
#   nested functions: clang has none (WM_PROG_CC_NESTEDFUNC fails), so
#           src/Makefile.am:134-146 builds misc.hack_nf.c and
#           xmodifier.hack_nf.c with script/nested-func-to-macro.sh; so
#           does this script.
# dylib versions: libtool -version-info C:R:A from configure.ac:74-91,
# converted to x11_link_dylib's -version-number M:m:r (major = C-A,
# current = C+1): wraster 7:0:1 -> 6:1:0, WINGs 5:0:2 -> 3:2:0,
# WUtil 5:0:0 -> 5:0:0.
set -e
cd "$(dirname "$0")"
. ./fontstack_common.sh

SRC="$TP/wmaker"
x11_require_source "$SRC/configure.ac"
grep -q '^AC_INIT(\[WindowMaker\],\[0.96.0\]' "$SRC/configure.ac" || {
    echo "error: $SRC is not Window Maker 0.96.0 -- recheck this script" >&2; exit 1; }
for d in libX11 libXext libXmu libXt libXft libXrender libfontconfig libfreetype libpng16 libsystem_m \
         libinfo libremovefile libresolv; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- run its build_*.sh first" >&2; exit 1; }
done
x11_require_staged X11/Xft/Xft.h build_libXft.sh
x11_require_staged X11/Xmu/Xmu.h build_libXmu.sh
x11_require_sys_dylibs

CFG="$X11_CONFIG_DIR/wmaker"
# WINGs/wconfig.h:23's #include "../config.h" is searched in the source
# tree first. An in-tree ./configure would leave config.h (and a
# wrlib/wraster.h) there that silently shadows ours, so refuse.
for f in "$SRC/config.h" "$SRC/wrlib/wraster.h" "$SRC/WINGs/WINGs/WINGsP.h"; do
    [ -e "$f" ] && { echo "error: $f exists (in-tree configure?) and would shadow the generated one -- remove it" >&2; exit 1; }
done
GEN="$X11_ROOT/obj/wmaker_gen"
rm -rf "$GEN"
mkdir -p "$GEN/WINGs"
# WINGs/wconfig.h:23 includes "../config.h" (an in-tree build's top-level
# config.h). With -I "$GEN/WINGs" on the line, that resolves to
# $GEN/config.h, so the checked-in config is copied there and third_party
# stays untouched.
cp "$CFG/config.h" "$CFG/config-paths.h" "$GEN/"
sh "$SRC/script/replace-ac-keywords.sh" --header "$CFG/config.h" --filter USE_XSHM \
    -o "$GEN/wraster.h" "$SRC/wrlib/wraster.h.in"
sh "$SRC/script/replace-ac-keywords.sh" --header "$CFG/config.h" --filter USE_PANGO \
    -o "$GEN/WINGs/WINGsP.h" "$SRC/WINGs/WINGs/WINGsP.h.in"
grep -q '#include <X11/extensions/XShm.h>' "$GEN/wraster.h" || {
    echo "error: $GEN/wraster.h lost its XShm block -- replace-ac-keywords.sh did not read USE_XSHM" >&2; exit 1; }
cp "$SRC/src/wconfig.h.in" "$GEN/wconfig.h"
sh "$SRC/script/nested-func-to-macro.sh" "$SRC/src/misc.c" -o "$GEN/misc.hack_nf.c" \
    -f "append_string" -f "append_modifier"
sh "$SRC/script/nested-func-to-macro.sh" "$SRC/src/xmodifier.c" -o "$GEN/xmodifier.hack_nf.c" \
    -f "modwarn" -f "modbarf" -f "check_modifier" -f "store_modifier"

FT_INC=(-I "$X11_INC/freetype2")
COMMON=(-I "$CFG" -I "$GEN" -I "$SRC/WINGs" -I "$GEN/WINGs" "${FT_INC[@]}")

# ---- wrlib (libwraster) ------------------------------------------------
# wrlib/Makefile.am: the base libwraster_la_SOURCES, plus the conditional
# lists config.h selects: USE_PNG -> load_png.c save_png.c; no USE_XPM
# -> load_xpm_normalized.c. Not USE_GIF/JPEG/TIFF/WEBP/MAGICK.
OBJ="$X11_ROOT/obj/wraster"
rm -rf "$OBJ"; mkdir -p "$OBJ"
X11_OK=0 X11_FAIL=0
for f in $(x11_am_sources "$SRC/wrlib/Makefile.am" libwraster_la_SOURCES); do
    case "$f" in
        load_gif.c|load_jpeg.c|save_jpeg.c|load_tiff.c|load_xpm.c|load_webp.c|load_magick.c) continue ;;
    esac
    x11_compile "$OBJ" "$SRC/wrlib/$f" "${COMMON[@]}" -I "$SRC/wrlib"
done
x11_compile_report libwraster
x11_link_dylib libwraster.dylib libwraster 6:1:0 "$OBJ" \
    "$OUT/system/libpng16.dylib" "$OUT/system/libXmu.dylib" "$OUT/system/libXext.dylib" \
    "$OUT/system/libX11.dylib" "$OUT/system/libsystem_m.dylib"

# ---- WINGs: libWUtil, then libWINGs ------------------------------------
WFLAGS=("${COMMON[@]}" -I "$SRC/WINGs/WINGs" -I "$SRC/src"
    '-DRESOURCE_PATH="/usr/X11/share/WINGs"')
OBJ="$X11_ROOT/obj/WUtil"
rm -rf "$OBJ"; mkdir -p "$OBJ"
X11_OK=0 X11_FAIL=0
for f in $(x11_am_sources "$SRC/WINGs/Makefile.am" libWUtil_la_SOURCES); do
    x11_compile "$OBJ" "$SRC/WINGs/$f" "${WFLAGS[@]}"
done
x11_compile_report libWUtil
# Owners as the other X11 dylibs here already bind them: rename/fsync from
# libremovefile, gethostname from libresolv, getpwnam/getpwuid from libinfo
# (DAR-429 tracks moving the first three to their Apple owners).
x11_link_dylib libWUtil.dylib libWUtil 5:0:0 "$OBJ" "$OUT/system/libinfo.dylib" \
    "$OUT/system/libremovefile.dylib" "$OUT/system/libresolv.dylib" "$OUT/system/libsystem_m.dylib"

OBJ="$X11_ROOT/obj/WINGs"
rm -rf "$OBJ"; mkdir -p "$OBJ"
X11_OK=0 X11_FAIL=0
for f in $(x11_am_sources "$SRC/WINGs/Makefile.am" libWINGs_la_SOURCES); do
    x11_compile "$OBJ" "$SRC/WINGs/$f" "${WFLAGS[@]}"
done
x11_compile_report libWINGs
x11_link_dylib libWINGs.dylib libWINGs 3:2:0 "$OBJ" \
    "$OUT/system/libWUtil.dylib" "$OUT/system/libwraster.dylib" \
    "$OUT/system/libXft.dylib" "$OUT/system/libfontconfig.dylib" "$OUT/system/libXrender.dylib" \
    "$OUT/system/libXext.dylib" "$OUT/system/libX11.dylib" "$OUT/system/libremovefile.dylib" \
    "$OUT/system/libsystem_m.dylib"

# ---- wmaker ------------------------------------------------------------
# src/Makefile.am: wmaker_SOURCES plus USE_DOCK_XDND -> xdnd.c,
# USE_MWM_HINTS -> motif.c, WM_OSDEP_DARWIN -> osdep_darwin.c, and the
# !USE_NESTED_FUNC nodist sources generated above (instead of misc.c and
# xmodifier.c). main.c calls system(3), which the iPhoneOS SDK marks
# __IOS_PROHIBITED; ios_prohibited_override.h is the same override
# build_simple_cmds.sh uses for awk/ed (see that header).
OBJ="$X11_ROOT/obj/wmaker"
rm -rf "$OBJ"; mkdir -p "$OBJ"
X11_OK=0 X11_FAIL=0
for f in $(x11_am_sources "$SRC/src/Makefile.am" wmaker_SOURCES); do
    case "$f" in
        osdep_bsd.c|osdep_linux.c|osdep_stub.c|misc.c|xmodifier.c) continue ;;
    esac
    x11_compile "$OBJ" "$SRC/src/$f" "${COMMON[@]}" -I "$SRC/src" -I "$SRC/wrlib" -include ios_prohibited_override.h
done
for f in misc.hack_nf.c xmodifier.hack_nf.c; do
    x11_compile "$OBJ" "$GEN/$f" "${COMMON[@]}" -I "$SRC/src" -I "$SRC/wrlib"
done
x11_compile_report wmaker
STAGE="$X11_ROOT/server/root/usr/X11"
mkdir -p "$STAGE/bin"
fs_link_exe "$STAGE/bin/wmaker" "$OBJ" \
    "$OUT/system/libWINGs.dylib" "$OUT/system/libWUtil.dylib" "$OUT/system/libwraster.dylib" \
    "$OUT/system/libXft.dylib" "$OUT/system/libfontconfig.dylib" "$OUT/system/libXmu.dylib" \
    "$OUT/system/libXext.dylib" "$OUT/system/libX11.dylib" "$OUT/system/libsystem_m.dylib"

# ---- WPrefs (the settings tool) and the helpers it and wmaker run ------
# 2026-10-02: the settings tool and background setting were not built before.
#   WPrefs        WPrefs.app/Makefile.am WPrefs_SOURCES. --with-nolocalegettext-style
#                 layout (configure.ac:894-895): bin/WPrefs, data in
#                 share/WPrefs (RESOURCE_PATH) -- WPrefs.xpm and xpm/*.xpm
#                 (ICON_EXT_XPM, as config.h has no libtiff). WPrefs/xmodifier.c
#                 is the same nested-function file as src/xmodifier.c, so it gets
#                 the same nested-func-to-macro treatment.
#   wmsetbg       util/Makefile.am: the helper wmaker spawns for every background
#                 change (src/misc.c:1000, src/defaults.c:3350) and that WPrefs'
#                 background panel drives. Its "../src/wconfig.h" include is
#                 served from $GEN/src through -I $GEN/util.
#   wdwrite/wdread/getstyle/setstyle: util/Makefile.am, what WPrefs' theme and
#                 style panels shell out to.
# Same libraries as the wmaker link above; wraster has PNG and the built-in XPM
# loader only (config.h), so backgrounds are PNG/XPM or a colour/gradient.
mkdir -p "$GEN/util" "$GEN/src"
cp "$GEN/wconfig.h" "$GEN/src/wconfig.h"
sh "$SRC/script/nested-func-to-macro.sh" "$SRC/WPrefs.app/xmodifier.c" -o "$GEN/wprefs_xmodifier.hack_nf.c" \
    -f "modwarn" -f "modbarf" -f "check_modifier" -f "store_modifier"
WLIBS=("$OUT/system/libWINGs.dylib" "$OUT/system/libWUtil.dylib" "$OUT/system/libwraster.dylib"
    "$OUT/system/libXft.dylib" "$OUT/system/libfontconfig.dylib" "$OUT/system/libXmu.dylib"
    "$OUT/system/libXext.dylib" "$OUT/system/libX11.dylib" "$OUT/system/libremovefile.dylib"
    "$OUT/system/libinfo.dylib" "$OUT/system/libsystem_m.dylib")   # fsync: libremovefile (as libWUtil)
UFLAGS=("${COMMON[@]}" -I "$SRC/src" -I "$SRC/wrlib" -I "$GEN/util" -I "$SRC/util"
    '-DETCDIR="/usr/X11/etc"' '-DDATADIR="/usr/X11/share/WindowMaker"')

OBJ="$X11_ROOT/obj/WPrefs"
rm -rf "$OBJ"; mkdir -p "$OBJ"
X11_OK=0 X11_FAIL=0
for f in $(x11_am_sources "$SRC/WPrefs.app/Makefile.am" WPrefs_SOURCES); do
    [ "$f" = xmodifier.c ] && continue
    x11_compile "$OBJ" "$SRC/WPrefs.app/$f" "${COMMON[@]}" -I "$SRC/WPrefs.app" -I "$SRC/wrlib" \
        '-DRESOURCE_PATH="/usr/X11/share/WPrefs"' '-DWMAKER_RESOURCE_PATH="/usr/X11/share/WindowMaker"' \
        -include ios_prohibited_override.h
done
x11_compile "$OBJ" "$GEN/wprefs_xmodifier.hack_nf.c" "${COMMON[@]}" -I "$SRC/WPrefs.app" -I "$SRC/wrlib"
x11_compile_report WPrefs
fs_link_exe "$STAGE/bin/WPrefs" "$OBJ" "${WLIBS[@]}"
mkdir -p "$STAGE/share/WPrefs/xpm"
cp "$SRC/WPrefs.app/WPrefs.xpm" "$STAGE/share/WPrefs/"
cp "$SRC/WPrefs.app/xpm/"*.xpm "$STAGE/share/WPrefs/xpm/"

# util tools: <name>:<sources>
for t in "wmsetbg:wmsetbg.c" "wdwrite:wdwrite.c" "wdread:wdread.c" \
         "getstyle:getstyle.c fontconv.c" "setstyle:setstyle.c fontconv.c"; do
    name=${t%%:*}
    OBJ="$X11_ROOT/obj/wm_util_$name"
    rm -rf "$OBJ"; mkdir -p "$OBJ"
    X11_OK=0 X11_FAIL=0
    for f in ${t#*:}; do
        x11_compile "$OBJ" "$SRC/util/$f" "${UFLAGS[@]}" -include ios_prohibited_override.h
    done
    x11_compile_report "$name"
    # setstyle's realpath(3) is the DARWIN_EXTSN variant, which this tree's
    # libcopyfile.dylib exports (build_copyfile.sh's -alias list).
    extra=()
    [ "$name" = setstyle ] && extra=("$OUT/system/libcopyfile.dylib")
    fs_link_exe "$STAGE/bin/$name" "$OBJ" ${extra[@]+"${extra[@]}"} "${WLIBS[@]}"
done

# ---- data (what `make install` puts in datadir/pkgconfdir) -------------
# am_data <Makefile.am> <VAR>: every entry of an automake _DATA list.
am_data() {
    awk -v var="$2" '
        cont { buf = buf " " $0; if ($0 !~ /\\$/) { cont = 0; emit(buf) } ; next }
        $0 ~ ("^" var "[ \t]*\\+?=") { buf = $0; if ($0 ~ /\\$/) cont = 1; else emit(buf) }
        function emit(s,  n, a, i) { gsub(/\\/, " ", s); sub(/^[^=]*=/, "", s)
            n = split(s, a, /[ \t]+/); for (i = 1; i <= n; i++) if (a[i] != "" && a[i] !~ /^\$/) print a[i] }
    ' "$1"
}
WMD="$SRC/WindowMaker"
PKGDATA="$STAGE/share/WindowMaker"
PKGCONF="$STAGE/etc/WindowMaker"
rm -rf "$PKGDATA" "$PKGCONF" "$STAGE/share/WINGs"
mkdir -p "$PKGDATA" "$PKGCONF" "$STAGE/share/WINGs"
# WindowMaker/Makefile.am: GENERATED_MENUS via sed (#wprefs#, #wmdatadir#),
# dist_prefsdata_DATA (incl. $(NONGENERATED_MENUS)) copied as-is.
# wpexecbindir = wprefs_bindir = ${bindir} (configure.ac:895).
for m in $(am_data "$WMD/Makefile.am" GENERATED_MENUS); do
    sed -e "s:#wprefs#:$X11_PREFIX/bin/WPrefs:" -e "s:#wmdatadir#:$X11_PREFIX/share/WindowMaker:" \
        "$WMD/$m.in" > "$PKGDATA/$m"
done
for f in $(am_data "$WMD/Makefile.am" dist_prefsdata_DATA) $(am_data "$WMD/Makefile.am" NONGENERATED_MENUS); do
    cp "$WMD/$f" "$PKGDATA/"
done
chmod 755 "$PKGDATA/autostart.sh" "$PKGDATA/exitscript.sh"
# (IconSets is generated below, from WMWindowAttributes.)
for sub in Backgrounds Icons Pixmaps Styles Themes; do
    mkdir -p "$PKGDATA/$sub"
    var=$(awk '/_DATA *=/{print $1; exit}' "$WMD/$sub/Makefile.am")
    for f in $(am_data "$WMD/$sub/Makefile.am" "$var"); do
        cp -R "$WMD/$sub/$f" "$PKGDATA/$sub/"
    done
done
# WindowMaker/Defaults/Makefile.am: ICONEXT=xpm (no libtiff,
# configure.ac:830-832), pkgdatadir/pixmapdir per config-paths.h.
sed -e "s:#extension#:xpm:" "$WMD/Defaults/WMWindowAttributes.in" > "$PKGCONF/WMWindowAttributes"
sed -e "s:#pkgdatadir#:$X11_PREFIX/share/WindowMaker:" -e "s:#pixmapdir#:$X11_PREFIX/share/pixmaps:" \
    "$WMD/Defaults/WindowMaker.in" > "$PKGCONF/WindowMaker"
sed -e "s:#wprefs#:$X11_PREFIX/bin/WPrefs:" "$WMD/Defaults/WMState.in" > "$PKGCONF/WMState"
# Stock Window Maker look: the generated global defaults above, unmodified.
# Their "Sans" fonts resolve to Inter through x11_config/fonts/61-inter-sans.conf
# (build_fonts.sh).
# This port's own menu (only installed programs) instead of upstream's
# plmenu, whose entries (xterm, firefox, gimp, ...) do not exist here.
# (No comments inside the file: Window Maker refuses them in its owned
# domain files, WINGs/proplist.c:887-889. Syntax: WindowMaker/plmenu.in.
# Add an entry there when a new app is installed.)
cp x11_config/wmaker/WMRootMenu "$PKGCONF/WMRootMenu"
# WindowMaker/IconSets/Makefile.am: Default.iconset is the Icon lines of
# WMWindowAttributes wrapped in braces.
mkdir -p "$PKGDATA/IconSets"
{ echo '{'; grep Icon "$PKGCONF/WMWindowAttributes"; echo '}'; } > "$PKGDATA/IconSets/Default.iconset"
cp "$WMD/Defaults/WMGLOBAL" "$PKGCONF/"
# WINGs/Resources/Makefile.am: resdata_DATA -> $(datadir)/WINGs.
for f in $(am_data "$SRC/WINGs/Resources/Makefile.am" resdata_DATA); do
    cp "$SRC/WINGs/Resources/$f" "$STAGE/share/WINGs/"
done

# ---- Terminal.app autostart ---------------------------------------------
# x11_config/session/autostart is wmaker's global init script; it opens
# Terminal.app. The boot session itself (gsdm's login window, the Xsession
# that execs wmaker as the logged-in user, the launchd plist) is staged by
# build_gsdm.sh.
cp "x11_config/session/autostart" "$PKGDATA/autostart"
chmod 755 "$PKGDATA/autostart"
sh -n "$PKGDATA/autostart" || { echo "error: autostart syntax" >&2; exit 1; }
echo "staged $STAGE/{bin/{wmaker,WPrefs,wmsetbg,wdwrite,wdread,getstyle,setstyle},share/{WindowMaker,WPrefs,WINGs},etc/WindowMaker}"
