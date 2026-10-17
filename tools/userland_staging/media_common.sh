#!/bin/bash
# Shared setup for the media stack behind Venice.app (libmpv and what it
# needs: FFmpeg, mbedTLS, fribidi, HarfBuzz, libass, libplacebo). SOURCED by
# build_mbedtls.sh, build_ffmpeg.sh, build_fribidi.sh, build_harfbuzz.sh,
# build_libass.sh, build_libplacebo.sh and build_libmpv.sh -- never run
# directly, and not named build_*.sh so gen_build_order.py does not treat it
# as a step.
#
# Same toolchain and link recipe as build_mesa.sh / build_libepoxy.sh (Xcode
# 12's iPhoneOS SDK, -nostdlib against this project's own dylibs, owners
# listed before libSystem.B, bind_target_audit.sh as the gate), but with a
# cross file of its own: build_mesa.sh's cross.ini embeds the checkout path
# it was generated in, and these projects need more owners on the link line
# (libinfo/libresolv for sockets and getaddrinfo, libc++ for HarfBuzz).
#
# Layout:
#   libc_build/system/<lib>.dylib      the dylibs, install names /usr/lib/<lib>.<major>.dylib
#                                      (inject_into_sd_image.sh installs every system dylib at its install name)
#   libc_build/media/include           staged public headers (mpv/client.h, libavcodec/..., ass/...)
#   libc_build/media/pkgconfig         .pc files for the staged libraries, generated here, never installed
#   libc_build/media/gen/<name>        per-project build directories and logs
#
# Every library is the upstream release at the tag setup_third_party.sh pins,
# unmodified.

. ./x11_common.sh

MEDIA="$PWD/$OUT/media"
MEDIA_INC="$MEDIA/include"
MEDIA_PC="$MEDIA/pkgconfig"
MEDIA_GEN="$MEDIA/gen"
MEDIA_SYS="$PWD/$OUT/system"
MEDIA_CROSS="$MEDIA/cross.ini"
mkdir -p "$MEDIA_INC" "$MEDIA_PC" "$MEDIA_GEN"
TC="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin"
LIBCXX="$PWD/$TP/llvm-project/libcxx"
LIBCXXABI="$PWD/$TP/llvm-project/libcxxabi"

for t in meson ninja pkg-config; do
    command -v "$t" >/dev/null || { echo "error: $t not found on the host (brew install meson ninja pkg-config)" >&2; exit 1; }
done
x11_require_sys_dylibs
for d in libinfo libresolv libsystem_m libc++ libz; do
    [ -f "$MEDIA_SYS/$d.dylib" ] || { echo "error: $MEDIA_SYS/$d.dylib not found -- build it first" >&2; exit 1; }
done

# Owners first, libSystem.B last (bind_target_audit.sh). -dead_strip_dylibs
# drops the ones a given image does not use, so one list serves every link.
MEDIA_SYSLIBS=("$MEDIA_SYS/libinfo.dylib" "$MEDIA_SYS/libresolv.dylib"
    "$MEDIA_SYS/libsystem_kernel.dylib" "$MEDIA_SYS/libsystem_c.dylib"
    "$MEDIA_SYS/libsystem_pthread.dylib" "$MEDIA_SYS/libsystem_malloc.dylib"
    "$MEDIA_SYS/libsystem_platform.dylib" "$MEDIA_SYS/libdyld.dylib"
    "$MEDIA_SYS/libsystem_m.dylib" "$MEDIA_SYS/libc++.dylib" "$MEDIA_SYS/libSystem.B.dylib")
MEDIA_CFLAGS=("$X11_OPT" -fno-builtin -fno-stack-protector -fno-common -D_FORTIFY_SOURCE=0
    -Wno-nullability-completeness -Wno-deprecated-declarations
    "-I$MEDIA_INC" "-I$PWD/$X11_INC" "-I$PWD/$X11_INC/freetype2" "-I$PWD/$OUT/fontstack/usr_include")
MEDIA_CXXFLAGS=(-nostdinc++ -isystem "$LIBCXX/include" "-I$LIBCXXABI/include")
# As on Apple, libm/libpthread/libdl are libSystem; configure and meson add
# -lm and -lpthread, which would otherwise find the SDK's libm.tbd (the SDK's
# libSystem) and bind libinfo's getaddrinfo & co. to it (gnustep_common.sh's
# gs_setup_link has the same links). -Wl,-Z: never search the SDK for a -l
# or a -framework, so a library this project does not build fails the link.
MEDIA_LINKDIR="$MEDIA/linkdir"
rm -rf "$MEDIA_LINKDIR"; mkdir -p "$MEDIA_LINKDIR"
for n in libm libpthread libdl; do ln -s "$MEDIA_SYS/libSystem.B.dylib" "$MEDIA_LINKDIR/$n.dylib"; done
MEDIA_LDFLAGS=(-nostdlib "-B$NEWLD_BINDIR" -Wl,-fixup_chains -Wl,-not_for_dyld_shared_cache -Wl,-dead_strip_dylibs
    -Wl,-Z "-L$MEDIA_LINKDIR")

# media_pc <name> <version> <cflags> <libs> [requires]: a .pc file for a
# staged library (Libs are full dylib paths, so a link records the dylib's
# own install name).
media_pc() {
    printf 'Name: %s\nDescription: %s (staged)\nVersion: %s\nRequires: %s\nCflags: %s\nLibs: %s\n' \
        "$1" "$1" "$2" "${5:-}" "$3" "$4" > "$MEDIA_PC/$1.pc"
}

