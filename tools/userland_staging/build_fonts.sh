#!/bin/bash
# Stages the DejaVu fonts for the SD image (DAR-384, X11GS 3) into
# libc_build/fontstack/root/usr/share/fonts, with the fonts.scale and
# fonts.dir an X server's core-font path needs, exactly as PureDarwin's
# nix/pkgs/x11/xvfb-fonts.nix does it (copy the DejaVu TTFs to
# /usr/share/fonts, then `mkfontscale .` and `mkfontdir .` there).
# inject_into_sd_image.sh copies the staged tree onto the image.
#
# Fonts: third_party/dejavu-fonts-ttf-2.37/ttf, the DejaVu 2.37 TTF release
# tarball setup_third_party.sh fetches and SHA-256-checks (see its comment
# for why the tarball, not the git tag). All 22 faces; about 10 MB.
#
# mkfontscale runs on the HOST, at image-build time: fonts.scale/fonts.dir
# depend only on the font files, and the Pi has no reason to regenerate
# them. It is built here from the pinned, unmodified sources:
# mkfontscale-1.2.3 + libfontenc-1.1.9 (setup_third_party.sh) and the same
# pinned FreeType 2.14.3 build_freetype.sh uses (its CMakeLists.txt
# BASE_SRCS, builds/unix/ftsystem.c, the same ftconfig.h/ftoption.h
# substitutions), compiled with the host's own clang and macOS SDK and
# linked against the SDK's libz. config: fontstack_config/mkfontscale/.
# mkfontdir is upstream's own mkfontdir.in wrapper: `mkfontscale -b -s -l`.
#
# Run build_fontconfig.sh first (the defaults staged at the end go into its /etc/fonts).
#
# fontconfig does NOT need either file (it scans the directory itself);
# its cache is left to the target (see build_fontconfig.sh).
set -e
cd "$(dirname "$0")"
TP="../../third_party"
OUT="libc_build"
FS_ROOT="$OUT/fontstack"
FONTS_SRC="$TP/dejavu-fonts-ttf-2.37/ttf"
INTER_SRC="$TP/inter-fonts-4.0/extras/ttf"
INTER_FACES="Regular Italic Medium SemiBold Bold BoldItalic"
JBM_SRC="$TP/jetbrains-mono-2.304/fonts/ttf"
JBM_FACES="Regular Italic Bold BoldItalic"
UNIFONT_OTF="$TP/unifont-16.0.01/unifont-16.0.01.otf"
NOTOEMOJI_TTF="$TP/noto-emoji/NotoEmoji.ttf"
if [ ! -f "$FONTS_SRC/DejaVuSans.ttf" ]; then
    echo "error: $FONTS_SRC not found -- run setup_third_party.sh first" >&2
    exit 1
fi
for f in $INTER_FACES; do
    [ -f "$INTER_SRC/Inter-$f.ttf" ] || { echo "error: $INTER_SRC/Inter-$f.ttf not found -- run setup_third_party.sh first" >&2; exit 1; }
done
for f in $JBM_FACES; do
    [ -f "$JBM_SRC/JetBrainsMono-$f.ttf" ] || { echo "error: $JBM_SRC/JetBrainsMono-$f.ttf not found -- run setup_third_party.sh first" >&2; exit 1; }
done
[ -f "$UNIFONT_OTF" ] || { echo "error: $UNIFONT_OTF not found -- run setup_third_party.sh first" >&2; exit 1; }
[ -f "$NOTOEMOJI_TTF" ] || { echo "error: $NOTOEMOJI_TTF not found -- run setup_third_party.sh first" >&2; exit 1; }
for d in mkfontscale libfontenc freetype; do
    [ -d "$TP/$d" ] || { echo "error: $TP/$d not found -- run setup_third_party.sh first" >&2; exit 1; }
done
if [ ! -f "$OUT/x11/include/X11/Xos.h" ]; then
    echo "error: $OUT/x11/include/X11/Xos.h not found -- run build_xorg_headers.sh first" >&2
    exit 1
fi
HOSTCC="$(xcrun --sdk macosx -f clang)"
HOSTSDK="$(xcrun --sdk macosx --show-sdk-path)"

# --- host mkfontscale ------------------------------------------------
HOST="$FS_ROOT/host"
rm -rf "$HOST"
mkdir -p "$HOST/obj" "$HOST/ftgen/freetype/config"
FT="$TP/freetype"
sed -E 's/#undef +(HAVE_UNISTD_H)/#define \1 1/; s/#undef +(HAVE_FCNTL_H)/#define \1 1/' \
    "$FT/builds/unix/ftconfig.h.in" > "$HOST/ftgen/freetype/config/ftconfig.h"
sed -E 's@/\* +(#define +FT_CONFIG_OPTION_SYSTEM_ZLIB) +\*/@\1@' \
    "$FT/include/freetype/config/ftoption.h" > "$HOST/ftgen/freetype/config/ftoption.h"
HCFLAGS=(-isysroot "$HOSTSDK" -c -O1 -w)
FTSRCS=$(awk '/^set\(BASE_SRCS/{f=1; next} f && /\)/{f=0} f {print $1}' "$FT/CMakeLists.txt")
for f in $FTSRCS builds/unix/ftsystem.c src/base/ftdebug.c; do
    o="$HOST/obj/ft_$(echo "${f%.c}" | tr '/' '_').o"
    "$HOSTCC" "${HCFLAGS[@]}" -DFT2_BUILD_LIBRARY -I "$HOST/ftgen" -I "$FT/include" \
        -I "$HOST/ftgen/freetype/config" "$FT/$f" -o "$o"
