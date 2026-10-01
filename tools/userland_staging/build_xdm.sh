#!/bin/bash
# Builds xdm 1.1.17 (third_party/xdm, tag xdm-1.1.17, unmodified freedesktop
# xorg/app/xdm): the graphical login for the boot-time X session. Stages
# into libc_build/x11/server/root, the tree inject_into_sd_image.sh ditto's
# onto the image, so that script needs no entry of its own:
#   usr/X11/bin/xdm, usr/X11/bin/xdm-launch
#   usr/X11/lib/X11/xdm/{libXdmGreet.so,chooser,xdm-config,Xservers,
#                        Xaccess,Xsession,Xsetup,Xstartup,Xreset,pixmaps/}
#   usr/X11/share/X11/app-defaults/{Xlogin,Chooser}
#   Library/LaunchDaemons/org.puredarwin.x11-session.plist
#   var/lib/xdm
# Prerequisites (read by gen_build_order.py): run build_libXaw.sh, build_libXpm.sh, build_libXft.sh, build_libXmu.sh, build_libXt.sh, build_libXext.sh, build_libX11.sh, build_libXrender.sh, build_libXau.sh, build_libXdmcp.sh, build_libinfo.sh, build_libresolv_dylib.sh, build_openpam_dylib.sh first.
# The session it starts also needs build_xserver.sh (Xorg) and
# build_wmaker.sh (wmaker, and the autostart that opens Terminal.app) run
# before the image is made; neither is needed to build xdm itself.
# Toolchain, layout and helpers: x11_common.sh / fontstack_common.sh. As with
# the other X11 ports, configure is not run:
#   config: x11_config/xdm/config.h (each define cites configure.ac).
#   sources: xdm/Makefile.am xdm_SOURCES (xdmauth.c for HAS_XDM_AUTH; not
#     prngc.c since HAVE_ARC4RANDOM, not rpcauth.c/krb5auth.c);
#     greeter/Makefile.am libXdmGreet_la_SOURCES; chooser/chooser.c.
#
# AUTHENTICATION -- non-PAM crypt(3)/master.passwd, the same as /bin/login:
#   xdm is built without USE_PAM and without HAVE_GETSPNAM, so
#   greeter/verify.c takes `user_pass = p->pw_passwd` from getpwnam()
#   (verify.c:231,292-337) and compares crypt(password, user_pass)
#   (verify.c:351-353). Apple's login.c does exactly this in its non-PAM
#   branch: `salt = pwd->pw_passwd; rval = strcmp(crypt(p, salt), salt)`
#   (third_party/system_cmds/login.tproj/login.c:514-516), and build_login.sh
#   documents why login is built that way (OpenPAM has no service modules
#   here, and dlopen-based module loading is not wired up). getpwnam() is
#   libinfo.dylib's, reading /etc/master.passwd (tools/libinfo_custom);
#   crypt() is Apple's Libc/gen/crypt.c, compiled into xdm itself as
#   build_su.sh does (no dylib exports crypt) and handed to the greeter
#   through xdm's dlfuncs table (session.c:203, greet.h __xdm_crypt).
#   Session setup (no HAVE_SETUSERCONTEXT): session.c StartClient runs
#   setgid, setlogin, initgroups, setuid (session.c:598-624), the same
#   order as login.c:872-884. setuid/setgid are libsystem_kernel exports,
#   setlogin is libsystem_c's, initgroups is libopenpam.dylib's (the only
#   dylib exporting it; libinfo's own copy is -unexported_symbol, see
#   build_libinfo.sh). killpg (session.c:262) has no dylib owner, so Apple's
#   own Libc/compat-43/FreeBSD/killpg.c is compiled into xdm. creat, wait
#   and sigsuspend are likewise not exported by any libc dylib (creat only
#   by libsystem_notify, sigsuspend only by libdispatch), so
#   x11_config/xdm/xdm_libc_gaps.c defines them locally, each citing its
#   real Libc/xnu source; moving them to their owners (libsystem_c,
#   libsystem_kernel) needs a libsystem_c rebuild and is left as follow-up.
#   xdm's greeter is a dlopen()ed MH_BUNDLE (session.c:345, libXdmGreet.so).
#
# The greeter is linked WITHOUT libXaw, libXmu, libXdmcp and libXau, which
# greeter/Makefile.am's XDMGREET_LIBS (configure.ac: xaw7, xdmcp, xau)
# lists but which none of its undefined symbols come from (checked with
# nm -u on the three greeter objects); libXaw is linked by the chooser, the
# one xdm program that uses Athena widgets (chooser.c XawList*).
set -e
cd "$(dirname "$0")"
. ./fontstack_common.sh

SRC="$TP/xdm"
x11_require_source "$SRC/xdm/Makefile.am"
grep -q 'AC_INIT(\[xdm\], \[1.1.17\]' "$SRC/configure.ac" || {
    echo "error: $SRC is not xdm 1.1.17 -- recheck this script" >&2; exit 1; }
