#!/bin/bash
# Mbed TLS 3.6.4 (third_party/mbedtls, github.com/Mbed-TLS/mbedtls tag v3.6.4,
# Apache-2.0), the TLS FFmpeg uses for https:// and HLS-over-https streams in
# Venice (build_ffmpeg.sh --enable-mbedtls):
#   /usr/lib/libmbedcrypto.16.dylib, libmbedx509.7.dylib, libmbedtls.21.dylib
# (the SOVERSIONs of library/CMakeLists.txt:303-319). Headers in
# libc_build/media/include/{mbedtls,psa}.
#
# Same shape as build_libvterm.sh: upstream's own source lists (src_crypto,
# src_x509, src_tls in library/CMakeLists.txt), compiled with the stock
# config (include/mbedtls/mbedtls_config.h, unmodified), three real dylib
# links. Entropy comes from /dev/urandom (platform_util/entropy_poll.c's
# non-Linux path).
#
# No ARMv8 crypto instructions: the Pi 3's Cortex-A53 lacks the Cryptography
# Extension, which clang's arm64-apple-ios target assumes. The stock config
# is kept and mbedtls_config/pi3_user_config.h (MBEDTLS_USER_CONFIG_FILE)
# turns the AES/SHA instruction paths off; the code is compiled for
# cortex-a53+nocrypto as well, and the link is checked for any AES, SHA or
# PMULL instruction left (an https:// open died with SIGILL before this).
set -euo pipefail
cd "$(dirname "$0")"
. ./media_common.sh
# media_common.sh links against these; for gen_build_order.py, which reads only build_*.sh:
# run build_libsystem.sh build_libinfo.sh build_libresolv_dylib.sh build_libm_dylib.sh build_libcxx_dylib.sh build_zlib.sh build_freetype.sh build_fontconfig.sh first
SRC="$PWD/$TP/mbedtls"
x11_require_source "$SRC/library/CMakeLists.txt"
grep -q 'VERSION 3.6.4 SOVERSION 16' "$SRC/library/CMakeLists.txt" || { echo "error: mbedtls is not 3.6.4 -- recheck this script" >&2; exit 1; }
eval "$(python3 -I - "$SRC/library/CMakeLists.txt" <<'PY'
import re, sys
t = open(sys.argv[1]).read()
for v in ("src_crypto", "src_x509", "src_tls"):
    m = re.search(r'set\(%s\s+(.*?)\)' % v, t, re.S)
    print('%s="%s"' % (v, " ".join(x for x in m.group(1).split() if x.endswith(".c"))))
PY
)"
OBJ="$MEDIA_GEN/mbedtls"
rm -rf "$OBJ"
for lib in crypto x509 tls; do
    mkdir -p "$OBJ/$lib"
    eval "files=\$src_$lib"
    for f in $files; do
        "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c "${MEDIA_CFLAGS[@]}" -Wall \
            -mcpu=cortex-a53+nocrypto -DMBEDTLS_USER_CONFIG_FILE="\"$PWD/mbedtls_config/pi3_user_config.h\"" \
            -I "$SRC/include" -I "$SRC/library" "$SRC/library/$f" -o "$OBJ/$lib/${f%.c}.o" 2> "$OBJ/$lib/${f%.c}.err" ||
            { cat "$OBJ/$lib/${f%.c}.err" >&2; echo "error: $f failed to compile" >&2; exit 1; }
    done
done
# mbedlink <lib> <soversion> [dylibs it links...]
mbedlink() {
    local lib="$1" so="$2" out="$MEDIA_SYS/libmbed$1.dylib"
    shift 2
    rm -f "$out"
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib "${MEDIA_LDFLAGS[@]}" \
        -install_name "/usr/lib/libmbed$lib.$so.dylib" -compatibility_version 1 -current_version 3.6.4 \
        -o "$out" "$OBJ/$lib"/*.o "$@" "${MEDIA_SYSLIBS[@]}"
    otool -hv "$out" | grep -q " DYLIB " || { echo "error: $out is not MH_DYLIB" >&2; exit 1; }
    local dis n
    dis="$(xcrun -f llvm-objdump)"
    n=$("$dis" -d --no-show-raw-insn "$out" | grep -ciE "\s(aes[edm]|aesimc|sha1[chmpsu]|sha256h|sha256su|sha512|pmull)" || true)
    [ "$n" -eq 0 ] || { echo "error: $out has $n ARMv8 crypto instructions (SIGILL on the Pi 3)" >&2; exit 1; }
    echo "wrote $out (/usr/lib/libmbed$lib.$so.dylib)"
    ./bind_target_audit.sh "$out"
}
mbedlink crypto 16
mbedlink x509 7 "$MEDIA_SYS/libmbedcrypto.dylib"
mbedlink tls 21 "$MEDIA_SYS/libmbedx509.dylib" "$MEDIA_SYS/libmbedcrypto.dylib"
rm -rf "$MEDIA_INC/mbedtls" "$MEDIA_INC/psa"
cp -R "$SRC/include/mbedtls" "$SRC/include/psa" "$MEDIA_INC/"
# what FFmpeg and anything else including these headers sees is the config the
# library was built with: the stock one with the user config appended
{ echo; echo "/* build_mbedtls.sh: this system's user config, as the library was built */"
  cat mbedtls_config/pi3_user_config.h; } >> "$MEDIA_INC/mbedtls/mbedtls_config.h"
media_pc mbedtls 3.6.4 "-I$MEDIA_INC" "$MEDIA_SYS/libmbedtls.dylib $MEDIA_SYS/libmbedx509.dylib $MEDIA_SYS/libmbedcrypto.dylib"
