#!/bin/bash
# Builds libc_build/dns_getaddrinfo_smoketest.macho -- DAR-182's real
# end-to-end verification client. See dns_getaddrinfo_smoketest.c's own
# header for exactly what it proves and why nothing already on the image
# could prove it.
#
# Same real dynamic-link recipe as build_res_init_dnsinfo_smoketest.sh
# (generic_main_wrapper.c + Csu start.s/crt.c + the NEWLD_BINDIR override
# for Xcode 12's ld64-609 -fixup_chains crash, docs/backlog.md item d9) --
# not repeated here.
#
# Links libinfo.dylib AND libresolv.dylib explicitly, same DAR-217
# reasoning as build_ping.sh/build_ifconfig.sh's own fix this same day:
# libinfo.dylib plain-links libresolv.dylib (does not re-export it), so a
# reference reached at link time only through libinfo would record its
# bind against libinfo and abort at real dyld load -- this test needs
# _h_errno/_hstrerror from libresolv directly regardless of what
# libinfo.dylib itself does internally.
set -euo pipefail
cd "$(dirname "$0")"
OUT="libc_build"
CSU="../../third_party/Csu"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi
if [ ! -d "$CSU" ]; then
    echo "error: $CSU not found -- run ./setup_third_party.sh first" >&2
    exit 1
fi

DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE)" >&2
    exit 1
fi

for f in system/libsystem_c.dylib system/libsystem_pthread.dylib \
         system/libsystem_malloc.dylib system/libsystem_kernel.dylib \
         system/libsystem_platform.dylib system/libsystem_blocks.dylib \
         system/libdispatch.dylib system/libxpc.dylib \
         system/libsystem_notify.dylib system/libcopyfile.dylib \
         system/libdyld.dylib system/libSystem.B.dylib system/libresolv.dylib \
         system/libinfo.dylib; do
    if [ ! -f "$OUT/$f" ]; then
        echo "error: $OUT/$f not found -- run the libSystem tier build scripts first" >&2
        exit 1
    fi
done
# Named explicitly, same reason build_res_init_dnsinfo_smoketest.sh names
# libresolv.dylib on its own: this test is meaningless against a
# libinfo.dylib built before DAR-306 wired real dns_module.c in.
if [ ! -f "$OUT/system/libinfo.dylib" ]; then
    echo "error: $OUT/system/libinfo.dylib not found -- run build_libinfo.sh first" >&2
    exit 1
fi

# Real staleness gate (staleness_check.sh, DAR-271) -- this test's whole
# value is that it exercises the CURRENT libinfo.dylib/libresolv.dylib.
./staleness_check.sh "$OUT/system/libinfo.dylib" DNS_SMOKETEST_ALLOW_STALE_LIBINFO \
    ../../third_party/Libinfo/lookup.subproj -name '*.c' -o -name '*.h'
./staleness_check.sh "$OUT/system/libresolv.dylib" DNS_SMOKETEST_ALLOW_STALE_LIBRESOLV \
    ../../third_party/libresolv -maxdepth 1 -name '*.c' -o -name '*.h'

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -Wall \
    -Wno-nullability-completeness"

"$CLANG" $CFLAGS -Dmain=dns_getaddrinfo_real_main dns_getaddrinfo_smoketest.c \
    -o "$OUT/dns_getaddrinfo_smoketest.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin \
    -fno-stack-protector -O0 -Wall \
    -DREAL_MAIN=dns_getaddrinfo_real_main -DCMD_NAME='"dns_getaddrinfo_smoketest"' \
    generic_main_wrapper.c -o "$OUT/dns_getaddrinfo_smoketest_wrapper.o"

rm -f "$OUT/dns_getaddrinfo_smoketest.macho"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o "$OUT/dns_getaddrinfo_smoketest.macho" \
    "$CSU/start.s" "$CSU/crt.c" \
    "$OUT/dns_getaddrinfo_smoketest_wrapper.o" "$OUT/dns_getaddrinfo_smoketest.o" \
    "$OUT/system/libinfo.dylib" "$OUT/system/libresolv.dylib" \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_malloc.dylib" "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_blocks.dylib" \
    "$OUT/system/libdispatch.dylib" "$OUT/system/libxpc.dylib" \
    "$OUT/system/libsystem_notify.dylib" "$OUT/system/libcopyfile.dylib" \
    "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib"

M="$OUT/dns_getaddrinfo_smoketest.macho"
otool -l "$M" | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: no LC_LOAD_DYLINKER /usr/lib/dyld -- not a real dynamic executable" >&2
    exit 1
}
otool -l "$M" | grep -q "cmd LC_MAIN" || {
    echo "error: no LC_MAIN -- see build_simple_cmds.sh's LINK_DYLIBS comment" >&2
    exit 1
}

# THE gate: this test is worthless unless the real DNS-capable functions
# really come from libinfo.dylib/libresolv.dylib, not a local stand-in.
for spec in "_getaddrinfo:libinfo" "_gethostbyname2:libinfo" "_h_errno:libresolv" "_hstrerror:libresolv"; do
    s="${spec%%:*}"
    lib="${spec#*:}"
    line=$(nm -m "$M" 2>/dev/null | grep -E "external $s( |\$)" || true)
    case "$line" in
        *"(undefined)"*"(from $lib)"*) ;;
        "") echo "error: $M has no reference to $s at all" >&2; exit 1 ;;
        *) echo "error: $M does not bind $s to $lib -- got: $line" >&2; exit 1 ;;
    esac
done
echo "wrote $M ($(wc -c < "$M") bytes, real dynamic executable)"
echo "  _getaddrinfo/_gethostbyname2 bind libinfo.dylib, _h_errno/_hstrerror bind libresolv.dylib"

./bind_target_audit.sh "$M" || {
    echo "error: $M has an unresolvable two-level bind -- it would abort at dyld load" >&2
    exit 1
}
echo "  install: tools/init_binary/inject_into_sd_image.sh puts it at /usr/local/bin/"
