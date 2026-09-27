#!/bin/bash
# Shared helpers for the font/2D stack ports (DAR-384, X11GS 3): zlib,
# expat, freetype, libpng, pixman, fontconfig, cairo, libXft and the
# fontconfig tools. SOURCED by build_zlib.sh, build_expat.sh,
# build_freetype.sh, build_libpng.sh, build_pixman.sh, build_fontconfig.sh,
# build_cairo.sh, build_libXft.sh and build_cairo_smoketest.sh -- never run
# directly, and deliberately not named build_*.sh so gen_build_order.py
# does not treat it as a build step.
#
# Builds on x11_common.sh (DAR-383): same Xcode 12 iPhoneOS 14.4 toolchain,
# same X11_CFLAGS recipe (hosted, -O0, -fno-builtin, -D_FORTIFY_SOURCE=0,
# -fno-common; see that file's header for why each is there), same
# per-file compile loop (x11_compile) and the same "fail on any compile
# gap" rule (x11_compile_report).
#
# What this file adds is a link helper that takes an explicit install name
# and explicit libtool-derived version numbers, because not every library
# here lives under /usr/X11/lib:
#   /usr/lib/libz.1.dylib, /usr/lib/libexpat.1.dylib -- Apple's own zlib
#       and expat (apple-oss-distributions zlib-76 / expat-26, the macos-112
#       manifest pins), installed exactly where macOS 11 installs them
#       (zlib.xcconfig INSTALL_PATH = /usr/lib; expat.xcodeproj
#       INSTALL_PATH = /usr/lib).
#   /usr/X11/lib/lib{freetype,png16,pixman-1,fontconfig,cairo,Xft}.*.dylib
#       -- Apple's Mac OS X 10.5-10.7 X11 shipped freetype, fontconfig,
#       libpng, pixman and cairo inside /usr/X11/lib alongside the X
#       client libraries, not in /usr/lib, since they were part of the X11
#       package rather than the base OS. Same prefix DAR-383 uses.
#
# Public headers are staged the way they install: the X11-prefix ones
# under $X11_INC (= /usr/X11/include), freetype's under
# $X11_INC/freetype2 (its own install layout), and zlib/expat's under
# $FS_SYSINC (= /usr/include).

. ./x11_common.sh

FS_ROOT="$OUT/fontstack"
FS_SYSINC="$FS_ROOT/usr_include"      # staged /usr/include headers (zlib.h, expat.h)
FS_CONFIG_DIR="fontstack_config"      # checked-in per-library config headers

X11_CFLAGS+=(-I "$FS_SYSINC")

# libtool -version-info c:r:a on its darwin branch: install-name major =
# c-a, compatibility_version = c+1, current_version = c+1.r.
# fs_version_info <c:r:a> -> "major compat current"
fs_version_info() {
    local c r a
    IFS=: read -r c r a <<< "$1"
    echo "$((c - a)) $((c + 1)) $((c + 1)).$r"
}

# fs_link_dylib <out-basename> <install-name> <compat> <current> <objdir> [extra dylibs/objs/flags...]
# Writes $OUT/system/<out-basename>, gates on MH_DYLIB and
# bind_target_audit.sh, like x11_link_dylib.
fs_link_dylib() {
    local outname="$1" iname="$2" compat="$3" current="$4" objdir="$5"
    shift 5
    local out="$OUT/system/$outname"
    mkdir -p "$OUT/system"
    rm -f "$out"
    # X11_GAP_SURVEY=1: same development-only mode as x11_link_dylib's
    # (links with -undefined dynamic_lookup and lists every missing
    # libSystem export). Never ship or commit what it produces.
    if [ "${X11_GAP_SURVEY:-0}" = "1" ]; then
        "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
            -install_name "$iname" -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
            -Wl,-undefined,dynamic_lookup \
            -o "$out" "$objdir"/*.o "$@" "${X11_SYS_DYLIBS[@]}" 2>"$objdir/link.err"
        echo "SURVEY $outname undefined:"; nm -m "$out" | grep 'dynamically looked up' | awk '{print "   ", $3}' | sort -u
        return 0
    fi
    if ! "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
        -install_name "$iname" \
        -compatibility_version "$compat" -current_version "$current" \
        -Wl,-not_for_dyld_shared_cache \
        -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
        -o "$out" \
        "$objdir"/*.o "$@" "${X11_SYS_DYLIBS[@]}" \
        2>"$objdir/link.err"; then
        echo "error: $outname link failed:" >&2
        grep -E '^  "|error' "$objdir/link.err" | sed 's/, referenced from:.*//' | sort -u >&2
        exit 1
    fi
    otool -hv "$out" | grep -q " DYLIB " || { echo "error: $out is not MH_DYLIB" >&2; exit 1; }
    echo "wrote $out ($iname)"
    ./bind_target_audit.sh "$out"
}

# fs_link_exe <out.macho> <objdir> [dylibs...]: a dynamic executable with
# the same Csu recipe and LC_LOAD_DYLINKER/LC_LOAD_DYLIB/LC_MAIN
# assertions as build_x11_smoketest.sh, then bind_target_audit.sh.
fs_link_exe() {
    local exe="$1" objdir="$2"
    shift 2
    if [ "${X11_GAP_SURVEY:-0}" = "1" ]; then
        "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
            -DCRT_DYNAMIC_LINKING -B"$NEWLD_BINDIR" -Wl,-fixup_chains -Wl,-undefined,dynamic_lookup \
            -o "$exe" "$TP/Csu/start.s" "$TP/Csu/crt.c" "$objdir"/*.o "$@" "${X11_SYS_DYLIBS[@]}"
        echo "SURVEY $exe undefined:"; nm -m "$exe" | grep 'dynamically looked up' | awk '{print "   ", $3}' | sort -u
        return 0
    fi
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
        -DCRT_DYNAMIC_LINKING -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
        -o "$exe" \
        "$TP/Csu/start.s" "$TP/Csu/crt.c" \
        "$objdir"/*.o "$@" "${X11_SYS_DYLIBS[@]}"
    local bad=0
    otool -l "$exe" | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
        echo "error: $exe: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
    otool -l "$exe" | grep -q "cmd LC_LOAD_DYLIB" || { echo "error: $exe: no LC_LOAD_DYLIB" >&2; bad=1; }
    otool -l "$exe" | grep -q "cmd LC_MAIN" || { echo "error: $exe: no LC_MAIN" >&2; bad=1; }
    [ "$bad" -eq 0 ] || exit 1
    echo "wrote $exe"
    # The audit takes images only; linker flags and static archives in
    # "$@" (build_xserver.sh's -Wl,-force_load pairs) are not passed on.
    local a audit_args=()
    for a in "$@"; do
        case "$a" in *.dylib) audit_args+=("$a") ;; esac
    done
    ./bind_target_audit.sh "$exe" ${audit_args[@]+"${audit_args[@]}"}
}
