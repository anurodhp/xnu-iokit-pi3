#!/bin/bash
# Builds libc_build/system/libfontconfig.dylib (install name
# /usr/X11/lib/libfontconfig.1.dylib), the fc-* tools
# (libc_build/fontstack/bin/fc-*.macho) and fontconfig's runtime data tree
# (libc_build/fontstack/root/: /etc/fonts, /usr/X11/share/fontconfig,
# /usr/X11/share/xml/fontconfig, /var/cache/fontconfig) from real,
# unmodified fontconfig 2.17.1 (third_party/fontconfig,
# gitlab.freedesktop.org/fontconfig/fontconfig tag 2.17.1 -- the version
# nixpkgs nixos-26.05 rev 8f0500b9 ships). DAR-384, X11GS 3.
# Helpers: fontstack_common.sh / x11_common.sh.
#
# Follows fontconfig's own meson build (PureDarwin's recipe,
# nix/pkgs/x11/fontconfig.nix, runs the same meson.build) with these
# options: prefix /usr/X11 (Apple's X11 prefix, where Apple's 10.5-10.7
# X11 put fontconfig), sysconfdir /etc and localstatedir /var (so the
# config is /etc/fonts/fonts.conf, as the ticket asks, and the cache is
# /var/cache/fontconfig), default-fonts-dirs=/usr/share/fonts (where the
# DejaVu fonts go; meson.build:327-340's darwin default would instead list
# /System/Library/Fonts and friends, which do not exist here),
# additional-fonts-dirs=no, xml-backend=expat (build_expat.sh; Apple's
# own expat, see that script), iconv and nls disabled, tests/docs off.
#
#   config.h     -- the tree's own meson-config.h.in (includes
#                   meson-config.h + config-fixups.h), with
#                   fontstack_config/fontconfig/meson-config.h standing in
#                   for the configure_file() output; every define there
#                   cites its meson.build line
#   fontconfig.h -- fontconfig/fontconfig.h.in with CACHE_VERSION 9
#                   (fontconfig/meson.build, meson.build:28)
#   fcstdint.h   -- src/fcstdint.h.in copied (meson.build:477)
#   fccase.h     -- fc-case/fc-case.py (fc-case/meson.build)
#   fclang.h     -- fc-lang/fc-lang.py over fc-lang/meson.build's orth_files
#   fcalias*.h, fcftalias*.h -- src/makealias.py (meson.build:486-498)
#   fcobjshash.h -- clang -E src/fcobjshash.gperf.h | src/cutout.py | gperf
#                   --pic -m 100 (src/meson.build)
#   library      -- src/meson.build fcpat.c + fc_sources; soversion 1,
#                   darwin_versions 17 (meson.build:23-27: soversion = 2-1,
#                   osxversion = 17-1+1), linked against libfreetype,
#                   libexpat, libsystem_m (math_dep)
#   tools        -- fc-cache fc-cat fc-conflist fc-list fc-match
#                   fc-pattern fc-query fc-scan fc-validate. This tree has
#                   no dylib that exports getopt/getopt_long, so, like
#                   build_configd.sh/build_cron.sh, each tool links Libc's
#                   own stdlib/FreeBSD/getopt.c + getopt_long.c objects
#                   and is compiled with HAVE_GETOPT/HAVE_GETOPT_LONG=1.
#   data         -- fonts.conf from fonts.conf.in (meson.build:582-587),
#                   conf.d/*.conf into the template dir with the
#                   conf.d/meson.build conf_links (hinting slight,
#                   sub-pixel none, bitmap no-except-emoji) as relative
#                   symlinks in /etc/fonts/conf.d, the same links
#                   conf.d/link_confs.py makes; 35-lang-normalize.conf from
#                   conf.d/write-35-lang-normalize-conf.py; fonts.dtd; and
#                   an empty /var/cache/fontconfig (meson.build:610-613)
#
# Cache policy (DAR-384): no fc-cache run on the host. Cache files are
# per-architecture and are validated against each font directory's mtime,
# which the copy into the HFS image changes, and the host's fontconfig is
# a different version. Instead the target builds it itself: the first
# fontconfig client (or `fc-cache` from the root shell) that scans
# /usr/share/fonts writes /var/cache/fontconfig/<md5>-le64.cache-9 there
# (FcDirCacheScan -> FcDirCacheWrite in src/fccache.c).
set -e
cd "$(dirname "$0")"
. ./fontstack_common.sh

