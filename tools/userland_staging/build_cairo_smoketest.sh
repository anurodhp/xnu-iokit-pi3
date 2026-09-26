#!/bin/bash
# Builds cairo_smoketest.macho (DAR-384, X11GS 3), the QEMU check for the
# font/2D stack: see cairo_smoketest.c's header. Installed on the SD image
# at /usr/X11/bin/cairo_smoketest by inject_into_sd_image.sh.
#
# Link recipe: fontstack_common.sh's fs_link_exe (the same Csu +
# LC_LOAD_DYLINKER/LC_LOAD_DYLIB/LC_MAIN assertions and
# bind_target_audit.sh gate as build_x11_smoketest.sh), then a check that
# no image in the process has a flat-namespace reference.
#
# Host reference render: if the host has a cairo with pkg-config files
# (Homebrew), the same cairo_smoketest.c is also built for the host
# (-DCAIRO_SMOKETEST_HOST, which drops the expat/Xft version lines only)
# and run against the SAME staged DejaVu files through a generated
# fonts.conf, writing libc_build/fontstack/host_render/{host.png,host.txt}.
# The target's output can then be compared against it: same ink bbox and
# pixel count expected, identical pixels only if the host's cairo/pixman/
# FreeType versions render identically (they are not pinned to ours).
set -e
cd "$(dirname "$0")"
. ./fontstack_common.sh

DYLIBS=(libcairo libpixman-1 libfontconfig libfreetype libpng16 libz libexpat libXft)
[ -f "$OUT/system/libcairo.dylib" ] || { echo "error: $OUT/system/libcairo.dylib not found -- run build_cairo.sh first" >&2; exit 1; }
[ -f "$OUT/system/libpixman-1.dylib" ] || { echo "error: $OUT/system/libpixman-1.dylib not found -- run build_pixman.sh first" >&2; exit 1; }
[ -f "$OUT/system/libfontconfig.dylib" ] || { echo "error: $OUT/system/libfontconfig.dylib not found -- run build_fontconfig.sh first" >&2; exit 1; }
[ -f "$OUT/system/libfreetype.dylib" ] || { echo "error: $OUT/system/libfreetype.dylib not found -- run build_freetype.sh first" >&2; exit 1; }
[ -f "$OUT/system/libpng16.dylib" ] || { echo "error: $OUT/system/libpng16.dylib not found -- run build_libpng.sh first" >&2; exit 1; }
[ -f "$OUT/system/libz.dylib" ] || { echo "error: $OUT/system/libz.dylib not found -- run build_zlib.sh first" >&2; exit 1; }
[ -f "$OUT/system/libexpat.dylib" ] || { echo "error: $OUT/system/libexpat.dylib not found -- run build_expat.sh first" >&2; exit 1; }
[ -f "$OUT/system/libXft.dylib" ] || { echo "error: $OUT/system/libXft.dylib not found -- run build_libXft.sh first" >&2; exit 1; }
for d in libsystem_kernel libsystem_c libsystem_pthread libsystem_malloc libsystem_platform libdyld; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2; exit 1; }
done
x11_require_sys_dylibs

OBJ="$FS_ROOT/obj/cairo_smoketest"
rm -rf "$OBJ"
mkdir -p "$OBJ"
X11_OK=0; X11_FAIL=0
x11_compile "$OBJ" cairo_smoketest.c -UHAVE_CONFIG_H \
    -I "$X11_INC/freetype2" -I "$X11_INC/pixman-1" -I "$X11_INC/libpng16"
x11_compile_report cairo_smoketest

LINK=()
for l in "${DYLIBS[@]}"; do LINK+=("$OUT/system/$l.dylib"); done
fs_link_exe cairo_smoketest.macho "$OBJ" "${LINK[@]}"
for l in "${DYLIBS[@]}"; do
    otool -L cairo_smoketest.macho | grep -q "/$l\.[0-9]*\.dylib\|/$l\.dylib" || {
        echo "error: cairo_smoketest: $l is not a load-time dependency" >&2; exit 1; }
done
flat=0
for img in cairo_smoketest.macho "${LINK[@]}"; do
    if nm -m "$img" | grep -q 'dynamically looked up'; then
        echo "error: $img has flat-namespace (dynamically looked up) references:" >&2
        nm -m "$img" | grep 'dynamically looked up' >&2
        flat=1
    fi
done
[ "$flat" -eq 0 ] || exit 1
echo "cairo_smoketest.macho: dynamic-executable assertions OK; no flat-namespace references in it or any font/2D dylib"

# --- optional host reference render ------------------------------------
FONTS="$FS_ROOT/root/usr/share/fonts"
if command -v pkg-config >/dev/null && pkg-config --exists cairo fontconfig freetype2 pixman-1 libpng zlib 2>/dev/null \
        && [ -f "$FONTS/DejaVuSans.ttf" ]; then
    HR="$FS_ROOT/host_render"
    rm -rf "$HR"
    mkdir -p "$HR/cache"
    # shellcheck disable=SC2046
    xcrun --sdk macosx clang -O1 -DCAIRO_SMOKETEST_HOST -o "$HR/cairo_smoketest_host" cairo_smoketest.c \
        -I "$(pkg-config --variable=includedir cairo)" \
        $(pkg-config --cflags --libs cairo fontconfig freetype2 pixman-1 libpng zlib)
    cat > "$HR/fonts.conf" <<CONF
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<fontconfig>
  <dir>$(cd "$FONTS" && pwd)</dir>
  <cachedir>$(cd "$HR/cache" && pwd)</cachedir>
</fontconfig>
CONF
    FONTCONFIG_FILE="$(cd "$HR" && pwd)/fonts.conf" "$HR/cairo_smoketest_host" "$HR/host.png" | tee "$HR/host.txt"
else
    echo "= no host cairo/fontconfig pkg-config files (or build_fonts.sh not run) -- skipping the host reference render"
fi
