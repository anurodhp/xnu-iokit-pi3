#!/bin/bash
# Builds libc_build/system/libsystem_c.dylib: static-to-dylib migration
# step 4 (docs/daemon-roadmap.md's "Static-vs-dylib architecture debt"
# section).
#
# Combines libc_string.a + libc_gen.a + libc_stdio.a + libc_printf.a +
# libc_locale_for_stdio.a + libc_regex.a into ONE real dylib, matching
# real Apple's actual granularity -- real macOS does NOT split these
# into separate libsystem_string.dylib/libsystem_stdio.dylib/etc images,
# they're all one real libsystem_c.dylib. This project's own build
# scripts split them into separate .a files purely for build-pipeline
# convenience (see docs/roadmap.md Phase 4's tier breakdown); that
# internal grouping doesn't have to (and, given the real duplicate-
# global-state risk below, must not) become separate dylib images.
#
# THIS ONE IS ALSO A REAL CORRECTNESS FIX, same class as
# libsystem_malloc.dylib's g_zone and libsystem_pthread.dylib's thread
# registry. Audited before writing this script: libc_stdio's real
# `__stdinp`/`__stdoutp`/`__stderrp` (the actual FILE* globals every
# printf/fprintf/fflush call in this port goes through) are plain BSS
# globals. Splitting stdio from printf (or either from anything else
# that touches these three FILE*s) into separate dylibs would give each
# image its own private stdin/stdout/stderr, with independently
# buffered, divergent state -- writes from one dylib's copy could
# interleave incorrectly with or clobber the other's buffered data.
# Bundling all of Libc into one real dylib avoids ever creating that
# split in the first place, same real fix shape as batch 2's malloc/
# pthread findings, just avoided by not fragmenting to begin with
# rather than fixed after the fact.
#
# Real, unmodified Apple/FreeBSD source: same file sets
# build_libc_string.sh/build_libc_gen.sh/build_libc_stdio.sh/
# build_libc_printf.sh/build_libc_locale.sh/build_libc_regex.sh already
# established for their own static archives (each script's own header
# comment has the full scope/gap writeup, not re-derived here).
#
# Real correction made while writing this script: a first attempt
# linked the 5 archives directly (not pre-extracted) hoping `ld`'s
# normal selective archive-member-pulling would just replicate today's
# already-working static link. It compiled, but left this dylib
# missing basic, obviously-needed exports (_getenv, _memchr, _memmove,
# _snprintf, _strcmp, _strlcpy, _vfprintf, _fprintf, _mkstemp) --
# confirmed via nm -g showing them absent. Root cause: selective
# scanning only pulls a member when something ALREADY IN THAT SAME LINK
# needs it; in the monolithic static build these get pulled because
# libcxx.a/libcxxabi.a/libunwind.a are linked in the SAME invocation and
# need them, but a real shared dylib has to export a real, complete API
# surface for ANY future consumer, not just what happens to be needed by
# whichever specific other archives are present today. Fixed by
# extracting every member (`ar x`, same approach every dylib since
# libsystem_malloc.dylib already uses) and closing the real gaps that
# approach surfaces one at a time below, same discipline
# build_libsystem_pthread.sh's pthread_atfork.o gap already used.
#
# One real, pre-existing overlap to get right: 38 string primitives
# (strlen.o, memcpy.o, etc) exist in BOTH libc_string.a and
# libc_locale_for_stdio.a. The current static link lists
# libcxx_locale_for_stdio_scratch.a (== libc_locale_for_stdio.a minus
# pthread_tsd_shim.o) BEFORE libc_string.a, so locale's copies are what
# actually link today -- extraction order below matches (locale
# extracted after string, so its copies overwrite on disk, the same
# "last write wins" outcome `ld`'s left-to-right archive scan gives the
# earlier-listed archive).
#
# Real, honestly-scoped exclusions (not silently worked around) -- each
# pulls in a real dependency this project's build has never established
# for reasons already documented elsewhere, confirmed real via a grep
# for source, not guessed. mktemp.o/tmpfile.o/tmpnam.o/tempnam.o are
# NOT excluded, despite needing real arc4random_uniform()/
# open_dprotected_np()/__sigprocmask -- see libsystem_c_syscall_compat.c's
# own comment for why (real dyld's own LibSystemHelpers::mkstemp() needs
# a real, working mkstemp(), discovered when this dylib was first linked
# against libdyld.dylib for real, same shape as batch 3's pthread_atfork
# discovery):
#   - machdep_ldisQ.o: needs strtopQ, real gdtoa Quad(128-bit)-precision
#     long double support -- genuinely inapplicable to this target:
#     arm64's `long double` is IEEE double (64-bit), identical to
#     `double`, so the whole Quad code path is real but dead code for
#     this architecture specifically, not a missing port.
#   - wcsftime.o (needs strftime_l) and the whole wide-char scanf family
#     (wscanf.o/vwscanf.o/fwscanf.o/swscanf.o/vswscanf.o/vfwscanf.o, all
#     cascading to the same wcstod_l/wcstof_l/wcstold_l family): real
#     source exists (stdtime/FreeBSD/strftime.c, locale/FreeBSD/
#     wcstod.c) but pulling it in is a real, substantial locale-
#     integration undertaking (this project's locale work has
#     repeatedly hit exactly this class of gap before, see project
#     memory on wide-char/locale work) -- scoped out rather than guessed
#     at under time pressure; a real follow-up, not a permanent ceiling.
# Neither of these was reachable in the current, already-working static
# link either (confirmed: it links today with zero undefined symbols
# without them) -- excluding them here preserves that exact behavior
# rather than changing it.
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
XNU="../../third_party/xnu-7195"
OUT="libc_build"
EXTRACT="$OUT/libsystem_c/extracted"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
for a in libc_string libc_gen libc_stdio libc_printf libc_locale_for_stdio libc_regex; do
    if [ ! -f "$OUT/$a.a" ]; then
        echo "error: $OUT/$a.a not found -- run build_$a.sh first (this script reuses their already-built member objects)" >&2
        exit 1
    fi
