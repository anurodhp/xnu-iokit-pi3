#!/bin/bash
# Builds libc_build/libc_locale.a: the ~35 Libc string/locale/wide-char
# files that were gated on real TSD (see docs/roadmap.md Phase 4's third
# pass) - now genuinely unblocked, TSD confirmed working for real via a
# QEMU boot-test (tsd_bootstrap.c, 2026-08-13).
#
# KEY DIFFERENCE from build_libc_string.sh: that script used the plain
# host clang + its modern default SDK, which was fine for pure string/mem
# functions with minimal header dependencies. These locale files pull in
# much deeper system-header territory (pthread.h, malloc headers) via
# xlocale_private.h, and the *host's* modern SDK's Availability.h expects
# newer macro forms (__API_AVAILABLE with more OS-version arguments) than
# this project's -target arm64-apple-macos11 line ever hits in normal
# use - real, genuine SDK-version skew, not a bug in these files. Fixed
# the same way third_party/xnu-7195's own kext builds already do
# (tools/build_kexts.sh): use Xcode 12's iPhoneOS SDK instead of the
# host's default modern one - Xcode 12 (~2020) is contemporaneous with
# this project's Darwin-20.3-vintage target, so its Availability.h never
# hits the newer macro forms at all. iOS vs macOS doesn't matter for this
# pure-C code; Libc's own source already branches on TARGET_OS_IPHONE
# internally for exactly this kind of portability.
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
OUT="libc_build"
mkdir -p "$OUT/apple_locale" "$OUT/compat_include/System/machine"

# real content already exists in this project at $XNU/osfmk/arm/cpu_capabilities.h -
# this just forwards to it from the real installed-SDK-style path
# (System/machine/cpu_capabilities.h) that third_party/libpthread/private/
# pthread/tsd_private.h expects but this non-Xcode-project build never
# produces (see build_libc_string.sh's own compat_include/sys/limits.h
# for the same pattern).
cat > "$OUT/compat_include/System/machine/cpu_capabilities.h" <<'EOF'
#include <arm/cpu_capabilities.h>
EOF

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -w \
    -D_LIBC_NO_FEATURE_VERIFICATION -D_FORTIFY_SOURCE=0 \
    -D__LIBC__ -D__ct_rune_t=__darwin_ct_rune_t \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/osfmk -I $XNU/libsyscall \
    -I ../../generated \
    -I $OUT/compat_include"
# -D__LIBC__: real Apple build-system flag ("I am building Libc itself") -
#   without it, collate.h/xlocale_private.h's own #ifdef __LIBC__ guards
#   skip real prototypes (e.g. __collate_mbstowcs/__collate_wcsdup), so
#   the compiler falls back to an implicit (wrong) declaration at each
#   call site and then fails with "conflicting types" once it reaches the
#   real, correctly-typed definition later in the same file - a real,
#   correct fix (the flag Apple's own build always sets for this exact
#   source), not a suppression.
# -D__ct_rune_t=__darwin_ct_rune_t: this SDK only exposes the Darwin-
#   renamed public name (__darwin_ct_rune_t, both = int on arm64/AAPCS64);
#   several FreeBSD-lineage files (runetype.c, tolower.c, toupper.c) still
#   reference the original private FreeBSD name directly. Same underlying
#   type either way, just a naming-convention gap between the two
#   lineages' internal header sets.

ok=0
skip=0
for f in "$LIBC"/string/*.c "$LIBC"/string/FreeBSD/*.c "$LIBC"/locale/FreeBSD/*.c "$LIBC/locale/xlocale.c"; do
    base=$(basename "$f" .c)
    # bcopy: same reason as build_libc_string.sh (needs libplatform's
    # platform/string.h, freebsd_string/bcopy.c already covers it).
    # wcstod/wcstof/wcstold: need _simple.h (a separate string-builder
    # subsystem, not yet pulled in) - genuinely deferred, not silently
    # dropped, see docs/roadmap.md Phase 4 for the real next-step list.
    case "$base" in
        bcopy|wcstod|wcstof|wcstold) continue ;;
    esac
    out="$OUT/apple_locale/${base}.o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>/dev/null; then
        ok=$((ok + 1))
    else
        skip=$((skip + 1))
        rm -f "$out"
        echo "  skip: $base"
    fi
done
echo "apple_locale: $ok compiled, $skip skipped"

# Real-named TSD shim (confirmed-working platform_syscall bootstrap +
# pthread_key_create/setspecific/getspecific/key_init_np/once) and the
# small compat stubs (os_unfair_lock/OSAtomic no-ops, placeholder
# allocator - see compat_stubs.c's own header for exactly what it is and
# isn't) these locale files need. Built with the plain host clang since
# neither has any Libc/system header dependency to trigger the SDK-skew
# problem above.
clang -target arm64-apple-macos11 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -w \
    pthread_tsd_shim.c -o "$OUT/pthread_tsd_shim.o"
clang -target arm64-apple-macos11 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -w \
    compat_stubs.c -o "$OUT/compat_stubs.o"

rm -f "$OUT/libc_locale.a"
ar rcs "$OUT/libc_locale.a" "$OUT"/apple_locale/*.o "$OUT/pthread_tsd_shim.o" "$OUT/compat_stubs.o"
echo "wrote $OUT/libc_locale.a"
