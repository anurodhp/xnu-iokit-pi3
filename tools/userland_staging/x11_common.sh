#!/bin/bash
# Shared toolchain + compile/link helpers for the X11 client-library
# ports (DAR-383, X11GS 2). SOURCED by build_xorg_headers.sh and every
# build_libX*.sh / build_libxcb.sh / build_libICE.sh / build_libSM.sh /
# build_x11_smoketest.sh -- never run directly, and deliberately not named
# build_*.sh so gen_build_order.py does not treat it as a build step.
#
# Why hand-written compile loops instead of each library's own
# ./configure: X.org's git tags carry configure.ac, not a generated
# configure script, and generating one needs automake + libtool +
# xorg-macros on the host (not installed here). More to the point, the
# link this project needs is not what libtool would produce: -nostdlib
# against this project's own libc_build/system/*.dylib, each real owner
# listed explicitly (build_netstat.sh's LINK_DYLIBS comment explains why
# libSystem.B's re-exports cannot be relied on at link time), with
# bind_target_audit.sh as a hard gate. So each library gets the same shape
# as build_libc_regex.sh / build_vim.sh: a real per-file compile loop over
# the library's own Makefile.am source list, a checked-in config.h under
# x11_config/<lib>/ whose every define cites the configure.ac check it
# stands in for, and a real dylib link.
#
# Sources: real freedesktop/X.org upstream at the release tags nixpkgs
# nixos-26.05 (rev 8f0500b9, the nixpkgs PureDarwin's flake.lock pins)
# ships -- see setup_third_party.sh's X.org block. Unmodified; nothing is
# forked.
#
# Install layout: /usr/X11 (lib/, include/, share/X11/), Apple's own
# layout for the X11 it shipped in Mac OS X 10.5-10.7 (/usr/X11, with
# /usr/X11R6 a symlink to it); XQuartz later moved the same tree to
# /opt/X11. Install names are therefore /usr/X11/lib/libX11.6.dylib etc,
# with libtool's own darwin major-version naming (see x11_dylib_versions).

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
# Same ld64-609 -fixup_chains crash workaround every dylib here uses
# (docs/backlog.md item d9; build_libiconv_dylib.sh's header).
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- see docs/backlog.md item d9" >&2
    exit 1
fi

TP="../../third_party"
OUT="libc_build"
X11_ROOT="$OUT/x11"
X11_INC="$X11_ROOT/include"          # staged public headers, laid out as /usr/X11/include
X11_PREFIX="/usr/X11"
X11_CONFIG_DIR="x11_config"          # checked-in per-library config.h files

# Base compile flags. Same recipe as every other third-party port here
# (build_vim.sh): Xcode 12's iPhoneOS 14.4 SDK headers, -fno-builtin
# -fno-stack-protector, optimised at $X11_OPT (default -O2, DAR-438; was
# -O0 only because "same recipe as build_vim.sh"). NOT -ffreestanding, unlike the libc-internal
# archives: these are ordinary hosted POSIX clients, and under
# -ffreestanding clang's own <limits.h> stops #include_next-ing the SDK's,
# so IOV_MAX/PATH_MAX vanish (libxcb's xcb_conn.c/xcb_util.c fail to
# compile) -- build_vim.sh papers over the same thing with
# -include sys/syslimits.h; a hosted compile is the honest fix here. -D_FORTIFY_SOURCE=0 is
# load-bearing, not hygiene: the SDK's fortify inlines rewrite
# memcpy/sprintf/... into __*_chk calls, and build_vim.sh documents the
# self-recursing __vsprintf_chk that produced. -fno-common matches
# clang 11's default on Darwin anyway; spelled out so a tentative
# definition duplicated across two X sources fails the link instead of
# silently merging.
# X11_OPT: override from the environment (e.g. X11_OPT=-O0) to rebuild one
# component unoptimised if -O2 is found to expose UB in it; record the
# evidence next to that component's build script.
# -fno-builtin stays (checked 2026-09-30 by compiling with Xcode 12's clang
# at -O2 -fno-builtin -D_FORTIFY_SOURCE=0 and reading the -S output):
# memcpy(d,s,n) stays `b _memcpy`, constant-size memcpy(d,s,32) stays
# `bl _memcpy` (without the flag it is inlined as ldp/stp q), and a
# hand-written zeroing loop stays a loop (without it, it becomes
# `bl _bzero`). So every memcpy/memset keeps calling libsystem_platform's
# real arm64 _platform_memmove/_platform_memset via libsystem_c, and -O2
# introduces no new libc calls.
X11_OPT="${X11_OPT:--O2}"
X11_CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c
    -fno-builtin -fno-stack-protector $X11_OPT -fno-common
    -D_FORTIFY_SOURCE=0 -DHAVE_CONFIG_H
    -Wno-nullability-completeness -Wno-deprecated-declarations
    -I "$X11_INC")

