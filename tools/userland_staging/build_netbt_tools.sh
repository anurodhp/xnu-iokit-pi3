#!/bin/bash
# Builds NetBSD's Bluetooth commands from our fork anurodhp/netbsd-src
# (iokit-port; third_party/netbsd-src) against libbluetooth.dylib
# (build_libbluetooth.sh, run first): docs/bluetooth-stack-plan.md S3.
#
#   btconfig     (usr.sbin) configure HCI units: up/down, inquiry, auth, ...
#   btpin        (usr.bin)  hand bthcid a PIN for pairing
#   sdpquery     (usr.bin)  query SDP servers (local sdpd or remote devices)
#   rfcomm_sppd  (usr.bin)  RFCOMM serial port profile client/server
#   sdpd         (usr.sbin) the local SDP server
#   bthcid       (usr.sbin) link-key and PIN agent for pairing (needs
#                build_libevent.sh and build_libprop.sh)
#   btkey        (usr.bin)  list/add/remove stored link keys (libprop)
#   btdevctl     (usr.sbin) query a device's HID service over SDP and cache it
#                for bthidd (libprop, build_libusbhid.sh)
#
# Sources and flags are each tool's own Makefile (SRCS, CPPFLAGS). Each is a
# dynamic executable, gated by bind_target_audit.sh. Output:
# libc_build/netbt_tools/<tool>.macho.
#
# Usage: build_netbt_tools.sh [tool ...]   (default: all of the above)
set -euo pipefail
cd "$(dirname "$0")"

OUT="libc_build"
T="../../third_party"
NB="$T/netbsd-src"
COMPAT="../../compat/netbt-user"
XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
NEWLD_BINDIR="$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/bin"
S="$OUT/system"
INC="$OUT/netbt_include"

[ -f "$S/libbluetooth.dylib" ] && [ -d "$INC" ] || { echo "error: run build_libbluetooth.sh first" >&2; exit 1; }

DEST="$OUT/netbt_tools"
mkdir -p "$DEST"

# tool | source dir | sources | extra CPPFLAGS | extra dylibs
spec() {
	case "$1" in
	btconfig)    echo "usr.sbin/btconfig|btconfig.c|" ;;
	btpin)       echo "usr.bin/btpin|btpin.c|" ;;
	sdpquery)    echo "usr.bin/sdpquery|sdpquery.c command.c print.c||libiconv" ;;
	rfcomm_sppd) echo "usr.bin/rfcomm_sppd|rfcomm_sppd.c|" ;;
	sdpd)        echo "usr.sbin/sdpd|db.c log.c main.c record.c server.c service.c compat.c|-DSDP_COMPAT -DINET6" ;;
	bthcid)      echo "usr.sbin/bthcid|bthcid.c hci.c client.c config.c|-I$OUT/libevent_include -I$OUT/libprop_include|libevent libprop" ;;
	btkey)       echo "usr.bin/btkey|btkey.c device.c file.c|-I$OUT/libprop_include|libprop" ;;
	btdevctl)    echo "usr.sbin/btdevctl|btdevctl.c db.c print.c sdp.c|-I$OUT/libprop_include|libprop libusbhid" ;;
	*) return 1 ;;
	esac
}

TOOLS=("$@")
[ ${#TOOLS[@]} -eq 0 ] && TOOLS=(btconfig btpin sdpquery rfcomm_sppd sdpd bthcid btkey btdevctl)

# libc gaps, linked into each tool (nm -gU over libc_build/system finds none
# of these exported): err(3)/warn(3) (compat/netbt-user/err_shim.c),
# snprintb(3) (snprintb.c), daemon/setprogname/revoke (libc_shim.c, after
# Apple Libc), and Apple libutil's humanize_number.c, unmodified.
SHIMS="$OUT/netbt_shims"
rm -rf "$SHIMS"; mkdir -p "$SHIMS"
SCF=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -O1 -D_FORTIFY_SOURCE=0)
"$CLANG" "${SCF[@]}" "$COMPAT/err_shim.c" -o "$SHIMS/err_shim.o"
"$CLANG" "${SCF[@]}" "$COMPAT/snprintb.c" -o "$SHIMS/snprintb.o"
"$CLANG" "${SCF[@]}" "$COMPAT/libc_shim.c" -o "$SHIMS/libc_shim.o"
"$CLANG" "${SCF[@]}" -I "$T/libutil" "$T/libutil/humanize_number.c" -o "$SHIMS/humanize_number.o"

for tool in "${TOOLS[@]}"; do
	IFS='|' read -r dir srcs extra libs <<< "$(spec "$tool")" || { echo "error: unknown tool $tool" >&2; exit 1; }
	extralibs=()
	for l in $libs; do extralibs+=("$S/$l.dylib"); done
	obj="$OUT/netbt_obj_$tool"
	rm -rf "$obj"
	mkdir -p "$obj"
	CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -O1 -Wall -Wno-nullability-completeness -D_FORTIFY_SOURCE=0
	    -Wno-unused-function -Wno-sign-compare -Wno-pointer-sign
	    -Werror=implicit-function-declaration -Werror=int-conversion -Werror=incompatible-pointer-types
	    -include "$COMPAT/netbt_user.h" -I "$INC" -I "$NB/$dir" $extra)
	for s in $srcs; do
		"$CLANG" "${CFLAGS[@]}" "$NB/$dir/$s" -o "$obj/${s%.c}.o"
	done
	M="$DEST/$tool.macho"
	rm -f "$M"
	"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
	    -o "$M" -DCRT_NO_PTHREAD_BOOTSTRAP "$T/Csu/start.s" "$T/Csu/crt.c" "$obj"/*.o "$SHIMS"/*.o \
	    ${extralibs[@]+"${extralibs[@]}"} "$S/libbluetooth.dylib" "$S/libinfo.dylib" "$S/libresolv.dylib" "$S/libsystem_c.dylib" "$S/libsystem_malloc.dylib" \
	    "$S/libsystem_kernel.dylib" "$S/libsystem_platform.dylib" "$S/libsystem_pthread.dylib" \
	    "$S/libdyld.dylib" "$S/libSystem.B.dylib"
	otool -l "$M" | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || { echo "error: $M: no LC_LOAD_DYLINKER" >&2; exit 1; }
	otool -l "$M" | grep -q "cmd LC_MAIN" || { echo "error: $M: no LC_MAIN" >&2; exit 1; }
	./bind_target_audit.sh "$M" >/dev/null || { ./bind_target_audit.sh "$M"; echo "error: $M has an unresolvable two-level bind" >&2; exit 1; }
	codesign -s - -f "$M" 2>&1 | grep -v "replacing existing signature" || true
	echo "wrote $M"
done