for pair in libXaw:build_libXaw.sh libXpm:build_libXpm.sh libXmu:build_libXmu.sh libXt:build_libXt.sh \
            libXext:build_libXext.sh libX11:build_libX11.sh libXft:build_libXft.sh \
            libXrender:build_libXrender.sh libXau:build_libXau.sh libXdmcp:build_libXdmcp.sh \
            libinfo:build_libinfo.sh libresolv:build_libresolv_dylib.sh libopenpam:build_openpam_dylib.sh; do
    d="${pair%%:*}"
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- run ${pair##*:} first" >&2; exit 1; }
done
x11_require_staged X11/Xaw/AllWidgets.h build_libXaw.sh
x11_require_staged X11/xpm.h build_libXpm.sh
x11_require_staged X11/Xft/Xft.h build_libXft.sh
x11_require_staged X11/Xmu/SysUtil.h build_libXmu.sh
x11_require_staged X11/Xdmcp.h build_libXdmcp.sh
x11_require_staged X11/Xauth.h build_libXau.sh
for d in libsystem_kernel libsystem_c libsystem_pthread libsystem_malloc libsystem_platform libdyld; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2; exit 1; }
done
x11_require_sys_dylibs

ROOT="$X11_ROOT/server/root"
STAGE="$ROOT/usr/X11"
XDMDIR="$STAGE/lib/X11/xdm"
CFG="$X11_CONFIG_DIR/xdm"
SESS="$X11_CONFIG_DIR/session"
S="$OUT/system"
BASE=(-I "$CFG" -I "$SRC/include")

# ---- xdm ---------------------------------------------------------------
# xdm/Makefile.am AM_CFLAGS: -DHASXDMAUTH (HAS_XDM_AUTH). dm.c:213 calls
# system(3), which the iPhoneOS SDK marks __IOS_PROHIBITED, so
# ios_prohibited_override.h (see it) as build_wmaker.sh does for main.c.
OBJ="$X11_ROOT/obj/xdm"
rm -rf "$OBJ"; mkdir -p "$OBJ"
X11_OK=0 X11_FAIL=0
for f in $(x11_am_sources "$SRC/xdm/Makefile.am" xdm_SOURCES); do
    case "$f" in prngc.c|rpcauth.c|krb5auth.c) continue ;; esac
    x11_compile "$OBJ" "$SRC/xdm/$f" "${BASE[@]}" -DHASXDMAUTH -include ios_prohibited_override.h
done
x11_compile "$OBJ" "$TP/Libc/gen/crypt.c"
x11_compile "$OBJ" "$TP/Libc/compat-43/FreeBSD/killpg.c"
x11_compile "$OBJ" "$CFG/xdm_libc_gaps.c"
x11_compile_report xdm
mkdir -p "$STAGE/bin"
fs_link_exe "$STAGE/bin/xdm" "$OBJ" \
    "$S/libXmu.dylib" "$S/libXdmcp.dylib" "$S/libXau.dylib" "$S/libX11.dylib" \
    "$S/libinfo.dylib" "$S/libresolv.dylib" "$S/libopenpam.dylib"

# ---- greeter (dlopen()ed bundle) ---------------------------------------
# greeter/Makefile.am: -DGREET_LIB, XDMGREET_CFLAGS incl. -D_CONST_X_STRING.
OBJ="$X11_ROOT/obj/xdm_greeter"
rm -rf "$OBJ"; mkdir -p "$OBJ"
X11_OK=0 X11_FAIL=0
for f in $(x11_am_sources "$SRC/greeter/Makefile.am" libXdmGreet_la_SOURCES); do
    x11_compile "$OBJ" "$SRC/greeter/$f" "${BASE[@]}" -I "$X11_INC/freetype2" -DGREET_LIB -D_CONST_X_STRING
done
x11_compile_report xdm_greeter
mkdir -p "$XDMDIR"
M="$XDMDIR/libXdmGreet.so"
rm -f "$M"
if ! "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -bundle -nostdlib -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains -o "$M" "$OBJ"/*.o \
    "$S/libXft.dylib" "$S/libXrender.dylib" "$S/libXpm.dylib" "$S/libXt.dylib" "$S/libXext.dylib" \
    "$S/libX11.dylib" "$S/libinfo.dylib" "${X11_SYS_DYLIBS[@]}" 2>"$OBJ/link.err"; then
    echo "error: greeter link failed; undefined symbols:" >&2
    grep -E '^  "|error' "$OBJ/link.err" | sed 's/, referenced from:.*//' | sort -u >&2
    exit 1