done
if [ ! -f "$OUT/system/libsystem_kernel.dylib" ] || [ ! -f "$OUT/system/libsystem_platform.dylib" ] || \
   [ ! -f "$OUT/system/libsystem_malloc.dylib" ] || [ ! -f "$OUT/system/libsystem_pthread.dylib" ] || \
   [ ! -f "$OUT/system/libsystem_blocks.dylib" ]; then
    echo "error: libsystem_kernel/platform/malloc/pthread/blocks.dylib not found -- run those build scripts first" >&2
    exit 1
fi

rm -rf "$EXTRACT"
mkdir -p "$EXTRACT"
(cd "$EXTRACT" && ar x "../../libc_string.a")
(cd "$EXTRACT" && ar x "../../libc_gen.a")
(cd "$EXTRACT" && ar x "../../libc_stdio.a")
(cd "$EXTRACT" && ar x "../../libc_locale_for_stdio.a")
(cd "$EXTRACT" && ar x "../../libc_printf.a")
(cd "$EXTRACT" && ar x "../../libc_regex.a")
rm -f "$EXTRACT/__.SYMDEF"* "$EXTRACT/pthread_tsd_shim.o" \
    "$EXTRACT/machdep_ldisQ.o" "$EXTRACT/wcsftime.o" "$EXTRACT/vfwscanf.o" "$EXTRACT/vswscanf.o" \
    "$EXTRACT/wscanf.o" "$EXTRACT/vwscanf.o" "$EXTRACT/fwscanf.o" "$EXTRACT/swscanf.o"

# Real gap: sys_siglist (real strsignal_r() dependency) and strvisx
# (real ___printf_render_vis() dependency) have real FreeBSD source
# neither build_libc_gen.sh nor any other tier's own file list happened
# to include yet -- added directly here, same real source, not stubbed.
# Same real CFLAGS/include set build_libc_locale.sh already established
# for locale-touching real Apple/FreeBSD source (vis.c pulls in
# ctype.h's real xlocale machinery) -- not re-derived twice.
PTHREAD="../../third_party/libpthread"
SIGLIST_CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall \
    -I ../../generated"
"$CLANG" $SIGLIST_CFLAGS "$LIBC/gen/FreeBSD/siglist.c" -o "$EXTRACT/siglist.o"
"$CLANG" $SIGLIST_CFLAGS "$LIBC/gen/FreeBSD/vis.c" -o "$EXTRACT/vis.o"

# Real gap: mktemp.o (kept in this dylib -- real dyld's own
# LibSystemHelpers::mkstemp() needs it, see libsystem_c_syscall_compat.c's
# own comment) needs real arc4random_uniform(). Real Apple source
# (gen/FreeBSD/arc4random.c) compiles clean under this project's
# standard -DVARIANT_STATIC (already proven by build_libresolv.sh's own
# identical compile, same real files reused verbatim, not re-derived).
"$CLANG" $SIGLIST_CFLAGS -include stdlib.h "$LIBC/gen/FreeBSD/arc4random.c" -o "$EXTRACT/arc4random.o"
"$CLANG" $SIGLIST_CFLAGS libresolv_getentropy_syscall.c -o "$EXTRACT/getentropy_syscall.o"
# NOT libresolv_arc4random_deps_compat.o here (unlike build_libresolv.sh):
# that file's real open$UNIX2003/read$UNIX2003 forwarders exist only
# because libresolv's own narrower build never otherwise pulls in a real
# definition of either -- this dylib already has one for real, from
# libc_gen_syscalls.o (part of libc_gen.a, extracted above), so
# compiling this file too would just duplicate-define the same symbols.
"$CLANG" $SIGLIST_CFLAGS libsystem_c_syscall_compat.c -o "$EXTRACT/syscall_compat.o"

