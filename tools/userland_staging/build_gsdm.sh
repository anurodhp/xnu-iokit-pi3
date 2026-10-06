#!/bin/bash
# Builds gsdm, the GNUstep display manager that is this port's graphical
# login (it replaced xdm), from third_party/gsdm (anurodhp/gsdm, pinned in
# setup_third_party.sh). Two programs, both from gsdm's own GNUmakefiles:
#
#   gsdm            the root daemon (daemon/, plain C), installed into
#                   libc_build/gnustep/root as
#                   /usr/GNUstep/System/Tools/Admin/gsdm -- /usr/local/sbin/gsdm
#                   on the image (stage_gnustep_mac_layout.sh: Tools/Admin);
#   LoginWindow.app the greeter (LoginWindow/, GNUstep AppKit, Eau theme
#                   through GlobalDefaults.plist), installed as
#                   /usr/GNUstep/System/Library/CoreServices/LoginWindow.app --
#                   /System/Library/CoreServices/LoginWindow.app on the image.
#
# and this port's configuration in libc_build/gsdm/root, which
# inject_into_sd_image.sh copies onto the image:
#   etc/gsdm/{gsdm.conf,Xgreeter,Xsession}       (x11_config/gsdm)
#   Library/LaunchDaemons/org.puredarwin.gsdm.plist
#
# What gsdm does is in third_party/gsdm/README.md and daemon/gsdm.c. In
# short it is xdm's own flow, followed function by function: Xorg with
# "-auth <file>" and xdm's SIGUSR1 ready handshake (xdm/server.c), the
# non-PAM crypt(3) password check of xdm's greeter/verify.c, the session
# start of xdm's session.c StartClient (setgid, setlogin, initgroups,
# setuid, xdm's user environment, ~/.Xauthority) and xdm's terminateServer
# after the session.
#
# AUTHENTICATION -- the same as xdm and /bin/login here: getpwnam() from
# libinfo.dylib (reads /etc/master.passwd, tools/libinfo_custom) and
# crypt(pw, pw->pw_passwd) compared with pw->pw_passwd -- Apple's login.c
# non-PAM branch (third_party/system_cmds/login.tproj/login.c:514-516) and
# xdm's greeter/verify.c:351-353. No dylib here exports crypt(), so Apple's
# Libc/gen/crypt.c is compiled into gsdm, exactly as build_xdm.sh and
# build_su.sh do, and handed to gsdm's GNUmakefile as GSDM_EXTRA_OBJS.
# Owners of the rest: initgroups is libopenpam.dylib's (the only dylib
# exporting it; build_xdm.sh), setlogin and sysctlbyname libsystem_c's,
# getpwnam libinfo's, XauWriteAuth/XauLockAuth libXau's.
#
# Restart / Shut Down run /sbin/reboot and /sbin/halt (build_reboot.sh:
# Apple's reboot.c, which hands the request to launchd's reboot2()).
#
# Prerequisites (read by gen_build_order.py): run build_gnustep_back.sh, build_gnustep_eau_theme.sh, build_libXau.sh, build_libinfo.sh, build_openpam_dylib.sh first.
# The session also needs build_xserver.sh (Xorg), build_wmaker.sh (the
# session's window manager and its Terminal autostart) and build_reboot.sh
# (Restart/Shut Down) before the image is made; none is needed to build gsdm.
#
# GSDM_SRC=<dir> builds another checkout of gsdm (development); the default
# is the pinned third_party/gsdm.
# X_SESSION_DISABLED=1 ships /etc/x11-session.disabled (gsdm then exits at
# boot without starting X; `rm /etc/x11-session.disabled` turns it on).
cd "$(dirname "$0")"
. ./gnustep_common.sh

SRC="${GSDM_SRC:-$HERE/../../third_party/gsdm}"
[ -f "$SRC/daemon/gsdm.c" ] && [ -f "$SRC/LoginWindow/GNUmakefile" ] ||
    { echo "error: $SRC is not a gsdm checkout -- run setup_third_party.sh" >&2; exit 1; }
for d in libXau libinfo libopenpam; do
    [ -f "$SYS/$d.dylib" ] || { echo "error: $SYS/$d.dylib not found -- run the build script for $d first" >&2; exit 1; }
done
[ -f "$OUT/x11/include/X11/Xauth.h" ] || { echo "error: X11/Xauth.h not staged -- run build_libXau.sh first" >&2; exit 1; }
[ -d "$ROOT$PREFIX/System/Library/Themes/Eau.theme" ] ||
    { echo "error: Eau.theme not installed -- run build_gnustep_eau_theme.sh first" >&2; exit 1; }
CFG="$HERE/x11_config/gsdm"

# ---- gsdm (the daemon) ---------------------------------------------------
# Compiled with the X11 ports' recipe (x11_common.sh X11_CFLAGS: hosted,
# -fno-builtin -fno-stack-protector -D_FORTIFY_SOURCE=0) and linked like
# build_xdm.sh's xdm: libXau, libinfo, libopenpam, then the libSystem tier
# owners-first (x11_common.sh X11_SYS_DYLIBS) and libSystem.B. Nothing
# Objective-C: gnustep-make's ctool.make links no GNUstep or objc library.
W="$G/gsdm-src"
rsync -a --delete --exclude .git "$SRC/" "$W/"
OBJ="$G/gsdm-obj"
rm -rf "$OBJ"; mkdir -p "$OBJ"
CC_FLAGS=("${TARGET[@]}" -fno-builtin -fno-stack-protector -O2 -fno-common -D_FORTIFY_SOURCE=0
    -Wno-nullability-completeness -Wno-deprecated-declarations)
