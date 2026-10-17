#!/bin/bash
# curl(1) and libcurl: Apple's own curl, apple-oss-distributions curl tag
# curl-166.0.0.0.1 (upstream curl 8.7.1, curl/include/curl/curlver.h) from
# our fork anurodhp/curl iokit-port, which adds upstream's b679efc so
# mbedtls_ssl_setup() runs after the RNG is set (Mbed TLS 3.6 requires it), with
# Mbed TLS (build_mbedtls.sh) for https:// in place of Apple's SecureTransport:
#   /usr/lib/libcurl.4.dylib   (install name, compatibility and current version
#                               from xcconfigs/libcurl-version.xcconfig: 4, 7, 9)
#   libc_build/curl.macho      -> /usr/bin/curl (inject_into_sd_image.sh)
# The CA bundle it trusts is /etc/ssl/cert.pem, where Apple's own macOS build
# looks (config_mac/curl_config.h:8); setup_third_party.sh fetches Mozilla's
# bundle (curl.se's cacert.pem, pinned by date and SHA-256) and
# inject_into_sd_image.sh installs it there.
#
# Why not the macos-112 manifest's curl-121.40.2: that is curl 7.64.1, whose
# lib/vtls/mbedtls.c:36-41 includes <mbedtls/net.h> and <mbedtls/certs.h>,
# removed in Mbed TLS 3 (third_party/mbedtls/include/mbedtls has neither),
# and Apple builds it on SecureTransport (Security.framework), which this
# port does not have. curl-166.0.0.0.1 is Apple's newest tag; its mbedtls.c
# supports Mbed TLS 3 and caps the protocol at TLS 1.2 (lib/vtls/mbedtls.c:
# 305-306), which needs no psa_crypto_init().
#
# The config is Apple's own iOS one (config_iphone/curl_config.h, what its
# Xcode project builds iOS curl with), copied unmodified and then changed
# only where this system differs, each line below with its reason. Sources
# are curl's own lists (lib/Makefile.inc LIB_*_CFILES, src/Makefile.inc
# CURL_CFILES + CURLX_CFILES, as README.APPLE says the Xcode targets use).
# Same toolchain and link recipe as the media stack (media_common.sh), whose
# Mbed TLS and zlib it links.
set -euo pipefail
cd "$(dirname "$0")"
. ./media_common.sh
# for gen_build_order.py, which reads only build_*.sh:
# run build_libsystem.sh build_libinfo.sh build_libresolv_dylib.sh build_zlib.sh build_mbedtls.sh first
SRC="$PWD/$TP/curl"
C="$SRC/curl"
x11_require_source "$C/include/curl/curlver.h"
grep -q 'define LIBCURL_VERSION "8.7.1"' "$C/include/curl/curlver.h" ||
    { echo "error: third_party/curl is not curl 8.7.1 (anurodhp/curl iokit-port) -- recheck this script" >&2; exit 1; }
for d in libmbedtls libmbedx509 libmbedcrypto libz; do
    [ -f "$MEDIA_SYS/$d.dylib" ] || { echo "error: $MEDIA_SYS/$d.dylib not found -- build it first" >&2; exit 1; }
done
[ -f "$MEDIA_INC/mbedtls/ssl.h" ] || { echo "error: Mbed TLS headers not staged -- run build_mbedtls.sh" >&2; exit 1; }

OBJ="$MEDIA_GEN/curl"
rm -rf "$OBJ"
mkdir -p "$OBJ/config" "$OBJ/lib" "$OBJ/src"

# Apple's iOS config, then this system's differences
cp "$SRC/config_iphone/curl_config.h" "$OBJ/config/curl_config.h"
python3 -I - "$OBJ/config/curl_config.h" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
changes = [
    # SecureTransport is Security.framework, not built here: Mbed TLS instead
    ("#define USE_SECTRANSP 1", "/* #undef USE_SECTRANSP */\n#define USE_MBEDTLS 1"),
    # no nghttp2 on this system: HTTP/1.1 only
    ("#define USE_NGHTTP2 1", "/* #undef USE_NGHTTP2 */"),
    # Mbed TLS has no system trust store: Apple's macOS path (config_mac/curl_config.h:8)
    ("/* #undef CURL_CA_BUNDLE */", '#define CURL_CA_BUNDLE "/etc/ssl/cert.pem"'),
    # with it, --tcp-fastopen calls connectx(2) (lib/cf-socket.c:1089-1103), a real xnu syscall
    # (xnu-7195 bsd/kern/syscalls.master:697) that this system's libsystem_kernel.dylib exports
    # no wrapper for; without it curl uses plain connect() there (lib/cf-socket.c:1108) and
    # lib/timeval.c:83-105 its non-__builtin_available clock path
    ("#define HAVE_BUILTIN_AVAILABLE 1", "/* #undef HAVE_BUILTIN_AVAILABLE */"),
    # --xattr calls fsetxattr(2) (src/tool_xattr.c:96-97), also a real xnu syscall
    # (syscalls.master:340) with no wrapper in this system's libsystem_kernel.dylib; without
    # these src/tool_xattr.h:28-36 leaves USE_XATTR off and --xattr does nothing
    ("#define HAVE_FSETXATTR 1", "/* #undef HAVE_FSETXATTR */"),
    ("#define HAVE_FSETXATTR_6 1", "/* #undef HAVE_FSETXATTR_6 */"),
]
for old, new in changes:
    if s.count(old) != 1:
        sys.exit("error: config_iphone/curl_config.h has no single '%s' -- recheck build_curl.sh" % old)
    s = s.replace(old, new)
