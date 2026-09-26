#!/bin/bash
# Stages the header-only X.org packages every X11 client library here
# compiles against (DAR-383, X11GS 2) into libc_build/x11/include/, laid
# out the way they install under /usr/X11/include:
#
#   xorgproto-2025.1  -> X11/**, GL/**   (third_party/xorgproto)
#   xtrans-1.6.0      -> X11/Xtrans/*    (third_party/xtrans)
#
# xtrans is "source-only": its Xtrans*.c files are never compiled on
# their own but #included, with TRANS_CLIENT/TRANS_SERVER/X11_t/ICE_t
# etc. defined, by the library that uses them (libX11's xcb_io-free
# paths, libICE). xtrans's own Makefile.am installs every .c/.h into
# $(includedir)/X11/Xtrans for exactly that reason; this copies the same
# set.
#
# Xpoll.h is the one generated xorgproto header: configure.ac lines 54-103
# substitute @USE_FDS_BITS@ with the fd_set member name it finds
# (AC_CHECK_MEMBER(fd_set.fds_bits) first, then __fds_bits). Darwin's
# is `fds_bits` (the iPhoneOS 14.4 SDK's sys/_types/_fd_def.h:
# `__int32_t fds_bits[...]`), so the first check wins -- asserted below
# against the real SDK header rather than assumed.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

x11_require_source "$TP/xorgproto/include/X11/X.h"
x11_require_source "$TP/xtrans/Xtrans.h"

grep -q 'fds_bits\[' "$SDK/usr/include/sys/_types/_fd_def.h" || {
    echo "error: SDK fd_set has no fds_bits member -- Xpoll.h's @USE_FDS_BITS@ choice needs revisiting" >&2
    exit 1
}

# Not rm -rf: every build_libX*.sh stages its own public headers into
# this same tree, and re-running just this script must not silently
# delete them out from under an already-built library's consumers.
mkdir -p "$X11_INC"
# Whole include tree, minus the meson/automake build files that live in it.
(cd "$TP/xorgproto/include" && find . -type f -name '*.h' -print) | while read -r h; do
    mkdir -p "$X11_INC/$(dirname "$h")"
    cp "$TP/xorgproto/include/$h" "$X11_INC/$h"
done
sed 's/@USE_FDS_BITS@/fds_bits/' "$TP/xorgproto/include/X11/Xpoll.h.in" > "$X11_INC/X11/Xpoll.h"

x11_stage_headers X11/Xtrans "$TP/xtrans"/Xtrans.h "$TP/xtrans"/Xtransint.h \
    "$TP/xtrans"/Xtrans.c "$TP/xtrans"/Xtranslcl.c "$TP/xtrans"/Xtranssock.c \
    "$TP/xtrans"/Xtransutil.c "$TP/xtrans"/transport.c

# Sanity: the client-side XID is `unsigned long` (LP64 = 64-bit in memory)
# and the wire types in Xmd.h are 32-bit; Xmd.h picks LONG64 from
# __SIZEOF_LONG__ == 8. Check the target compiler really takes that
# branch, since every wire-struct size in Xproto.h depends on it.
cat > "$X11_ROOT/xmd_check.c" <<'EOF'
#include <X11/Xmd.h>
#include <X11/Xproto.h>
#ifndef LONG64
#error "LONG64 not defined on arm64 -- Xmd.h wire types would be wrong"
#endif
_Static_assert(sizeof(CARD32) == 4, "CARD32 must be 32-bit on the wire");
_Static_assert(sizeof(xResourceReq) == sz_xResourceReq, "xResourceReq wire size");
_Static_assert(sizeof(xCreateWindowReq) == sz_xCreateWindowReq, "xCreateWindowReq wire size");
_Static_assert(sizeof(xConnSetup) == sz_xConnSetup, "xConnSetup wire size");
EOF
"$CLANG" "${X11_CFLAGS[@]}" -fsyntax-only "$X11_ROOT/xmd_check.c"
echo "wrote $X11_INC (xorgproto-2025.1 + xtrans-1.6.0; LONG64 + wire sizes checked for arm64)"
