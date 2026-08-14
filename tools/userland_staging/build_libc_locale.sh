#!/bin/bash
# Builds libc_build/libc_locale.a: the ~35 Libc string/locale/wide-char
# files that were gated on real TSD (see docs/roadmap.md Phase 4's third
# pass) - genuinely unblocked, TSD confirmed working for real via a QEMU
# boot-test (tsd_bootstrap.c, 2026-08-13). REWRITTEN after an adversarial
# review of an earlier version found real bugs (see docs/roadmap.md and
# project memory for the full list) - this version fixes all of them:
#   - EVERYTHING going into the final archive/link (string tier, locale
#     tier, the TSD shim, all compat stubs) now builds with the SAME
#     toolchain/target/sysroot. An earlier version mixed the host clang's
#     default macOS target for the shim/stubs with Xcode 12's iOS target
#     for the locale tier - Mach-O's platform load command is enforced at
#     link time ("building for macOS, but linking in object file built
#     for iOS"), not just a lint warning, so that combination could never
#     actually link. Confirmed by reproducing the failure directly before
#     fixing it.
#   - -w (suppress all warnings) is GONE. It was hiding real
#     -Werror-promoted implicit-function-declaration errors for several
#     of this project's own shim functions (_open/_read/_write/_close/
#     _fstat/_getprogname/_pthread_key_create/_pthread_once/
#     _pthread_getspecific) - Libc source files calling them with zero
#     prototype in scope, silently assumed `int name()` by C's implicit-
#     declaration fallback. Fixed by force-including
#     (-include, below) a real prototypes header
#     (libc_build_prototypes.h) - which then caught two more real bugs:
#     _open's mode argument needs to be variadic (a fixed 3-arg signature
#     rejected real 2-arg call sites), and pthread_key_create's key
#     parameter needs to be `unsigned long *` (real pthread_key_t width),
#     not `int *` (was silently truncating a real 8-byte write to 4).
#     -Wno-nullability-completeness is kept - that one's genuine
#     Apple-header-style noise (missing _Nullable/_Nonnull annotations on
#     public API declarations this project doesn't own), not a
#     correctness signal.
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
OUT="libc_build"
mkdir -p "$OUT/apple_locale" "$OUT/compat_include/sys" "$OUT/compat_include/System/machine" "$OUT/compat_include/mach-o" "$OUT/compat_include/os"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

cat > "$OUT/compat_include/sys/limits.h" <<'EOF'
/* See build_libc_string.sh's identical file for the full reasoning
 * (FreeBSD's LONG_BIT, absent from Apple's public headers - fixed at 64,
 * arm64 is strictly LP64 here). */
#ifndef _COMPAT_SYS_LIMITS_H_
#define _COMPAT_SYS_LIMITS_H_
#define LONG_BIT 64
#endif
EOF

cat > "$OUT/compat_include/System/machine/cpu_capabilities.h" <<'EOF'
/* real content already exists at $XNU/osfmk/arm/cpu_capabilities.h - see
 * that file's own -I flag below; this just forwards to it from the real
 * installed-SDK-style path third_party/libpthread/private/pthread/
 * tsd_private.h expects but this non-Xcode-project build never produces. */
#include <arm/cpu_capabilities.h>
EOF

cat > "$OUT/compat_include/os/assumes.h" <<'EOF'
/* Minimal stand-in: the real os/assumes.h transitively pulls in
 * firehose/tracepoint_private.h's COMM_PAGE-based logging machinery this
 * project doesn't have, just to define os_assert-/os_assumes-family
 * macros. Confirmed by grep that
 * third_party/Libc/stdio/FreeBSD/vfprintf.c (this project's only
 * consumer of this header) never actually calls any of them - unused
 * include, empty stand-in, not a stub of real logic anything uses.
 * (Not currently compiled into this archive - kept for when stdio work
 * resumes, see docs/roadmap.md Phase 4.) */
#ifndef _OS_ASSUMES_H_COMPAT_
#define _OS_ASSUMES_H_COMPAT_
#endif
EOF

cat > "$OUT/compat_include/mach-o/dyld_priv.h" <<'EOF'
/* Same reasoning as os/assumes.h above: third_party/dyld's real
 * dyld_priv.h is from a newer checkout whose __API_UNAVAILABLE(...)
 * invocations this project's Xcode-12/iPhoneOS14.4 Availability.h
 * doesn't support - genuine SDK-version skew, and unused by
 * vfprintf.c (its only consumer here) anyway. */
#ifndef _MACH_O_DYLD_PRIV_H_COMPAT_
#define _MACH_O_DYLD_PRIV_H_COMPAT_
#endif
EOF