# Real gap: real access()/fstatat()/ftruncate()/mkdirat()/rmdir()/
# unlink()/sigprocmask() raw syscall stubs -- same real create-
# syscalls.pl generator + recipe build_libmalloc.sh already established
# (see that script's own header comment), just never previously
# generated for these specific syscalls since nothing needed them
# before this dylib's own real API-completeness requirement (see this
# script's own header comment on why "ar x everything" surfaces gaps a
# narrower, selective static link never had to close).
TMPGEN="$(mktemp -d)"
trap 'rm -rf "$TMPGEN"' EXIT
ARCHS=arm64 perl "$XNU/libsyscall/xcodescripts/create-syscalls.pl" \
    "$XNU/bsd/kern/syscalls.master" \
    "$XNU/libsyscall/custom" \
    "$XNU/libsyscall/Platforms" \
    iPhoneOS \
    "$TMPGEN" >/dev/null
MACSDK="$(xcode-select -p)/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"
SYSCFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -I $XNU/libsyscall/custom -I $XNU/libsyscall/mach -idirafter $XNU/osfmk -idirafter $MACSDK/usr/include"
for s in _access _ftruncate _mkdirat _sigprocmask _read; do
    "$CLANG" $SYSCFLAGS "$TMPGEN/${s}.s" -o "$EXTRACT/sys${s}.o"
done
# fstatat: real generated _fstatat.s is an empty placeholder (no
# MI_ENTRY_POINT at all, confirmed by reading the generator's own
# output, not guessed) -- the real implementation is _fstatat64.s,
# which also defines plain `_fstatat` itself as a real `.set` alias to
# `_fstatat64` (same real 64-bit-inode convention this project's other
# tiers already rely on, e.g. build_libc_gen.sh's own getdirentries64
# comment).
"$CLANG" $SYSCFLAGS "$TMPGEN/_fstatat64.s" -o "$EXTRACT/sys_fstatat64.o"
# rmdir/unlink are real cancelable syscalls (real Apple convention: the
# generator emits the raw, non-cancelable trap under a triple-underscore
# name, needing a public single-underscore alias -- same real shape
# build_libdyld.sh's own -Wl,-alias list already uses for ___kill/etc).
"$CLANG" $SYSCFLAGS "$TMPGEN/___rmdir.s" -o "$EXTRACT/sys_rmdir.o"
"$CLANG" $SYSCFLAGS "$TMPGEN/___unlink.s" -o "$EXTRACT/sys_unlink.o"

mkdir -p "$OUT/system"
rm -f "$OUT/system/libsystem_c.dylib"
# ___progname: a real per-PROCESS global that lives in the main
# executable's own crt.c (third_party/Csu/crt.c), set from argv[0] at
# startup -- NOT something libsystem_c.dylib itself should define (real
# Apple's own architecture: __progname genuinely lives in the
# executable's own crt1.o, libSystem just references it). Marked as an
# intentional dynamic lookup (same real -Wl,-U mechanism
# _dyld_initializer already uses), resolved at runtime against whatever
# process image actually defines it.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libsystem_c.dylib \
    -Wl,-fixup_chains \
    -Wl,-U,___progname \
    -Wl,-alias,___kill,_kill \
    -Wl,-alias,___rmdir,_rmdir \
    -Wl,-alias,___unlink,_unlink \
    -Wl,-alias,___kill,'_kill$UNIX2003' \
    -Wl,-alias,_mprotect,'_mprotect$UNIX2003' \
    -Wl,-alias,_write,'_write$UNIX2003' \
    -Wl,-alias,_open,'_open$UNIX2003' \
    -Wl,-alias,_close,'_close$UNIX2003' \
    -Wl,-alias,_fcntl,'_fcntl$UNIX2003' \
    -Wl,-alias,_fstat,'_fstat$INODE64' \
    -Wl,-alias,_lstat,'_lstat$INODE64' \
    -Wl,-alias,_stat,'_stat$INODE64' \
    -Wl,-alias,_fsync,'_fsync$UNIX2003' \
    -Wl,-alias,_read,'_read$UNIX2003' \
    -Wl,-alias,_reallocarray,'_reallocarray$DARWIN_EXTSN' \
    -Wl,-alias,'_mmap$UNIX2003',_mmap \
    -Wl,-alias,'_munmap$UNIX2003',_munmap \
    -Wl,-alias,__pthread_cond_wait,_pthread_cond_wait \
    -Wl,-alias,_pthread_testcancel,'_pthread_testcancel$UNIX2003' \
    -Wl,-alias,'_pthread_rwlock_wrlock$UNIX2003',_pthread_rwlock_wrlock \
    -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003',_pthread_rwlock_rdlock \
    -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003',_pthread_rwlock_unlock \
    -o "$OUT/system/libsystem_c.dylib" \
    "$EXTRACT"/*.o \
    "$OUT/system/libsystem_kernel.dylib" "$OUT/system/libsystem_platform.dylib" \
    "$OUT/system/libsystem_malloc.dylib" "$OUT/system/libsystem_pthread.dylib" "$OUT/system/libsystem_blocks.dylib"

echo "wrote $OUT/system/libsystem_c.dylib"
otool -hv "$OUT/system/libsystem_c.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
if nm -u "$OUT/system/libsystem_c.dylib" | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u "$OUT/system/libsystem_c.dylib" >&2
else
    echo "libsystem_c.dylib: fully self-contained (beyond libsystem_kernel/platform/malloc/pthread/blocks.dylib), zero undefined symbols"
fi
