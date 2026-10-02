#!/bin/bash
# Shared setup for the GNUstep builds (DAR-388 base, DAR-390 gui/back and
# apps). SOURCED by build_gnustep_base.sh, build_gnustep_gui.sh,
# build_gnustep_back.sh and the app scripts -- never run directly, and not
# named build_*.sh so gen_build_order.py does not treat it as a step.
#
# Target layout: gnustep-make's "gnustep" layout under /usr/GNUstep, config
# file /etc/GNUstep/GNUstep.conf. Everything installs with DESTDIR into
# libc_build/gnustep/root, a copy of the target's tree that
# inject_into_sd_image.sh copies onto the image, rearranged into the Mac
# layout (/Applications, /System/Library, /usr/local/bin) by
# stage_gnustep_mac_layout.sh. Builds keep using this layout.
#
# Linking: -nostdlib against this project's own dylibs, owners listed
# first, with no -undefined dynamic_lookup (gnustep-make's darwin default;
# callers pass DYLIB_EXTRA_FLAGS= to make). A symbol missing on the target
# then fails the link here, and every two-level bind names the dylib that
# owns it. Callers check the result with bind_target_audit.sh.
set -euo pipefail
HERE="$(pwd -P)"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
[ -x "$CLANG" ] && [ -d "$SDK" ] || { echo "error: Xcode-12.app not found at $XCODE12" >&2; exit 1; }
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
[ -x "$NEWLD_BINDIR/ld" ] || { echo "error: no usable 'ld' under $DAILY_XCODE" >&2; exit 1; }
OUT="$(cd "$HERE/libc_build" && pwd -P)"
G="$OUT/gnustep"
ROOT="$G/root"
PREFIX=/usr/GNUstep
CONF=/etc/GNUstep/GNUstep.conf
SYS="$OUT/system"
TARGET=(-isysroot "$SDK" -target arm64-apple-ios14.4)
GS_SYS_HEADERS="$ROOT$PREFIX/System/Library/Headers"
GS_SYS_LIBS="$ROOT$PREFIX/System/Library/Libraries"
# The libSystem tier plus the runtime, owners first (bind_target_audit.sh
# records each bind against the first listed dylib that exports the
# symbol); callers put their own libraries in front and libSystem.B stays
# last.
GS_BASE_SYSLIBS=("$SYS/libsystem_kernel.dylib" "$SYS/libsystem_platform.dylib" "$SYS/libsystem_pthread.dylib"
         "$SYS/libsystem_malloc.dylib" "$SYS/libsystem_c.dylib" "$SYS/libsystem_m.dylib"
         "$SYS/libsystem_blocks.dylib" "$SYS/libdispatch.dylib" "$SYS/libdyld.dylib"
         "$SYS/libc++.dylib" "$SYS/libunwind.dylib" "$OUT/libobjc.A.dylib"
         "$SYS/libffi.dylib" "$SYS/libiconv.dylib" "$SYS/libz.dylib"
         `# getpwnam/getaddrinfo (libinfo) and inet_ntop/inet_pton/getifaddrs` \
         `# (libresolv) are not in libSystem.B's reexports here; ping and` \
         `# ifconfig link these two the same way (DAR-182).` \
         "$SYS/libinfo.dylib" "$SYS/libresolv.dylib")

