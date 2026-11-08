#!/bin/bash
# libetpan 1.9.4 (third_party/libetpan, github.com/dinhvh/libetpan tag 1.9.4,
# BSD-3-Clause, unmodified) as
#   libc_build/system/libetpan.dylib   install name /usr/lib/libetpan.20.dylib
#                                      (libtool-style versions from api_current 25, age 5)
#   libc_build/media/include/libetpan/ the public headers
# for Mail2.app: the low-level IMAP, SMTP, RFC 5322 (imf), MIME and Maildir
# layers plus the stream/data types they share. The high-level driver and
# engine layers (and mbox, MH, POP3, NNTP, feeds) are not built: Mail2 drives
# mailimap/mailsmtp itself.
#
# No TLS library is linked: libetpan's TLS is OpenSSL or GnuTLS, neither of
# which this system has, so mailstream_ssl.c builds its no-TLS stubs and
# Mail2 supplies an Mbed TLS mailstream_low driver of its own (a public
# libetpan interface, mailstream_low_new). No SASL (PLAIN/LOGIN are libetpan's
# own), no Berkeley DB, no zlib COMPRESS. iconv is the system's libiconv.
#
# configure is not run: the checkout has no generated configure (its
# build-mac/autogen-result.tar.gz is from 1.6) and needs automake, which the
# build host lacks. config.h below is what configure finds on Darwin for these
# options (checked against a host run of that 1.6 configure: the same HAVE_*
# set), with the 1.9.4 version from configure.ac; the source lists are each
# Makefile.am's own *_SOURCES, read by make_list.
set -euo pipefail
cd "$(dirname "$0")"
. ./media_common.sh
# for gen_build_order.py, which reads only build_*.sh:
# run build_libsystem.sh build_libinfo.sh build_libresolv_dylib.sh build_libiconv_dylib.sh first
SRC="$PWD/$TP/libetpan"
x11_require_source "$SRC/src/low-level/imap/mailimap.c"
grep -q 'm4_define(\[min_version\], \[9\])' "$SRC/configure.ac" && grep -q 'm4_define(\[mic_version\], \[4\])' "$SRC/configure.ac" ||
    { echo "error: third_party/libetpan is not 1.9.4 -- recheck this script" >&2; exit 1; }
[ -f "$MEDIA_SYS/libiconv.dylib" ] || { echo "error: $MEDIA_SYS/libiconv.dylib not found -- build it first" >&2; exit 1; }

OBJ="$MEDIA_GEN/libetpan"
INC="$MEDIA_INC/libetpan"
rm -rf "$OBJ" "$INC"
mkdir -p "$OBJ/config" "$OBJ/obj" "$INC"

cat > "$OBJ/config/config.h" <<'EOF'
/* libetpan 1.9.4 config.h for this Darwin port (build_libetpan.sh) */
#define HAVE_ARPA_INET_H 1
#define HAVE_CTYPE_H 1
#define HAVE_DLFCN_H 1
#define HAVE_FCNTL_H 1
#define HAVE_GETOPT_LONG 1
#define HAVE_GETPAGESIZE 1
#define HAVE_ICONV 1
#define HAVE_INTTYPES_H 1
#define HAVE_IPV6 1
#define HAVE_LIMITS_H 1
#define HAVE_MEMORY_H 1
#define HAVE_MMAP 1
#define HAVE_NETDB_H 1
#define HAVE_NETINET_IN_H 1
#define HAVE_PTHREAD_H 1
#define HAVE_SETENV 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_MMAN_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_SELECT_H 1
#define HAVE_SYS_SOCKET_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define DBVERS 0
#define LIBETPAN_REENTRANT 1
#define PACKAGE "libetpan"
#define PACKAGE_NAME "libetpan"
#define PACKAGE_VERSION "1.9.4"
#define VERSION "1.9.4"
#define LIBETPAN_VERSION "1.9.4"
#define LIBETPAN_VERSION_MAJOR 1
#define LIBETPAN_VERSION_MINOR 9
#define STDC_HEADERS 1
#define UNSTRICT_SYNTAX 1
EOF

# libetpan-config.h: libetpan-config.h.in run through configure's cpp step
# (its # lines evaluated against config.h, its @ lines kept as directives)
cat > "$INC/libetpan-config.h" <<'EOF'
#ifndef LIBETPAN_CONFIG_H
#define LIBETPAN_CONFIG_H
#include <limits.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#include <sys/param.h>
#include <inttypes.h>
#define MAIL_DIR_SEPARATOR '/'
#define MAIL_DIR_SEPARATOR_S "/"
#define LIBETPAN_EXPORT
#endif
EOF
sed -e 's/@VERSION_MAJOR@/1/' -e 's/@VERSION_MINOR@/9/' -e 's/@REENTRANT@/1/' \
    -e 's/@API_CURRENT@/25/' -e 's/@API_REVISION@/0/' -e 's/@API_COMPATIBILITY@/20/' \
    "$SRC/src/main/libetpan_version.h.in" > "$INC/libetpan_version.h"