done
FE="$TP/libfontenc"
for f in encparse fontenc reallocarray; do
    "$HOSTCC" "${HCFLAGS[@]}" -DHAVE_CONFIG_H -I fontstack_config/mkfontscale -I "$FE/include" \
        -I "$OUT/x11/include" \
        -DFONT_ENCODINGS_DIRECTORY=\"/usr/X11/share/fonts/encodings/encodings.dir\" \
        "$FE/src/$f.c" -o "$HOST/obj/fe_$f.o"
done
MF="$TP/mkfontscale"
for f in constlist hash ident list mkfontscale; do
    "$HOSTCC" "${HCFLAGS[@]}" -DHAVE_CONFIG_H -D_BSD_SOURCE -I fontstack_config/mkfontscale \
        -I "$FE/include" -I "$OUT/x11/include" -I "$HOST/ftgen" -I "$FT/include" \
        "$MF/$f.c" -o "$HOST/obj/mf_$f.o"
done
"$HOSTCC" -isysroot "$HOSTSDK" -o "$HOST/mkfontscale" "$HOST/obj"/*.o -lz
echo "built host $HOST/mkfontscale ($("$HOST/mkfontscale" -v))"

# --- stage /usr/share/fonts ------------------------------------------
FONTS="$FS_ROOT/root/usr/share/fonts"
rm -rf "$FONTS"
mkdir -p "$FONTS"
cp "$FONTS_SRC"/*.ttf "$FONTS/"
for f in $INTER_FACES; do cp "$INTER_SRC/Inter-$f.ttf" "$FONTS/"; done
for f in $JBM_FACES; do cp "$JBM_SRC/JetBrainsMono-$f.ttf" "$FONTS/"; done
cp "$UNIFONT_OTF" "$NOTOEMOJI_TTF" "$FONTS/"
chmod 644 "$FONTS"/*.ttf "$FONTS"/*.otf
"$HOST/mkfontscale" "$FONTS"             # mkfontscale .  -> fonts.scale
"$HOST/mkfontscale" -b -s -l "$FONTS"    # mkfontdir .    -> fonts.dir
n=$(ls "$FONTS"/*.ttf | wc -l | tr -d ' ')
[ "$n" -eq 33 ] && [ "$(ls "$FONTS"/*.otf | wc -l | tr -d ' ')" -eq 1 ] || {
    echo "error: expected 22 DejaVu + 6 Inter + 4 JetBrains Mono + NotoEmoji TTFs and the Unifont OTF, staged $n TTFs" >&2; exit 1; }
[ "$(head -1 "$FONTS/fonts.scale")" -gt 0 ] && [ "$(head -1 "$FONTS/fonts.dir")" -gt 0 ] || {
    echo "error: fonts.scale/fonts.dir came out empty" >&2; exit 1; }
echo "staged $n fonts in $FONTS ($(du -sk "$FONTS" | awk '{print $1}') KB): fonts.scale $(head -1 "$FONTS/fonts.scale") entries, fonts.dir $(head -1 "$FONTS/fonts.dir") entries"

# --- font defaults (Mac-like look) -------------------------------------
# Inter for sans-serif / system UI, JetBrains Mono for monospace (DejaVu Sans Mono and, for
# every script and emoji they lack, Unifont and Noto Emoji behind it). Run
# build_fontconfig.sh first: it wipes and recreates /etc/fonts, so the
# conf.d file below has to be added after it.
# Sources: x11_config/fonts/61-inter-sans.conf (fontconfig; Window Maker's
# "Sans" and GNUstep's cairo backend both resolve through it) and
# x11_config/gnustep/GlobalDefaults.plist (GNUstep system-wide defaults,
# read from next to GNUstep.conf -- gnustep-base NSUserDefaults.m: the
# GlobalDefaults.plist "stored in the same directory as the system wide
# GNUstep.conf file"; NSFont roles).
[ -d "$FS_ROOT/root/etc/fonts/conf.d" ] || {
    echo "error: $FS_ROOT/root/etc/fonts/conf.d missing -- run build_fontconfig.sh first" >&2; exit 1; }
cp x11_config/fonts/61-inter-sans.conf "$FS_ROOT/root/etc/fonts/conf.d/61-inter-sans.conf"
chmod 644 "$FS_ROOT/root/etc/fonts/conf.d/61-inter-sans.conf"
mkdir -p "$FS_ROOT/root/etc/GNUstep"
cp x11_config/gnustep/GlobalDefaults.plist "$FS_ROOT/root/etc/GNUstep/GlobalDefaults.plist"
chmod 644 "$FS_ROOT/root/etc/GNUstep/GlobalDefaults.plist"
plutil -lint "$FS_ROOT/root/etc/GNUstep/GlobalDefaults.plist" >/dev/null 2>&1 ||
    { echo "error: GlobalDefaults.plist is not a valid old-style plist for the host parser" >&2; }
echo "staged fontconfig 61-inter-sans.conf and GNUstep GlobalDefaults.plist"