# The base system dylibs every X library links. Real owner of each symbol
# listed before libSystem.B.dylib (bind_target_audit.sh's rule, DAR-217).
X11_SYS_DYLIBS=("$OUT/system/libsystem_kernel.dylib" "$OUT/system/libsystem_c.dylib"
    "$OUT/system/libsystem_pthread.dylib" "$OUT/system/libsystem_malloc.dylib"
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libdyld.dylib"
    "$OUT/system/libSystem.B.dylib")

x11_require_sys_dylibs() {
    local d
    for d in libsystem_kernel libsystem_c libsystem_pthread libsystem_malloc libsystem_platform; do
        if [ ! -f "$OUT/system/$d.dylib" ]; then
            echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2
            exit 1
        fi
    done
    if [ ! -f "$OUT/system/libdyld.dylib" ]; then
        echo "error: $OUT/system/libdyld.dylib not found -- run build_libdyld.sh first" >&2
        exit 1
    fi
    if [ ! -f "$OUT/system/libSystem.B.dylib" ]; then
        echo "error: $OUT/system/libSystem.B.dylib not found -- run build_libsystem.sh first" >&2
        exit 1
    fi
}

x11_require_source() {
    if [ ! -e "$1" ]; then
        echo "error: $1 not found -- run setup_third_party.sh first" >&2
        exit 1
    fi
}

# x11_compile <objdir> <src.c> [extra clang args...]
# Object name is derived from the path relative to third_party so two
# same-basename sources (libX11's many modules/*/*.c) can never collide --
# build_vim.sh's libvterm screen.o lesson.
X11_OK=0
X11_FAIL=0
x11_compile() {
    local objdir="$1" src="$2"
    shift 2
    local rel="${src#$TP/}"
    rel="${rel#./}"
    local base
    base=$(echo "${rel%.c}" | tr '/' '_')
    local out="$objdir/$base.o"
    if "$CLANG" "${X11_CFLAGS[@]}" "$@" "$src" -o "$out" 2>"$objdir/$base.err"; then
        X11_OK=$((X11_OK + 1))
        [ -s "$objdir/$base.err" ] || rm -f "$objdir/$base.err"
    else
        X11_FAIL=$((X11_FAIL + 1))
        echo "  gap: $rel -- $(grep -m1 'error:' "$objdir/$base.err")" >&2
    fi
}

# x11_compile_report <name>: fail the build on any compile gap (a real,
# clean tier or nothing -- build_netstat.sh's rule).
x11_compile_report() {
    echo "$1: $X11_OK compiled, $X11_FAIL failed"
    if [ "$X11_FAIL" -ne 0 ]; then
        echo "error: $1 has real compile failures (see the *.err files next to its objects)" >&2
        exit 1
    fi
}

# x11_dylib_versions <M:m:r> -> "major compat current" as libtool's darwin
# branch computes them for -version-number M:m:r (libtool: current=M+m,
# age=m, revision=r; install-name major = current-age = M; darwin
# compatibility_version = current+1, current_version = current+1.revision).
x11_dylib_versions() {
    local M m r
    IFS=: read -r M m r <<< "$1"
    local current=$((M + m))
    echo "$M $((current + 1)) $((current + 1)).$r"
}