open(p, "w").write(s)
PY

CURL_CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c "${MEDIA_CFLAGS[@]}"
    -DHAVE_CONFIG_H '-DOS="aarch64-apple-darwin20.3.0"'
    -I "$OBJ/config" -I "$C/include" -I "$C/lib" "-I$PWD/$OUT/fontstack/usr_include")
compile() {	# compile <objdir> <src> [flags...]
    local dir="$1" f="$2" b
    shift 2
    b="$(basename "${f%.c}")"
    [ -e "$dir/$b.o" ] && b="$(echo "${f%.c}" | tr / _)"
    "$CLANG" "${CURL_CFLAGS[@]}" "$@" "$f" -o "$dir/$b.o" 2> "$dir/$b.err" ||
        { cat "$dir/$b.err" >&2; echo "error: $f failed to compile" >&2; exit 1; }
    [ -s "$dir/$b.err" ] || rm -f "$dir/$b.err"
}
# make_list <Makefile.inc> <VAR...>: the .c files of those variables
make_list() {
    local mk="$1"
    shift
    python3 -I - "$mk" "$@" <<'PY'
import re, sys
t = open(sys.argv[1]).read().replace("\\\n", " ")
for v in sys.argv[2:]:
    m = re.search(r"^%s\s*=(.*)$" % v, t, re.M)
    if not m:
        sys.exit("error: %s not in %s" % (v, sys.argv[1]))
    print(" ".join(x for x in m.group(1).split() if x.endswith(".c")))
PY
}

# libcurl: every source the Makefile builds; curl_config.h decides what is in
for f in $(make_list "$C/lib/Makefile.inc" LIB_CFILES LIB_VAUTH_CFILES LIB_VTLS_CFILES LIB_VQUIC_CFILES LIB_VSSH_CFILES); do
    compile "$OBJ/lib" "$C/lib/$f" -DBUILDING_LIBCURL -DCURL_HIDDEN_SYMBOLS -fvisibility=hidden
done
LIB="$MEDIA_SYS/libcurl.dylib"
rm -f "$LIB"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib "${MEDIA_LDFLAGS[@]}" \
    -install_name /usr/lib/libcurl.4.dylib -compatibility_version 7 -current_version 9 \
    -o "$LIB" "$OBJ/lib"/*.o \
    "$MEDIA_SYS/libmbedtls.dylib" "$MEDIA_SYS/libmbedx509.dylib" "$MEDIA_SYS/libmbedcrypto.dylib" \
    "$MEDIA_SYS/libz.dylib" "${MEDIA_SYSLIBS[@]}" 2> "$OBJ/lib/link.err" ||
    { grep -E '^  "|error' "$OBJ/lib/link.err" | sed 's/, referenced from:.*//' | sort -u >&2
      echo "error: libcurl link failed" >&2; exit 1; }
otool -hv "$LIB" | grep -q " DYLIB " || { echo "error: $LIB is not MH_DYLIB" >&2; exit 1; }
nm -gU "$LIB" | grep -q " _curl_easy_perform$" || { echo "error: $LIB does not export curl_easy_perform" >&2; exit 1; }
./bind_target_audit.sh "$LIB"
echo "wrote $LIB (/usr/lib/libcurl.4.dylib)"

# curl(1): the tool's sources plus the curlx_* ones it shares with libcurl; zlib for its
# built-in manual, which is stored compressed (src/tool_hugehelp.c, USE_MANUAL with HAVE_LIBZ)
for f in $(make_list "$C/src/Makefile.inc" CURL_CFILES CURLX_CFILES); do
    compile "$OBJ/src" "$C/src/$f" -I "$C/src"
done
M="$PWD/$OUT/curl.macho"
rm -f "$M"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 "${MEDIA_LDFLAGS[@]}" -DCRT_DYNAMIC_LINKING \
    -o "$M" "$TP/Csu/start.s" "$TP/Csu/crt.c" "$OBJ/src"/*.o \
    "$LIB" "$MEDIA_SYS/libz.dylib" "${MEDIA_SYSLIBS[@]}" 2> "$OBJ/src/link.err" ||
    { grep -E '^  "|error' "$OBJ/src/link.err" | sed 's/, referenced from:.*//' | sort -u >&2
      echo "error: curl link failed" >&2; exit 1; }
otool -l "$M" | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || { echo "error: $M has no LC_LOAD_DYLINKER" >&2; exit 1; }
otool -l "$M" | grep -q "cmd LC_MAIN" || { echo "error: $M has no LC_MAIN" >&2; exit 1; }
otool -L "$M" | grep -q "/usr/lib/libcurl.4.dylib" || { echo "error: $M does not link libcurl" >&2; exit 1; }
./bind_target_audit.sh "$M" || { echo "error: $M has an unresolvable two-level bind" >&2; exit 1; }
codesign -s - -f "$M" 2>&1 | grep -v "replacing existing signature" || true
echo "wrote $M (/usr/bin/curl)"