# Real flags Apple's own build actually uses for this exact purpose -
# found in third_party/Libc/xcodescripts/libc.xcconfig (BASE_PREPROCESSOR_MACROS)
# and libc_static.xcconfig (the VARIANT_STATIC overrides, matching this
# project's own static/no-dyld target) - not independently reinvented.
# -D__LIBC__: "I am building Libc itself" - without it, collate.h's real
#   prototypes (__collate_mbstowcs/__collate_wcsdup) are invisible at
#   their call sites (found via the compiler's exact "conflicting types"
#   + "previous IMPLICIT declaration" diagnostic, not guessed), AND
#   xlocale/_ctype.h switches from header-inlined ctype_l functions to
#   real externalized ones (third_party/Libc/locale/isctype.c provides
#   them as real linkable symbols - see below).
# -D__ct_rune_t=__darwin_ct_rune_t: FreeBSD/Darwin naming-convention gap
#   (runetype.c/tolower.c/toupper.c), same underlying `int` either way.
# -D__va_list=__builtin_va_list: same class of naming gap, hits printf-
#   adjacent files.
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/osfmk -I $XNU/libsyscall \
    -I ../../generated"

# Real Apple source: string/*.c + string/FreeBSD/*.c + locale/FreeBSD/*.c +
# locale/xlocale.c + locale/isctype.c (the real externalized ctype_l
# definitions __LIBC__ requires, found while chasing tolower_l's earlier
# "declared but never defined" gap - was missing entirely from prior
# passes, not deliberately deferred).
ok=0
skip=0
for f in "$LIBC"/string/*.c "$LIBC"/string/FreeBSD/*.c "$LIBC"/locale/FreeBSD/*.c "$LIBC/locale/xlocale.c" "$LIBC/locale/isctype.c" \
         "$LIBC/stdlib/FreeBSD/strtol.c" "$LIBC/gen/errlst.c" "$LIBC/stdtime/FreeBSD/timelocal.c"; do
    base=$(basename "$f" .c)
    # bcopy: needs libplatform's platform/string.h, freebsd_string covers it.
    # wcstod/wcstof/wcstold: need _simple.h (separate string-builder
    #   subsystem, genuinely not pulled in yet).
    case "$base" in
        bcopy|wcstod|wcstof|wcstold) continue ;;
    esac
    out="$OUT/apple_locale/${base}.o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>"$OUT/apple_locale/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/apple_locale/${base}.err"
    else
        skip=$((skip + 1))
        rm -f "$out"
        echo "  skip: $base"
    fi
done
echo "apple_locale: $ok compiled, $skip skipped"

# Real FreeBSD primitives (same real gap as build_libc_string.sh: no
# portable-C memcpy/memset/strlen/etc for any arch in this Libc checkout)
# plus the simple string functions found missing while chasing the
# locale tier's real dependency surface (memchr/strchr/strcpy/strncpy/
# strlcpy/strlcat/strnlen/ffs) - fetched fresh each run, not vendored.
mkdir -p "$OUT/freebsd_string"
for f in bcopy memcpy memmove memset bzero memcmp strlen strcmp strncmp \
         memchr strchr strchrnul strcpy strncpy strlcpy strlcat strnlen ffs; do
    dst="$OUT/freebsd_string/${f}.c"
    if [ ! -f "$dst" ]; then
        curl -sf -o "$dst" "https://raw.githubusercontent.com/freebsd/freebsd-src/main/lib/libc/string/${f}.c"
    fi
    "$CLANG" $CFLAGS -I "$OUT/freebsd_string" "$dst" -o "$OUT/freebsd_string/${f}.o"
done
echo "freebsd_string: 16 compiled"

# This project's own TSD shim (real, QEMU-confirmed platform_syscall
# bootstrap under real POSIX names) + compat stubs (os_unfair_lock/
# OSAtomic no-ops, placeholder allocator) + raw syscalls + environment
# stand-ins - ALL built with the SAME iOS-target toolchain as everything
# above now, fixing the real cross-toolchain link failure an earlier
# version of this script had.
for f in pthread_tsd_shim pthread_tsd_shim_libc_bootstrap compat_stubs \
         libc_locale_syscalls libc_locale_env_stubs libc_locale_stdio cerror_stub; do
    "$CLANG" $CFLAGS "${f}.c" -o "$OUT/${f}.o"
done

rm -f "$OUT/libc_locale.a"
ar rcs "$OUT/libc_locale.a" \
    "$OUT"/apple_locale/*.o "$OUT"/freebsd_string/*.o \
    "$OUT/pthread_tsd_shim.o" "$OUT/pthread_tsd_shim_libc_bootstrap.o" \
    "$OUT/compat_stubs.o" "$OUT/libc_locale_syscalls.o" \
    "$OUT/libc_locale_env_stubs.o" "$OUT/libc_locale_stdio.o" "$OUT/cerror_stub.o"
echo "wrote $OUT/libc_locale.a"

# Link the real smoketest with the SAME toolchain/target - the archive
# above only pulls in members actually referenced (an earlier version of
# this script linked apple_locale/*.o as a raw glob instead of an
# archive, which forces every undefined symbol in every file to resolve
# unconditionally - a real bug, fixed here).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -include libc_build_prototypes.h \
    libc_locale_smoketest.c -o "$OUT/libc_locale_smoketest.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -o libc_locale_smoketest.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT/libc_locale_smoketest.o" "$OUT/libc_locale.a"
echo "wrote libc_locale_smoketest.macho"

otool -l libc_locale_smoketest.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l libc_locale_smoketest.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
if nm -u libc_locale_smoketest.macho | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u libc_locale_smoketest.macho >&2
fi