# The libraries the media stack takes from the existing X11/font stack.
media_pc zlib 1.2.11 "-I$PWD/$OUT/fontstack/usr_include" "$MEDIA_SYS/libz.dylib"
media_pc expat 2.5.0 "-I$PWD/$OUT/fontstack/usr_include" "$MEDIA_SYS/libexpat.dylib"
media_pc freetype2 26.4.20 "-I$PWD/$X11_INC/freetype2" "$MEDIA_SYS/libfreetype.dylib"
media_pc fontconfig 2.17.1 "-I$PWD/$X11_INC" "$MEDIA_SYS/libfontconfig.dylib" "freetype2"
media_pc x11 1.8.12 "-I$PWD/$X11_INC" "$MEDIA_SYS/libX11.dylib"
media_pc xext 1.3.6 "-I$PWD/$X11_INC" "$MEDIA_SYS/libXext.dylib" "x11"
media_pc gl 25.2.8 "-I$PWD/$X11_INC" "$MEDIA_SYS/libGL.dylib"

{
    echo "[binaries]"
    echo "c = ['$CLANG', '-isysroot', '$SDK', '-target', 'arm64-apple-ios14.4']"
    echo "cpp = ['$TC/clang++', '-isysroot', '$SDK', '-target', 'arm64-apple-ios14.4']"
    echo "objc = ['$CLANG', '-isysroot', '$SDK', '-target', 'arm64-apple-ios14.4']"
    echo "ar = '$TC/ar'"
    echo "strip = '$TC/strip'"
    echo "pkg-config = '$(command -v pkg-config)'"
    echo "[built-in options]"
    printf "c_args = ["; printf "'%s', " "${MEDIA_CFLAGS[@]}"; echo "]"
    printf "cpp_args = ["; printf "'%s', " "${MEDIA_CFLAGS[@]}" "${MEDIA_CXXFLAGS[@]}" -fexceptions -frtti; echo "]"
    for l in c_link_args cpp_link_args; do
        printf "%s = [" "$l"; printf "'%s', " "${MEDIA_LDFLAGS[@]}" "${MEDIA_SYSLIBS[@]}"; echo "]"
    done
    echo "[properties]"
    echo "pkg_config_libdir = '$MEDIA_PC'"
    echo "needs_exe_wrapper = true"
    echo "[host_machine]"
    echo "system = 'darwin'"
    echo "cpu_family = 'aarch64'"
    echo "cpu = 'arm64'"
    echo "endian = 'little'"
} > "$MEDIA_CROSS"

# media_meson <name> <src> [meson -D options...]: configure and build one
# meson project in $MEDIA_GEN/<name>, failing on any implicit declaration
# (it truncates a pointer return on arm64, DAR-377).
media_meson() {
    local name="$1" src="$2"
    shift 2
    local build="$MEDIA_GEN/$name"
    rm -rf "$build"
    meson setup "$build" "$src" --cross-file "$MEDIA_CROSS" --prefix=/usr --buildtype=plain \
        --wrap-mode=nodownload "$@" > "$MEDIA_GEN/$name-setup.log" 2>&1 ||
        { tail -40 "$MEDIA_GEN/$name-setup.log" >&2; echo "error: $name meson setup failed" >&2; exit 1; }
    ninja -C "$build" > "$MEDIA_GEN/$name-ninja.log" 2>&1 ||
        { grep -E 'error:|FAILED|Undefined|^  "' "$MEDIA_GEN/$name-ninja.log" | head -40 >&2
          echo "error: $name build failed, see $MEDIA_GEN/$name-ninja.log" >&2; exit 1; }
    if grep -qE 'implicit declaration|implicitly declaring' "$MEDIA_GEN/$name-ninja.log"; then
        grep -E 'implicit declaration|implicitly declaring' "$MEDIA_GEN/$name-ninja.log" | sort -u >&2
        echo "error: implicit function declarations in the $name build (above)" >&2; exit 1
    fi
}

# media_install_dylib <built dylib> <system name> <install name>: copy a
# built dylib into libc_build/system under its install name (meson links
# @rpath ids), rewrite references to other staged media dylibs the same way,
# and gate on MH_DYLIB and bind_target_audit.sh.
media_install_dylib() {
    local built="$1" out="$MEDIA_SYS/$2" iname="$3" dep base
    rm -f "$out"
    cp "$built" "$out"
    chmod u+w "$out"
    install_name_tool -id "$iname" "$out" 2>/dev/null
    for dep in $(otool -L "$out" | awk 'NR > 2 && $1 ~ /^@rpath\// {print $1}'); do
        base="$(basename "$dep")"
        install_name_tool -change "$dep" "/usr/lib/$base" "$out" 2>/dev/null
    done
    otool -hv "$out" | grep -q " DYLIB " || { echo "error: $out is not MH_DYLIB" >&2; exit 1; }
    # install_name_tool invalidates the ad-hoc signature the new ld adds
    codesign -f -s - "$out" 2>/dev/null || true
    echo "wrote $out ($iname)"
    ./bind_target_audit.sh "$out"
}

# media_stage_headers <name>: run the project's own install into
# $MEDIA/root/<name> and stage its public headers (usr/include) in $MEDIA_INC.
media_stage_headers() {
    local dest="$MEDIA/root/$1"
    rm -rf "$dest"
    meson install -C "$MEDIA_GEN/$1" --destdir "$dest" --no-rebuild > "$MEDIA_GEN/$1-install.log" 2>&1 ||
        { tail -20 "$MEDIA_GEN/$1-install.log" >&2; echo "error: $1 install failed" >&2; exit 1; }
    cp -R "$dest/usr/include/." "$MEDIA_INC/"
}
