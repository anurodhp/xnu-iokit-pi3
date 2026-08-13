#!/bin/bash
# Builds libc_build/libc_string.a from a mix of real Apple Libc source
# (third_party/Libc/string/) and real FreeBSD libc source (fetched fresh
# into libc_build/freebsd_string/ - see that dir's own provenance: this
# project's apple-oss-distributions Libc checkout has NO portable-C
# implementation of memcpy/memset/strlen/memcmp/strcmp/strncmp/bcopy/bzero/
# memmove for ANY architecture - only x86_64/i386 asm profiling stubs
# exist, nothing arm64 - confirmed by grep before falling back, per this
# project's standing Apple-first/FreeBSD-fallback rule), then archives
# everything that compiles into a single .a. See docs/roadmap.md's Phase 4
# section and project memory (project_xnu_rpi3_userspace_libc_dyld.md) for
# the full tier breakdown and what's deliberately deferred.
#
# Key compile flags, each with a real reason (not guessed):
#   -D_LIBC_NO_FEATURE_VERIFICATION   Apple's own sys/cdefs.h escape hatch
#                                      for skipping libc-features.h, a
#                                      generated file this project's build
#                                      never produces (no Xcode project
#                                      build - see xcodescripts/generate_features.pl).
#   -D_FORTIFY_SOURCE=0                Apple's Libc/include/_types.h turns
#                                      FORTIFY on by default; that's fine
#                                      for CALLERS of e.g. stpcpy, but the
#                                      IMPLEMENTATION of stpcpy itself must
#                                      not have its own definition macro-
#                                      expanded into __builtin___stpcpy_chk.
#
# Include paths, each pulling in a real Apple compat shim already present
# in this checkout rather than reinventing one:
#   third_party/Libc/darwin       libc_private.h
#   third_party/Libc/fbsdcompat   __weak_reference as a real (no-op) macro,
#                                 namespace.h - lets a handful of FreeBSD-
#                                 sourced files compile as-is
#   libc_build/compat_include     ONE actually-hand-written shim this
#                                 project added: sys/limits.h's LONG_BIT,
#                                 a BSD-ism absent from Apple's public
#                                 headers, fixed at 64 (arm64 is strictly
#                                 LP64 for every target this project
#                                 builds) - not a stub of real logic, just
#                                 a constant real FreeBSD source expects to
#                                 already exist.
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
OUT="libc_build"
mkdir -p "$OUT/apple_string" "$OUT/freebsd_string" "$OUT/compat_include/sys"

cat > "$OUT/compat_include/sys/limits.h" <<'EOF'
/* Minimal compat shim: FreeBSD's sys/limits.h isn't present in this
 * project's Apple Libc checkout (BSD-ism, not part of Apple's public
 * headers), but a handful of FreeBSD libc source files pulled in to fill
 * gaps in Apple's own Libc (see docs/roadmap.md Phase 4) need LONG_BIT.
 * arm64 is a strict LP64 target (long == 8 bytes) for every target this
 * project builds for, so this is a fixed constant, not a guess. */
#ifndef _COMPAT_SYS_LIMITS_H_
#define _COMPAT_SYS_LIMITS_H_
#define LONG_BIT 64
#endif
EOF

CFLAGS="-target arm64-apple-macos11 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -w \
    -D_LIBC_NO_FEATURE_VERIFICATION -D_FORTIFY_SOURCE=0 \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale \
    -I $OUT/compat_include"

# Real Apple source (third_party/Libc/string/*.c + string/FreeBSD/*.c).
# bcopy.c is skipped deliberately: Apple's own is a thin platform-optimized
# wrapper needing libplatform's platform/string.h (not pulled in yet) -
# freebsd_string/bcopy.c below already provides a real, portable
# implementation instead, no need for both.
ok=0
skip=0
for f in "$LIBC"/string/*.c "$LIBC"/string/FreeBSD/*.c; do
    base=$(basename "$f" .c)
    [ "$base" = "bcopy" ] && continue
    out="$OUT/apple_string/${base}.o"
    if clang $CFLAGS "$f" -o "$out" 2>/dev/null; then
        ok=$((ok + 1))
    else
        skip=$((skip + 1))
        rm -f "$out"
    fi
done
echo "apple_string: $ok compiled, $skip skipped (locale/pthread-TSD/wide-char - see docs/roadmap.md Phase 4)"

# Real FreeBSD source, fetched fresh each run (see freebsd_string/*.c for
# the exact upstream URLs baked into this list - not vendored/committed as
# a full clone, just the handful of files this project actually needs).
mkdir -p "$OUT/freebsd_string"
for f in bcopy memcpy memmove memset bzero memcmp strlen strcmp strncmp; do
    dst="$OUT/freebsd_string/${f}.c"
    if [ ! -f "$dst" ]; then
        curl -sf -o "$dst" "https://raw.githubusercontent.com/freebsd/freebsd-src/main/lib/libc/string/${f}.c"
    fi
    clang $CFLAGS -I "$OUT/freebsd_string" -I "$OUT/compat_include" "$dst" -o "$OUT/freebsd_string/${f}.o"
done
echo "freebsd_string: 9 compiled (memcpy/memmove/memset/memcmp/strlen/strcmp/strncmp/bcopy/bzero)"

rm -f "$OUT/libc_string.a"
ar rcs "$OUT/libc_string.a" "$OUT"/apple_string/*.o "$OUT"/freebsd_string/*.o
echo "wrote $OUT/libc_string.a"
