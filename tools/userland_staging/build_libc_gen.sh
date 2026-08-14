#!/bin/bash
# Builds libc_build/libc_gen.a: the real Apple/FreeBSD POSIX wrapper layer
# (third_party/Libc/gen/) real coreutils/bash actually call -
# opendir/readdir/closedir/rewinddir/seekdir/telldir, getcwd, err/errx/
# warn/warnx, basename/dirname - continuing the same tier structure as
# build_libc_string.sh/build_libc_locale.sh/build_libc_stdio.sh (see
# docs/roadmap.md Phase 4 for the running tier breakdown).
#
# New real syscalls this tier needed (libc_gen_syscalls.c): __getdirentries64,
# openat, fstatfs (real fstatfs64), sysctl, sysctlbyname - each cross-checked
# against syscalls.master, see that file's own header comment.
# New shim (libc_gen_shims.c): __isthreaded=0, real/correct for this
# single-threaded target (see that file's own comment).
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
OUT="libc_build"
mkdir -p "$OUT/apple_gen" "$OUT/compat_include"

# Real values straight from third_party/xnu-7195/bsd/sys/dirent.h (this
# project's own kernel source, not guessed/approximated) -
# getdirentries64_flags_t/GETDIRENTRIES64_EOF/GETDIRENTRIES64_EXTENDED_BUFSIZE
# are a real, newer Apple userland-header addition (matching this exact
# kernel's own getdirentries64 implementation in bsd/vfs/vfs_syscalls.c)
# that simply postdates the Xcode-12/iOS-14.4 SDK headers this project's
# kext/Libc builds are pinned to (see tools/build_kexts.sh's own header
# comment for why that specific Xcode version is required) - a real SDK-
# vintage gap, not a guess at unknown values.
cat > "$OUT/compat_include/getdirentries64_compat.h" <<'EOF'
#ifndef _COMPAT_GETDIRENTRIES64_H_
#define _COMPAT_GETDIRENTRIES64_H_
typedef unsigned int getdirentries64_flags_t;
#define GETDIRENTRIES64_EOF 1U
#define GETDIRENTRIES64_EXTENDED_BUFSIZE 1024

/* _GENERIC_DIRSIZ: standard BSD dirent-record-size macro
 * (third_party/Libc/gen/FreeBSD/readdir.c's readdir_r() calls it, but it
 * isn't defined anywhere in this checkout's headers - confirmed by grep
 * across both third_party/Libc/include and the Xcode-12 iPhoneOS SDK's
 * own sys/dirent.h - another real SDK-vintage gap, same class as
 * getdirentries64_flags_t above). Standard formula (round the real
 * d_namlen up to the next 4-byte boundary, add the fixed struct dirent
 * header size before d_name) - real Apple/FreeBSD's own struct dirent
 * layout (checked above: d_ino/d_seekoff/d_reclen/d_namlen/d_type/
 * d_name, from the Xcode-12 SDK's sys/dirent.h) already gives d_namlen,
 * so this isn't approximated. */
#define _GENERIC_DIRSIZ(dp) \
    ((sizeof(struct dirent) - sizeof((dp)->d_name)) + (((dp)->d_namlen + 1 + 3) & ~3))
#endif
EOF

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

# Same real flags as build_libc_stdio.sh (see that script's own CFLAGS
# comment for why VARIANT_DARWINEXTSN is deliberately NOT set) plus
# gen/FreeBSD's own include dir for telldir.h.
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h -include $OUT/compat_include/stdio_flockfile_compat.h \
    -include $LIBC/gen/__dirent.h -include $OUT/compat_include/getdirentries64_compat.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD -I $LIBC/stdio/FreeBSD -I $LIBC/gen -I $LIBC/gen/FreeBSD \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/osfmk -I $XNU/libsyscall \
    -I ../../generated"

# Real Apple/FreeBSD source. Deliberately curated rather than a blind
# glob over gen/FreeBSD/*.c - most of that directory (arc4random, the
# rand48 family, wordexp, glob, vis/unvis, termios, sysctl-heavy getcap/
# getmntinfo) needs subsystems genuinely out of scope for "the POSIX
# wrapper layer real coreutils/bash need" (real /dev/random, full termios
# ioctl surface, etc) - listed explicitly so the skip reasons are real,
# not "didn't get to it".
GEN_FILES="
$LIBC/gen/FreeBSD/opendir.c
$LIBC/gen/FreeBSD/readdir.c
$LIBC/gen/FreeBSD/closedir.c
$LIBC/gen/FreeBSD/rewinddir.c
$LIBC/gen/FreeBSD/seekdir.c
$LIBC/gen/FreeBSD/telldir.c
$LIBC/gen/FreeBSD/getcwd.c
$LIBC/gen/dirfd.c
$LIBC/gen/FreeBSD/err.c
$LIBC/gen/FreeBSD/basename.c
$LIBC/gen/FreeBSD/dirname.c
$LIBC/gen/uname.c
$LIBC/gen/FreeBSD/getpagesize.c
$LIBC/gen/confstr.c
$LIBC/stdlib/FreeBSD/merge.c
"

ok=0
skip=0
for f in $GEN_FILES; do
    base=$(basename "$f" .c)
    out="$OUT/apple_gen/${base}.o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>"$OUT/apple_gen/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/apple_gen/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
    fi
done
echo "apple_gen: $ok compiled, $skip skipped (see docs/roadmap.md Phase 4 for what's deferred and why)"

"$CLANG" $CFLAGS libc_gen_syscalls.c -o "$OUT/libc_gen_syscalls.o"
"$CLANG" $CFLAGS libc_gen_shims.c -o "$OUT/libc_gen_shims.o"

rm -f "$OUT/libc_gen.a"
ar rcs "$OUT/libc_gen.a" \
    "$OUT"/apple_gen/*.o \
    "$OUT/libc_gen_syscalls.o" "$OUT/libc_gen_shims.o"
echo "wrote $OUT/libc_gen.a"

# Real smoketest: exercise opendir/readdir/closedir + getcwd against /tmp
# (see inject_into_sd_image.sh - this build's target image always has
# /tmp provisioned).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -include libc_build_prototypes.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -I $LIBC/include \
    libc_gen_smoketest.c -o "$OUT/libc_gen_smoketest.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -o libc_gen_smoketest.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT/libc_gen_smoketest.o" \
    "$OUT/libc_gen.a" "$OUT/libc_stdio.a" "$OUT/libc_locale_for_stdio.a" "$OUT/libc_gen.a" "$OUT/libc_stdio.a" \
    "$OUT/pthread_tsd_shim.o" "$OUT/pthread_tsd_shim_libc_bootstrap.o" \
    "$OUT/compat_stubs.o" "$OUT/cerror_stub.o"
echo "wrote libc_gen_smoketest.macho"

otool -l libc_gen_smoketest.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l libc_gen_smoketest.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
if nm -u libc_gen_smoketest.macho | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u libc_gen_smoketest.macho >&2
fi