# gs_setup_link <dylib>...  (owners first; libSystem.B is appended)
# Sets LINKDIR, LIBFLAGS, LINK.
# Every dylib gets a -l name in LINKDIR, so -lobjc/-lffi/-ltiff/-lX11 from
# configure resolve to our dylibs (the SDK's .tbd stubs are never linked
# against), and LIBS can hold only -l flags: configure compiles some tests
# with "-x objective-c" in CPPFLAGS, which would make clang read a .dylib
# path in LIBS as source.
gs_setup_link() {
    local libs=("$@" "$SYS/libSystem.B.dylib") d n p f c
    for d in "${libs[@]}"; do [ -e "$d" ] || { echo "error: $d not found" >&2; exit 1; }; done
    LINKDIR="$G/linkdir-$(basename "$0" .sh)"
    rm -rf "$LINKDIR"; mkdir -p "$LINKDIR"
    LIBFLAGS=""
    for d in "${libs[@]}"; do
        n="$(basename "$d" .dylib)"
        case "$n" in libobjc.A) n=libobjc ;; libSystem.B) n=libSystem ;; libgnustep-base.*|libgnustep-gui.*) continue ;; esac
        ln -s "$d" "$LINKDIR/$n.dylib"
        LIBFLAGS="$LIBFLAGS -l${n#lib}"
    done
    # As on Apple, libdl/libpthread/libm are libSystem (configure adds -ldl
    # and -lpthread). Without these, -ldl found the SDK's libdl.tbd -- the
    # SDK's libSystem -- and symbols we own in libinfo got bound to it.
    for n in libdl libpthread libm; do ln -s "$SYS/libSystem.B.dylib" "$LINKDIR/$n.dylib"; done
    # libSystem.B reexports dylibs by install path (/usr/lib/system/...,
    # /usr/lib/libresolv.9.dylib, ...), which ld would look up inside the
    # SDK sysroot. -dylib_file maps each one to our build of it, found by
    # its own LC_ID_DYLIB.
    local map=""
    for p in $(otool -L "$SYS/libSystem.B.dylib" | awk '/reexport/ {print $1}'); do
        f=""
        for c in "$SYS"/*.dylib "$OUT/libobjc.A.dylib"; do
            [ "$(otool -D "$c" 2>/dev/null | tail -1)" = "$p" ] && { f="$c"; break; }
        done
        [ -n "$f" ] || { echo "error: no dylib in $SYS has install name $p (reexported by libSystem.B)" >&2; exit 1; }
        map="$map -Wl,-dylib_file,$p:$f"
    done
    # -Wl,-Z: never search the SDK's /usr/lib for a -l, so a library this
    # project does not build is a link error, not a silent SDK stub.
    # The GNUstep libraries are found by -L into root/, where gnustep-make
    # links them by their own -l names.
    LINK="-nostdlib -B$NEWLD_BINDIR -Wl,-Z -Wl,-fixup_chains -L$LINKDIR -L$GS_SYS_LIBS$map"
}

# gs_build_env: the installed gnustep-make, as seen from the build host.
# Its makefiles and gnustep-config come from root/, but the config file's
# paths are the target's, so everything built is configured for
# /usr/GNUstep. GNUstep.sh (which configure sources) takes
# GNUSTEP_MAKEFILES from the config file, so the build uses a copy that
# differs only in that line.
gs_build_env() {
    [ -f "$ROOT$CONF" ] || { echo "error: $ROOT$CONF missing -- run build_gnustep_base.sh first" >&2; exit 1; }
    BUILDCONF="$G/GNUstep-build.conf"
    sed "s|^GNUSTEP_MAKEFILES=.*|GNUSTEP_MAKEFILES=$ROOT$PREFIX/System/Library/Makefiles|" "$ROOT$CONF" > "$BUILDCONF"
    grep -q "^GNUSTEP_MAKEFILES=$ROOT" "$BUILDCONF" || { echo "error: no GNUSTEP_MAKEFILES line in $ROOT$CONF" >&2; exit 1; }
    export GNUSTEP_MAKEFILES="$ROOT$PREFIX/System/Library/Makefiles"
    export GNUSTEP_CONFIG_FILE="$BUILDCONF"
    # XQuartz's and MacPorts' bin directories are dropped: their *-config
    # scripts (libpng-config, ...) would hand configure host libraries.
    # Homebrew's stays, because gnustep-make runs its gmake; callers turn
    # off the individual config-script probes Homebrew would answer
    # (build_gnustep_gui.sh: libpng-config).
    local p d newpath=""
    IFS=: read -ra p <<< "$PATH"
    for d in "${p[@]}"; do
        case "$d" in /opt/X11/*|/usr/X11/*|/opt/local/*) ;; *) newpath="$newpath:$d" ;; esac
    done
    # host-tools (build_gnustep_host_tools.sh) first: gnustep-make runs
    # plmerge on the build host, and root/'s is an arm64 iOS binary.
    [ -x "$G/host-tools/plmerge" ] || { echo "error: $G/host-tools/plmerge missing -- run build_gnustep_host_tools.sh" >&2; exit 1; }
    export PATH="$G/host-tools:$ROOT$PREFIX/System/Tools${newpath}"
    # Empty: configure's pkg-config probes must not find the host's .pc
    # files and add host include paths.
    mkdir -p "$G/pkgconfig-empty"
    export PKG_CONFIG_LIBDIR="$G/pkgconfig-empty"
}

# gs_relocate_makefiles: a package's .make fragment goes to
# $GNUSTEP_MAKEFILES/Additional, which during the build is root/'s own
# host path, so DESTDIR nests it under root/<host path>. Move it to the
# target's makefiles directory.
gs_relocate_makefiles() {
    local nested="$ROOT$GNUSTEP_MAKEFILES" hosttop
    hosttop="$(echo "$ROOT" | cut -d/ -f2)"     # e.g. "Users"
    [ -n "$hosttop" ] || { echo "error: cannot take the top directory of $ROOT" >&2; exit 1; }
    if [ -d "$nested" ]; then
        cp -R "$nested/." "$GNUSTEP_MAKEFILES/"
        rm -rf "${ROOT:?}/$hosttop"
    fi
    [ ! -e "$ROOT/$hosttop" ] || { echo "error: host path still nested under $ROOT" >&2; exit 1; }
}

# gs_audit_tree <dir>: bind_target_audit.sh every Mach-O under <dir>.
gs_audit_tree() {
    local n=0 f
    while IFS= read -r f; do
        (cd "$HERE" && ./bind_target_audit.sh "$f" >/dev/null) ||
            { (cd "$HERE" && ./bind_target_audit.sh "$f"); echo "error: $f fails bind_target_audit" >&2; exit 1; }
        n=$((n+1))
    done < <(find "$1" -type f \( -perm -u+x -o -name '*.dylib' \) -exec sh -c 'file -b "$1" | grep -q Mach-O && echo "$1"' _ {} \;)
    echo "bind_target_audit: $n images clean under $1"
}

# gs_build_app <AppName> <source dir>: build a gnustep-make application in
# a copy of <source dir> (libc_build/gnustep/<app>-src), install it into
# root/ as /usr/GNUstep/System/Applications/<AppName>.app, and audit it.
# Same link recipe as the libraries; the app links gnustep-gui and
# gnustep-base itself, and the owners are listed so its binds name them.
gs_build_app() {
    local name="$1" src="$2" work app
    [ -f "$src/GNUmakefile" ] || { echo "error: $src not found -- run setup_third_party.sh" >&2; exit 1; }
    [ -d "$ROOT$PREFIX/System/Library/Bundles/libgnustep-back-032.bundle" ] ||
        { echo "error: gnustep-back not installed -- run build_gnustep_back.sh first" >&2; exit 1; }
    gs_setup_link "${GS_BASE_SYSLIBS[@]}"
    gs_build_env
    local cppflags="${TARGET[*]} -D_FORTIFY_SOURCE=0 -I$GS_SYS_HEADERS -I$OUT/objc4_public_headers"
    work="$G/$(echo "$name" | tr 'A-Z' 'a-z')-src"
    rsync -a --delete --exclude .git "$src/" "$work/"
    (cd "$work" &&
     make DYLIB_EXTRA_FLAGS= GNUSTEP_INSTALLATION_DOMAIN=SYSTEM CPPFLAGS="$cppflags" \
         LDFLAGS="${TARGET[*]} $LINK $LIBFLAGS" > make.out 2>&1) ||
        { grep -E "error:|Undefined|^ld: |^  \"" "$work/make.out" | grep -v "ld: warning" | head -30 || true
          echo "error: $name build failed, see $work/make.out" >&2; exit 1; }
    app="$ROOT$PREFIX/System/Applications/$name.app"
    rm -rf "$app"
    (cd "$work" && make install DESTDIR="$ROOT" GNUSTEP_INSTALLATION_DOMAIN=SYSTEM > install.out 2>&1) ||
        { tail -20 "$work/install.out"; echo "error: $name install failed" >&2; exit 1; }
    [ -x "$app/$name" ] || { echo "error: $app/$name not installed" >&2; exit 1; }
    otool -l "$app/$name" | grep -q "cmd LC_MAIN" || { echo "error: $app/$name has no LC_MAIN" >&2; exit 1; }
    gs_audit_tree "$app"
    echo "installed $app"
}

# gs_build_theme <Name> <source dir> [make VAR=value...] (DAR-461): build a
# gnustep-make theme bundle (BUNDLE_EXTENSION = .theme) in a copy of <source
# dir> and install it as /usr/GNUstep/System/Library/Themes/<Name>.theme in
# root/. It builds ONLY the bundle: it links the already-installed
# libgnustep-gui/base and the X11 libraries (themes may call Xlib) and
# rebuilds none of them. gnustep-gui dlopen()s the bundle, so the executable
# is bind_target_audited. Callers check build_gnustep_back.sh was run.
gs_build_theme() {
    local name="$1" src="$2" work T x11_inc cppflags
    shift 2
    [ -f "$src/GNUmakefile" ] || { echo "error: $src not found -- run setup_third_party.sh" >&2; exit 1; }
    [ -d "$ROOT$PREFIX/System/Library/Bundles/libgnustep-back-032.bundle" ] ||
        { echo "error: gnustep-back not installed -- run build_gnustep_back.sh first" >&2; exit 1; }
    gs_setup_link "$SYS/libXext.dylib" "$SYS/libX11.dylib" "$SYS/libxcb.dylib" "$SYS/libXau.dylib" \
        "$SYS/libXdmcp.dylib" "${GS_BASE_SYSLIBS[@]}"
    gs_build_env
    x11_inc="$OUT/x11/include"
    cppflags="${TARGET[*]} -D_FORTIFY_SOURCE=0 -I$GS_SYS_HEADERS -I$OUT/objc4_public_headers -I$x11_inc"
    work="$G/$(echo "$name" | tr 'A-Z' 'a-z')-theme-src"
    rsync -a --delete --exclude .git "$src/" "$work/"
    (cd "$work" &&
     make DYLIB_EXTRA_FLAGS= GNUSTEP_INSTALLATION_DOMAIN=SYSTEM CPPFLAGS="$cppflags" \
         LDFLAGS="${TARGET[*]} $LINK $LIBFLAGS -L$GS_SYS_LIBS -lgnustep-gui -lgnustep-base" "$@" > make.out 2>&1) ||
        { grep -E "error:|Undefined|^ld: |^  \"" "$work/make.out" | grep -v "ld: warning" | head -40 || true
          echo "error: $name theme build failed, see $work/make.out" >&2; exit 1; }
    T="$ROOT$PREFIX/System/Library/Themes/$name.theme"
    rm -rf "$T"
    (cd "$work" && make install "$@" DESTDIR="$ROOT" GNUSTEP_INSTALLATION_DOMAIN=SYSTEM > install.out 2>&1) ||
        { tail -20 "$work/install.out"; echo "error: $name theme install failed" >&2; exit 1; }
    gs_relocate_makefiles
    [ -x "$T/$name" ] || { echo "error: $T/$name not installed" >&2; exit 1; }
    gs_audit_tree "$T"
    echo "installed $T"
}