SRC="$TP/fontconfig"
x11_require_source "$SRC/meson.build"
[ -f "$OUT/system/libfreetype.dylib" ] || { echo "error: $OUT/system/libfreetype.dylib not found -- run build_freetype.sh first" >&2; exit 1; }
[ -f "$OUT/system/libexpat.dylib" ] || { echo "error: $OUT/system/libexpat.dylib not found -- run build_expat.sh first" >&2; exit 1; }
[ -f "$OUT/system/libsystem_m.dylib" ] || { echo "error: $OUT/system/libsystem_m.dylib not found -- run build_libm_dylib.sh first" >&2; exit 1; }
[ -f "$OUT/system/libcopyfile.dylib" ] || { echo "error: $OUT/system/libcopyfile.dylib not found -- run build_copyfile_dylib.sh first" >&2; exit 1; }
[ -f "$OUT/system/libremovefile.dylib" ] || { echo "error: $OUT/system/libremovefile.dylib not found -- run build_removefile_dylib.sh first" >&2; exit 1; }
for d in libsystem_kernel libsystem_c libsystem_pthread libsystem_malloc libsystem_platform libdyld; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2; exit 1; }
done
x11_require_sys_dylibs
grep -q "version: '2.17.1'" "$SRC/meson.build" || { echo "error: fontconfig is not 2.17.1 -- recheck this script" >&2; exit 1; }
command -v gperf >/dev/null || { echo "error: gperf not found on the host (fcobjshash.h needs it)" >&2; exit 1; }
PY=$(command -v python3) || { echo "error: python3 not found on the host" >&2; exit 1; }

OBJ="$FS_ROOT/obj/fontconfig"
GEN="$FS_ROOT/gen/fontconfig"
ROOT="$FS_ROOT/root"
BIN="$FS_ROOT/bin"
rm -rf "$OBJ" "$GEN" "$ROOT/etc/fonts" "$ROOT/usr/X11/share/fontconfig" "$ROOT/usr/X11/share/xml/fontconfig" \
    "$ROOT/var/cache/fontconfig"
mkdir -p "$OBJ/lib" "$OBJ/getopt" "$GEN/fontconfig" "$BIN"

# --- generated headers ------------------------------------------------
cp "$SRC/meson-config.h.in" "$GEN/config.h"
cp "$FS_CONFIG_DIR/fontconfig/meson-config.h" "$GEN/meson-config.h"
sed 's/@CACHE_VERSION@/9/' "$SRC/fontconfig/fontconfig.h.in" > "$GEN/fontconfig/fontconfig.h"
cp "$SRC/src/fcstdint.h.in" "$GEN/fcstdint.h"
# fcstr.c includes "../fc-case/fccase.h"; meson's build dir has it at
# <build>/fc-case/fccase.h next to <build>/src, so mirror that layout.
mkdir -p "$GEN/fc-case" "$GEN/src"
"$PY" "$SRC/fc-case/fc-case.py" "$SRC/fc-case/CaseFolding.txt" \
    --template "$SRC/fc-case/fccase.tmpl.h" --output "$GEN/fc-case/fccase.h"