# x11_link_dylib <out-basename> <lib-stem> <M:m:r> <objdir> [extra dylibs/objs...]
#   e.g. x11_link_dylib libXau.dylib libXau 6:0:0 "$OBJ"
# Writes $OUT/system/<out-basename> with install name
# /usr/X11/lib/<lib-stem>.<major>.dylib, then gates on the DYLIB filetype
# and bind_target_audit.sh.
x11_link_dylib() {
    local outname="$1" stem="$2" vn="$3" objdir="$4"
    shift 4
    local major compat current
    read -r major compat current <<< "$(x11_dylib_versions "$vn")"
    local out="$OUT/system/$outname"
    mkdir -p "$OUT/system"
    rm -f "$out"
    # X11_GAP_SURVEY=1: development aid only. Links with
    # -undefined dynamic_lookup so a whole dependency chain can be linked
    # in one pass and every missing libSystem export listed at once
    # (gap_audit.sh then classifies them). The result is NOT shippable:
    # it skips bind_target_audit.sh, so always rerun the script without
    # X11_GAP_SURVEY before using or committing anything it produced.
    if [ "${X11_GAP_SURVEY:-0}" = "1" ]; then
        "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
            -install_name "$X11_PREFIX/lib/$stem.$major.dylib" \
            -B"$NEWLD_BINDIR" -Wl,-fixup_chains -Wl,-undefined,dynamic_lookup \
            -o "$out" "$objdir"/*.o "$@" "${X11_SYS_DYLIBS[@]}" 2>"$objdir/link.err"
        echo "SURVEY $outname undefined:"; nm -m "$out" | grep 'dynamically looked up' | awk '{print "   ", $3}' | sort -u
        return 0
    fi
    if ! "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
        -install_name "$X11_PREFIX/lib/$stem.$major.dylib" \
        -compatibility_version "$compat" -current_version "$current" \
        -Wl,-not_for_dyld_shared_cache \
        -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
        -o "$out" \
        "$objdir"/*.o "$@" "${X11_SYS_DYLIBS[@]}" \
        2>"$objdir/link.err"; then
        echo "error: $outname link failed; undefined symbols:" >&2
        grep -E '^  "|error' "$objdir/link.err" | sed 's/, referenced from:.*//' | sort -u >&2
        exit 1
    fi
    otool -hv "$out" | grep -q " DYLIB " || { echo "error: $out is not MH_DYLIB" >&2; exit 1; }
    echo "wrote $out ($X11_PREFIX/lib/$stem.$major.dylib)"
    ./bind_target_audit.sh "$out"
}

# x11_stage_headers <dest-subdir-under-X11_INC> <file...>
x11_stage_headers() {
    local dest="$X11_INC/$1"
    shift
    mkdir -p "$dest"
    cp "$@" "$dest/"
}

x11_require_staged() {
    if [ ! -e "$X11_INC/$1" ]; then
        echo "error: $X11_INC/$1 not found -- run $2 first" >&2
        exit 1
    fi
}

# x11_am_sources <Makefile.am> <VARIABLE>
# Prints the .c entries of one automake variable (e.g. libX11_la_SOURCES),
# joining backslash continuations, so each build script compiles the
# library's OWN source list rather than a hand-copied one. Only the
# plain `VAR = ...` / `VAR += ...` assignments are read; the caller picks
# which conditional variables apply and says why.
x11_am_sources() {
    awk -v var="$2" '
        { line = $0 }
        cont { buf = buf " " line; if (line !~ /\\$/) { cont = 0; emit(buf) } ; next }
        $1 == var && ($2 == "=" || $2 == "+=") {
            buf = line; if (line ~ /\\$/) { cont = 1 } else { emit(buf) }
        }
        function emit(s,   n, a, i) {
            gsub(/\\/, " ", s); sub(/^[^=]*=/, "", s)
            n = split(s, a, /[ \t]+/)
            for (i = 1; i <= n; i++) if (a[i] ~ /\.c$/) print a[i]
        }
    ' "$1"
}