fi
otool -hv "$M" | grep -q " BUNDLE " || { echo "error: $M is not MH_BUNDLE" >&2; exit 1; }
nm -gU "$M" | awk '{print $3}' | grep -qx _GreetUser || { echo "error: $M does not export _GreetUser" >&2; exit 1; }
FLAT=$(nm -m "$M" | grep 'dynamically looked up' || true)
[ -z "$FLAT" ] || { echo "error: $M has flat-namespace references:" >&2; echo "$FLAT" >&2; exit 1; }
# It reaches xdm only through the dlfuncs pointers (greet.h), so it must
# bind nothing to the main executable.
MAINBINDS=$(xcrun dyld_info -fixups "$M" | awk '/<main-executable>\// { sub(/.*<main-executable>\//, ""); print $1 }' | sort -u)
[ -z "$MAINBINDS" ] || { echo "error: $M binds symbols to the main executable: $MAINBINDS" >&2; exit 1; }
echo "wrote $M"
./bind_target_audit.sh "$M"

# ---- chooser (XDMCP host menu; not used with requestPort 0, but it is
# what links libXaw) -----------------------------------------------------
OBJ="$X11_ROOT/obj/xdm_chooser"
rm -rf "$OBJ"; mkdir -p "$OBJ"
X11_OK=0 X11_FAIL=0
x11_compile "$OBJ" "$SRC/chooser/chooser.c" "${BASE[@]}" -D_CONST_X_STRING
x11_compile_report xdm_chooser
fs_link_exe "$XDMDIR/chooser" "$OBJ" \
    "$S/libXaw.dylib" "$S/libXt.dylib" "$S/libXdmcp.dylib" "$S/libX11.dylib" \
    "$S/libinfo.dylib" "$S/libresolv.dylib"

# ---- boot launcher -----------------------------------------------------
OBJ="$X11_ROOT/obj/xdm_launch"
rm -rf "$OBJ"; mkdir -p "$OBJ"
X11_OK=0 X11_FAIL=0
x11_compile "$OBJ" "$SESS/xdm-launch.c"
x11_compile_report xdm-launch
fs_link_exe "$STAGE/bin/xdm-launch" "$OBJ"

# ---- data --------------------------------------------------------------
# What `make install` installs (config/Makefile.am, app-defaults/
# Makefile.am), with this port's own files from x11_config/xdm: see
# xdm-config and Xlogin for where they differ from the .in templates.
for f in xdm-config Xservers Xaccess Xsession Xsetup Xstartup Xreset; do
    cp "$CFG/$f" "$XDMDIR/$f"
done
chmod 644 "$XDMDIR/xdm-config" "$XDMDIR/Xservers" "$XDMDIR/Xaccess"
chmod 755 "$XDMDIR/Xsession" "$XDMDIR/Xsetup" "$XDMDIR/Xstartup" "$XDMDIR/Xreset"
mkdir -p "$XDMDIR/pixmaps" "$STAGE/share/X11/app-defaults"
cp "$CFG/xorg.xpm" "$CFG/xorg-bw.xpm" "$XDMDIR/pixmaps/"
cp "$CFG/Xlogin" "$STAGE/share/X11/app-defaults/Xlogin"
cp "$SRC/app-defaults/Chooser" "$STAGE/share/X11/app-defaults/Chooser"
mkdir -p "$ROOT/var/lib/xdm"
chmod 755 "$ROOT/var/lib/xdm"

# The old script-based boot session (x11-session, run as root) is gone;
# drop it from an already-staged tree.
rm -f "$STAGE/bin/x11-session"
mkdir -p "$ROOT/Library/LaunchDaemons"
cp "$SESS/org.puredarwin.x11-session.plist" "$ROOT/Library/LaunchDaemons/"
chmod 644 "$ROOT/Library/LaunchDaemons/org.puredarwin.x11-session.plist"
# Ships ENABLED (the user's decision, 2026-10-01): at boot xdm-launch starts
# Xorg and the xdm login screen when the kernel boot-args carry
# hdmi_console=<nonzero> (QEMU without a framebuffer, and any boot without
# it, stays console-only: xdm-launch exits 0). The serial console login is
# a separate job and always comes up. The off switch is a file: set
# X_SESSION_DISABLED=1 when running this script to ship it, or create
# /etc/x11-session.disabled on the Pi (`touch`), no rebuild needed;
# `rm /etc/x11-session.disabled` enables it again.
mkdir -p "$ROOT/etc"
if [ "${X_SESSION_DISABLED:-0}" = "1" ]; then
    printf '%s\n' "# Remove this file to start xdm (Xorg + login screen) at boot." > "$ROOT/etc/x11-session.disabled"
else
    rm -f "$ROOT/etc/x11-session.disabled"
fi
plutil -lint "$ROOT/Library/LaunchDaemons/org.puredarwin.x11-session.plist" >/dev/null ||
    { echo "error: x11-session plist is not valid" >&2; exit 1; }
for f in Xsession Xsetup Xstartup Xreset; do
    sh -n "$XDMDIR/$f" || { echo "error: $f syntax" >&2; exit 1; }
done
echo "staged xdm under $STAGE and $ROOT/Library/LaunchDaemons"