ORTHS=$(awk '/^orth_files = \[/{f=1; next} f && /^\]/{f=0} f {gsub(/[ ,\x27]/, ""); if ($0 != "") print}' "$SRC/fc-lang/meson.build")
[ "$(echo "$ORTHS" | wc -l)" -gt 200 ] || { echo "error: could not read orth_files from fc-lang/meson.build" >&2; exit 1; }
# shellcheck disable=SC2086
"$PY" "$SRC/fc-lang/fc-lang.py" $ORTHS --template "$SRC/fc-lang/fclang.tmpl.h" \
    --output "$GEN/fclang.h" --directory "$SRC/fc-lang"
"$PY" "$SRC/src/makealias.py" "$SRC/src" "$GEN/fcalias.h" "$GEN/fcaliastail.h" \
    "$GEN/fontconfig/fontconfig.h" "$SRC/src/fcdeprecate.h" "$SRC/fontconfig/fcprivate.h"
"$PY" "$SRC/src/makealias.py" "$SRC/src" "$GEN/fcftalias.h" "$GEN/fcftaliastail.h" \
    "$SRC/fontconfig/fcfreetype.h"
INCS=(-I "$GEN" -I "$GEN/src" -I "$SRC" -I "$SRC/fc-lang" -I "$SRC/src" -I "$X11_INC/freetype2")
# -P: meson's cc.preprocess() emits no linemarkers, and cutout.py/gperf
# expect none.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -E -P -DHAVE_CONFIG_H "${INCS[@]}" \
    "$SRC/src/fcobjshash.gperf.h" -o "$GEN/fcobjshash.gperf.i"
"$PY" "$SRC/src/cutout.py" "$GEN/fcobjshash.gperf.i" "$GEN/fcobjshash.gperf"
gperf --pic -m 100 "$GEN/fcobjshash.gperf" --output-file "$GEN/fcobjshash.h"

# --- library ---------------------------------------------------------
FC_SRCS=$(awk '/^fc_sources = files\(\[/{f=1; next} f && /^\]\)/{f=0} f {gsub(/[ ,\x27]/, ""); if ($0 != "") print}' "$SRC/src/meson.build")
[ "$(echo "$FC_SRCS" | wc -l)" -ge 25 ] || { echo "error: could not read fc_sources from src/meson.build" >&2; exit 1; }
X11_OK=0; X11_FAIL=0
for f in fcpat.c $FC_SRCS; do
    x11_compile "$OBJ/lib" "$SRC/src/$f" "${INCS[@]}"
done
x11_compile_report fontconfig

mkdir -p "$X11_INC/fontconfig"
cp "$GEN/fontconfig/fontconfig.h" "$SRC/fontconfig/fcfreetype.h" "$SRC/fontconfig/fcprivate.h" "$X11_INC/fontconfig/"

# Link-line owners for two libc calls that no libsystem_* dylib exports
# here yet (the same convention DAR-383 used for rename/sendto/etc.):
# realpath$DARWIN_EXTSN (fccfg.c) comes from libcopyfile.dylib and
# rename (fcatomic.c) from libremovefile.dylib -- that is who owns each of
# them in this tree today. Real Apple's owners are libsystem_c and
# libsystem_kernel.
fs_link_dylib libfontconfig.dylib "$X11_PREFIX/lib/libfontconfig.1.dylib" 17 17 "$OBJ/lib" \
    "$OUT/system/libfreetype.dylib" "$OUT/system/libexpat.dylib" "$OUT/system/libsystem_m.dylib" \
    "$OUT/system/libcopyfile.dylib" "$OUT/system/libremovefile.dylib"

# --- tools -----------------------------------------------------------
LIBC="$TP/Libc"
X11_OK=0; X11_FAIL=0
for f in getopt getopt_long; do
    x11_compile "$OBJ/getopt" "$LIBC/stdlib/FreeBSD/$f.c" -UHAVE_CONFIG_H \
        -I "$LIBC/fbsdcompat" -I "$LIBC/darwin" -I "$LIBC/gen"