grep -q '@' "$INC/libetpan_version.h" && { echo "error: unsubstituted @ in libetpan_version.h" >&2; exit 1; }

# make_list <Makefile.am> <VAR>: the files of that variable
make_list() {
    python3 -I - "$1" "$2" <<'PY'
import re, sys
t = open(sys.argv[1]).read().replace("\\\n", " ")
m = re.search(r"^%s\s*=(.*)$" % sys.argv[2], t, re.M)
if not m:
    sys.exit("error: %s not in %s" % (sys.argv[2], sys.argv[1]))
print(" ".join(m.group(1).split()))
PY
}

# Every module's public headers (libetpan.h includes the driver layers' too)
while IFS= read -r mk; do
    d="$(dirname "$mk")"
    grep -q '^etpaninclude_HEADERS' "$mk" || continue
    for h in $(make_list "$mk" etpaninclude_HEADERS); do
        [ "$h" = libetpan_version.h ] && continue
        cp "$d/$h" "$INC/"
    done
done < <(find "$SRC/src" -name Makefile.am)
cp "$SRC/src/main/libetpan.h" "$MEDIA_INC/libetpan.h"

# creat(2): this system's libsystem_c exports it only as creat$UNIX2003 (the
# x86_64 name), not the plain arm64 one (maildir.c:451 calls it). POSIX defines
# creat(p, m) as open(p, O_WRONLY|O_CREAT|O_TRUNC, m), which is what this is.
cat > "$OBJ/config/creat_compat.h" <<'EOF'
#include <fcntl.h>
#define creat(p, m) open((p), O_WRONLY | O_CREAT | O_TRUNC, (m))
EOF
IFLAGS=(-include "$OBJ/config/creat_compat.h" -I "$OBJ/config" -I "$INC" -I "$MEDIA_INC")
for d in data-types low-level/imap low-level/imf low-level/mime low-level/smtp low-level/maildir main; do
    IFLAGS+=(-I "$SRC/src/$d")
done
CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c "${MEDIA_CFLAGS[@]}" -O2 -DHAVE_CONFIG_H
    -Wno-pointer-sign -Wno-shorten-64-to-32 -Werror=implicit-function-declaration
    "${IFLAGS[@]}")
n=0
for spec in data-types:libdata_types_la_SOURCES low-level/imap:libimap_la_SOURCES low-level/imf:libimf_la_SOURCES \
            low-level/mime:libmime_la_SOURCES low-level/smtp:libsmtp_la_SOURCES \
            low-level/maildir:libmaildir_la_SOURCES main:libmain_la_SOURCES; do
    d="${spec%%:*}"; var="${spec#*:}"
    for f in $(make_list "$SRC/src/$d/Makefile.am" "$var"); do
        case "$f" in *.c) ;; *) continue ;; esac
        b="$(echo "$d/${f%.c}" | tr / _)"
        "$CLANG" "${CFLAGS[@]}" "$SRC/src/$d/$f" -o "$OBJ/obj/$b.o" 2> "$OBJ/obj/$b.err" ||
            { cat "$OBJ/obj/$b.err" >&2; echo "error: $d/$f failed to compile" >&2; exit 1; }
        [ -s "$OBJ/obj/$b.err" ] || rm -f "$OBJ/obj/$b.err"
        n=$((n+1))
    done
done
echo "compiled $n libetpan sources"

LIB="$MEDIA_SYS/libetpan.dylib"
rm -f "$LIB"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib "${MEDIA_LDFLAGS[@]}" \
    -install_name /usr/lib/libetpan.20.dylib -compatibility_version 21 -current_version 25 \
    -o "$LIB" "$OBJ/obj"/*.o "$MEDIA_SYS/libiconv.dylib" "${MEDIA_SYSLIBS[@]}" 2> "$OBJ/link.err" ||
    { grep -E '^  "|error' "$OBJ/link.err" | sed 's/, referenced from:.*//' | sort -u >&2
      echo "error: libetpan link failed" >&2; exit 1; }
otool -hv "$LIB" | grep -q " DYLIB " || { echo "error: $LIB is not MH_DYLIB" >&2; exit 1; }
syms="$(nm -gU "$LIB")"	# once: grep -q closing the pipe early fails nm under pipefail
for s in mailimap_new mailimap_idle mailesmtp_send mailmime_parse mailstream_low_new maildir_message_add_uid; do
    grep -q " _$s$" <<< "$syms" || { echo "error: $LIB does not export $s" >&2; exit 1; }
done
./bind_target_audit.sh "$LIB"
echo "wrote $LIB (/usr/lib/libetpan.20.dylib)"