"$CLANG" "${CC_FLAGS[@]}" -c "$HERE/../../third_party/Libc/gen/crypt.c" -o "$OBJ/crypt.o"
DAEMON_DYLIBS=("$SYS/libXau.dylib" "$SYS/libinfo.dylib" "$SYS/libopenpam.dylib"
    "$SYS/libsystem_kernel.dylib" "$SYS/libsystem_c.dylib" "$SYS/libsystem_pthread.dylib"
    "$SYS/libsystem_malloc.dylib" "$SYS/libsystem_platform.dylib" "$SYS/libdyld.dylib")
gs_setup_link "${DAEMON_DYLIBS[@]}"
gs_build_env
# ctool.make puts -lXau (ADDITIONAL_TOOL_LIBS) first; the rest follow in
# owner order, libSystem.B last (gs_setup_link appends it).
LIBS_REST=""
for d in "${DAEMON_DYLIBS[@]:1}" "$SYS/libSystem.B.dylib"; do
    n="$(basename "$d" .dylib)"; n="${n#lib}"; [ "$n" = System.B ] && n=System
    LIBS_REST="$LIBS_REST -l$n"
done
(cd "$W/daemon" &&
 make GNUSTEP_INSTALLATION_DOMAIN=SYSTEM \
     CPPFLAGS="${CC_FLAGS[*]} -I$OUT/x11/include" \
     LDFLAGS="${TARGET[*]} $LINK" \
     GSDM_EXTRA_OBJS="$OBJ/crypt.o" GSDM_CRYPT_LIBS="$LIBS_REST" > make.out 2>&1) ||
    { grep -E "error:|warning:|Undefined|^ld: |^  \"" "$W/daemon/make.out" | head -30
      echo "error: gsdm build failed, see $W/daemon/make.out" >&2; exit 1; }
if grep -q "warning:" "$W/daemon/make.out"; then
    grep "warning:" "$W/daemon/make.out" | grep -v "ld: warning" >&2 || true
fi
DAEMON="$ROOT$PREFIX/System/Tools/Admin/gsdm"
rm -f "$DAEMON"
(cd "$W/daemon" && make install DESTDIR="$ROOT" GNUSTEP_INSTALLATION_DOMAIN=SYSTEM > install.out 2>&1) ||
    { tail -20 "$W/daemon/install.out"; echo "error: gsdm install failed" >&2; exit 1; }
[ -x "$DAEMON" ] || { echo "error: $DAEMON not installed" >&2; exit 1; }
otool -l "$DAEMON" | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" ||
    { echo "error: $DAEMON is not a dynamic executable" >&2; exit 1; }
otool -l "$DAEMON" | grep -q "cmd LC_MAIN" || { echo "error: $DAEMON has no LC_MAIN" >&2; exit 1; }
nm -m "$DAEMON" | grep -q "external _crypt$" || { echo "error: $DAEMON has no crypt()" >&2; exit 1; }
if otool -L "$DAEMON" | tail -n +2 | grep -qE "libobjc|gnustep"; then
    echo "error: $DAEMON links Objective-C/GNUstep libraries" >&2; exit 1
fi
./bind_target_audit.sh "$DAEMON" "${DAEMON_DYLIBS[@]}" "$SYS/libSystem.B.dylib"
echo "installed $DAEMON"

# ---- LoginWindow.app (the greeter) -------------------------------------
GS_APP_DIR=Library/CoreServices gs_build_app LoginWindow "$SRC/LoginWindow"

# ---- configuration and the launchd job ---------------------------------
STAGE="$OUT/gsdm/root"
rm -rf "$STAGE"
mkdir -p "$STAGE/etc/gsdm" "$STAGE/Library/LaunchDaemons"
cp "$CFG/gsdm.conf" "$STAGE/etc/gsdm/gsdm.conf"
cp "$CFG/Xgreeter" "$CFG/Xsession" "$STAGE/etc/gsdm/"
chmod 644 "$STAGE/etc/gsdm/gsdm.conf"
chmod 755 "$STAGE/etc/gsdm/Xgreeter" "$STAGE/etc/gsdm/Xsession"
for f in Xgreeter Xsession; do
    sh -n "$STAGE/etc/gsdm/$f" || { echo "error: $f syntax" >&2; exit 1; }
done
cp "$CFG/org.puredarwin.gsdm.plist" "$STAGE/Library/LaunchDaemons/"
chmod 644 "$STAGE/Library/LaunchDaemons/org.puredarwin.gsdm.plist"
/usr/bin/plutil -lint "$STAGE/Library/LaunchDaemons/org.puredarwin.gsdm.plist" >/dev/null ||
    { echo "error: org.puredarwin.gsdm.plist is not valid" >&2; exit 1; }
# Ships ENABLED (as xdm did, the user's decision 2026-10-01): at boot gsdm
# starts Xorg and the login window when the kernel boot-args carry
# hdmi_console=<nonzero> (gsdm.conf require_bootarg); otherwise it exits 0
# and only the serial console login comes up.
if [ "${X_SESSION_DISABLED:-0}" = "1" ]; then
    printf '%s\n' "# Remove this file to start gsdm (Xorg + login window) at boot." > "$STAGE/etc/x11-session.disabled"
fi
echo "staged gsdm configuration under $STAGE"