done
x11_compile_report "fontconfig tools: Libc getopt"
TOOLS="fc-cache fc-cat fc-conflist fc-list fc-match fc-pattern fc-query fc-scan fc-validate"
for t in $TOOLS; do
    rm -rf "$OBJ/$t"; mkdir -p "$OBJ/$t"
    X11_OK=0; X11_FAIL=0
    x11_compile "$OBJ/$t" "$SRC/$t/$t.c" "${INCS[@]}" -DHAVE_GETOPT=1 -DHAVE_GETOPT_LONG=1
    x11_compile_report "$t"
    cp "$OBJ/getopt/"*.o "$OBJ/$t/"
    fs_link_exe "$BIN/$t.macho" "$OBJ/$t" "$OUT/system/libfontconfig.dylib" "$OUT/system/libfreetype.dylib"
done

# --- runtime data ----------------------------------------------------
TEMPLATEDIR="$ROOT/usr/X11/share/fontconfig/conf.avail"
mkdir -p "$ROOT/etc/fonts/conf.d" "$TEMPLATEDIR" "$ROOT/usr/X11/share/xml/fontconfig" "$ROOT/var/cache/fontconfig"
# fonts.conf.in's @FC_DEFAULT_FONTS@/@FC_FONTPATH@/@FC_CACHEDIR@/@CONFIGDIR@,
# set exactly as meson.build:327-428 sets fonts_conf (CONFIGDIR relative
# to /etc/fonts, :426-429).
"$PY" - "$SRC/fonts.conf.in" "$ROOT/etc/fonts/fonts.conf" <<'EOF'
import sys
s = open(sys.argv[1], encoding='utf-8').read()
for k, v in (('FC_DEFAULT_FONTS', '\t<dir>/usr/share/fonts</dir>\n'), ('FC_FONTPATH', ''),
             ('FC_CACHEDIR', '/var/cache/fontconfig'), ('CONFIGDIR', 'conf.d')):
    s = s.replace('@' + k + '@', v)
assert '@' not in s.replace('@@', ''), 'unsubstituted @VAR@ left in fonts.conf'
open(sys.argv[2], 'w', encoding='utf-8').write(s)
EOF
CONF_FILES=$(awk '/^conf_files = \[/{f=1; next} f && /^\]/{f=0} f {gsub(/[ ,\x27]/, ""); if ($0 != "") print}' "$SRC/conf.d/meson.build")
CONF_LINKS=$(awk '/^conf_links = \[/{f=1; next} f && /^\]/{f=0} f {gsub(/[ ,\x27]/, ""); if ($0 != "") print}' "$SRC/conf.d/meson.build")
for c in $CONF_FILES; do cp "$SRC/conf.d/$c" "$TEMPLATEDIR/"; done
LANGS=$(echo "$ORTHS" | sed 's/\.orth$//' | grep -v _ | paste -sd, -)
"$PY" "$SRC/conf.d/write-35-lang-normalize-conf.py" "$LANGS" "$TEMPLATEDIR/35-lang-normalize.conf"
# The three option-dependent links (conf.d/meson.build, after conf_links):
# default-hinting=slight, default-sub-pixel-rendering=none,
# bitmap-conf=no-except-emoji -- meson.options' defaults.
for c in $CONF_LINKS 10-hinting-slight.conf 10-sub-pixel-none.conf 70-no-bitmaps-except-emoji.conf; do
    [ -f "$TEMPLATEDIR/$c" ] || { echo "error: conf.d link target $c missing" >&2; exit 1; }
    ln -s "../../../usr/X11/share/fontconfig/conf.avail/$c" "$ROOT/etc/fonts/conf.d/$c"
done
cp "$SRC/fonts.dtd" "$ROOT/usr/X11/share/xml/fontconfig/"
echo "fontconfig data: $(ls "$TEMPLATEDIR" | wc -l | tr -d ' ') templates, $(ls "$ROOT/etc/fonts/conf.d" | wc -l | tr -d ' ') enabled in /etc/fonts/conf.d"
