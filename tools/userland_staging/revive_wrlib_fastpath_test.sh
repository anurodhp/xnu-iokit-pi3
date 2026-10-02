#!/bin/bash
# DAR-437 pixel-diff test for wrlib's fast paths (wrlib/fastpath.h). Host
# build (daily clang; an arm64 mac has NEON, so the real arm_neon.h code runs).
#   ref   = pristine upstream wrlib/{convert,raster,gradient}.c from tag
#           wmaker-0.96.0 (what DAR-437 replaces)
#   new   = working-tree wrlib (NEON where the host has it)
#   newsc = working-tree wrlib, -DWR_NO_NEON (scalar fast path)
# All three write every produced buffer to a file; the files must be
# byte-identical (XImage rows include their padding canary bytes).
set -e
cd "$(dirname "$0")"
. ./x11_common.sh    # TP, OUT, X11_ROOT, X11_INC, X11_CONFIG_DIR
SRC="$TP/wmaker"
W="$SRC/wrlib"
T="$OUT/wrlib_fastpath_test"
rm -rf "$T"; mkdir -p "$T/refsrc" "$T/gen"
for f in convert.c raster.c gradient.c; do
    git -C "$SRC" show "wmaker-0.96.0:wrlib/$f" > "$T/refsrc/$f"
done
# wraster.h exactly as revive_wmaker.sh generates it
sh "$SRC/script/replace-ac-keywords.sh" --header "$X11_CONFIG_DIR/wmaker/config.h" --filter USE_XSHM \
    -o "$T/gen/wraster.h" "$W/wraster.h.in"
HOSTCC="${HOSTCC:-clang}"
CF=(-O2 -g -Wall -Wno-unused -Wno-deprecated-declarations -Wno-macro-redefined
    -I "$X11_CONFIG_DIR/wmaker" -I "$T/gen" -I "$X11_INC")
build() {   # name, extra flags
    local n="$1"; shift
    "$HOSTCC" "${CF[@]}" "$@" wrlib_fastpath_test.c -o "$T/$n" -Wl,-undefined,dynamic_lookup -lm
}
build ref   -DWR_REF -I "$T/refsrc" -I "$W"
build new   -I "$W"
build newsc -DWR_NO_NEON -I "$W"
for n in ref new newsc; do "$T/$n" "$T/$n.out" | head -1; done
rc=0
for n in new newsc; do
    if cmp "$T/ref.out" "$T/$n.out"; then
        echo "PASS: $n == ref ($(stat -f %z "$T/ref.out") bytes compared)"
    else
        echo "FAIL: $n differs from ref"; rc=1
    fi
done
exit $rc
