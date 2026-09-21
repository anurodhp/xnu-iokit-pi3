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

# stdio_flockfile_compat.h: real, already-documented gap (see
# build_libc_stdio.sh's own comment on this exact file) that this
# script also force-includes (-include, below), but this script now
# runs BEFORE build_libc_stdio.sh in the real tier order (same
# cross-dependency class as build_libplatform.sh's/build_libmalloc.sh's
# own compat-header self-sufficiency fixes) -- found 2026-09-13 via a
# live build_everything.sh run. Writing this script's own copy makes it
# self-sufficient regardless of run order; build_libc_stdio.sh writing
# the identical content again later is a harmless idempotent overwrite.
cat > "$OUT/compat_include/stdio_flockfile_compat.h" <<'EOF'
/* See build_libc_stdio.sh's own stdio_flockfile_compat.h for the full
 * real reasoning (FLOCKFILE/FUNLOCKFILE genuinely undefined in this
 * checkout; true no-op is correct on this single-threaded target).
 * Written here too since build_libc_gen.sh now runs first; do not edit
 * here, it is regenerated on every run. */
#ifndef _COMPAT_STDIO_FLOCKFILE_H_
#define _COMPAT_STDIO_FLOCKFILE_H_
#define FLOCKFILE(fp) ((void)(fp))
#define FUNLOCKFILE(fp) ((void)(fp))
#endif
EOF

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
# New real dependency as of build_libplatform.sh (see its header comment):
# libc_locale_for_stdio.a pulls in real os_unfair_lock/OSAtomic users.
if [ ! -f "$OUT/libplatform.a" ]; then
    echo "error: $OUT/libplatform.a not found -- run build_libplatform.sh first" >&2
    exit 1
fi
# New real dependency as of build_libmalloc.sh (see its header comment):
# this smoketest links real malloc/free (opendir/readdir etc genuinely
# call them) instead of compat_stubs.c's now-retired placeholder bump
# allocator.
if [ ! -f "$OUT/libmalloc.a" ]; then
    echo "error: $OUT/libmalloc.a not found -- run build_libmalloc.sh first" >&2
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
# rand48 family, wordexp, glob, vis/unvis, sysctl-heavy getcap/
# getmntinfo) needs subsystems genuinely out of scope for "the POSIX
# wrapper layer real coreutils/bash need" (real /dev/random, etc) -
# listed explicitly so the skip reasons are real, not "didn't get to it".
#
# termios.c/isatty.c/ttyname.c moved IN here 2026-08-19 (readline
# investigation) - the "full termios ioctl surface" gap this comment used
# to cite them for was never actually verified, just assumed, and turned
# out to be wrong: a disposable ioctl(TIOCGETA/TIOCSETA) probe built
# into the retired tools/init_binary/init.c (see libc_gen_syscalls.c's new ioctl/
# _ioctl header comment for the full transcript-backed writeup) proved
# third_party/xnu-7195's real BSD tty stack (bsd/dev/arm/km.c's kmopen/
# kmioctl -> bsd/kern/tty.c's ttioctl_locked/ttread) is already fully
# wired under /dev/console, including real raw-mode (TIOCSETA clearing
# ICANON|ECHO) with genuine immediate, unechoed single-byte delivery -
# exactly what tcsetattr(3)/real readline need. ps already proved
# termios.c/ttyname.c compile clean against this project's headers (see
# build_ps.sh); this promotes that from a ps-only curiosity to the
# shared tier every tool links, and lets bash_syscalls.c's tcgetattr/
# tcsetattr/tcgetpgrp/tcsetpgrp/ttyname honest-ENOTTY stand-ins retire in
# favor of the real thing.
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
$LIBC/stdlib/FreeBSD/abs.c
$LIBC/stdlib/FreeBSD/labs.c
$LIBC/stdlib/FreeBSD/llabs.c
$LIBC/stdlib/FreeBSD/bsearch.c
$LIBC/stdlib/FreeBSD/atol.c
$LIBC/stdlib/FreeBSD/atoi.c
$LIBC/stdlib/FreeBSD/strtoll.c
$LIBC/stdlib/FreeBSD/strtoul.c
$LIBC/gen/FreeBSD/termios.c
$LIBC/gen/FreeBSD/ttyname.c
$LIBC/gen/devname.c
"
# alarm.c added 2026-09-01 (DAR-153, syslogd boot investigation): real,
# unmodified FreeBSD alarm(3) (Berkeley 1993, unchanged upstream --
# setitimer(ITIMER_REAL, ...) wrapper, no real deps beyond that one
# syscall). Real gap found via a real dyld abort:
# "symbol not found in flat namespace '_alarm'" on syslogd.macho's boot
# -- root cause was libopenpam.dylib's own `-Wl,-U,_alarm` deferral
# (build_openpam_dylib.sh, added when this project's libc build had no
# real alarm() anywhere at all, confirmed via a project-wide `nm -g`
# sweep at the time). Real canonical home is libsystem_c.dylib (real
# Apple ships alarm() there, not libsystem_kernel.dylib -- it's a libc
# wrapper around a raw syscall, same split as sigaction()/nanosleep()
# elsewhere in this build). build_libsystem_c.sh's `ar x libc_gen.a`
# extraction step picks this up automatically once compiled in here.
# Needs `setitimer()` (build_libsystem_kernel.sh's own curated syscall
# list, same PR) as a real two-level-namespace dependency -- libc_gen.a's
# own smoketest link doesn't exercise alarm() specifically, but a full
# QEMU boot of syslogd.macho does.
GEN_FILES="$GEN_FILES
$LIBC/gen/FreeBSD/alarm.c
"
# isatty.c deliberately NOT pulled in here (unlike termios.c/ttyname.c
# above) -- it would collide with libc_stdio_process_shims.c's own real
# isatty() (tools/userland_staging/build_libc_stdio.sh), which has to
# define it locally so libc_stdio.a's OWN standalone smoketest (which
# proves libc_stdio.a is self-contained with zero undefined symbols,
# genuinely BEFORE libc_gen.a exists in a from-scratch build) keeps
# working without depending on this later tier. One real definition,
# living in the earlier tier every later tier already links anyway - see
# that file's header comment for why it's real, not a stub.

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
# /tmp provisioned). Real, undeclared forward dependency on libc_stdio.a/
# libc_locale_for_stdio.a, found 2026-09-13 via a live build_everything.sh
# run.
#
# CORRECTION 2026-09-21: the paragraph this replaced claimed "this is NOT
# a cycle (build_libc_stdio.sh has no dependency back on build_libc_gen.sh,
# confirmed by grep)" and made this a hard `exit 1` gate on the theory that
# gen_build_order.py would "just correctly reorder the two scripts". That
# claim went stale: build_libc_stdio.sh's own DAR-227 fix added a real
# `$OUT/libc_gen.a not found -- run build_libc_gen.sh first` hard check
# (its own file, "DAR-227: new real dependency"), which IS a real edge
# back onto this script - so the two scripts now form a genuine mutual
# cycle (gen's smoketest needs stdio's archive; stdio's archive needs
# gen's archive). A live build_everything.sh run confirmed this directly:
# gen_build_order.py detects the cycle, reports it
# ("dependency-cycle edge(s) dropped"), breaks it arbitrarily via stable
# sort, and landed build_libc_stdio.sh BEFORE build_libc_gen.sh - the
# wrong order for stdio's own hard requirement, aborting the whole
# from-scratch build before libc_gen.a (let alone anything downstream,
# including launchd) ever gets built.
#
# The actual fix belongs here, not in the ordering tool: this check gates
# only this script's OWN smoketest (everything below, through the
# libc_gen_smoketest.macho link at the end of this file) - libc_gen.a
# itself is already written above, unconditionally. A missing
# libc_stdio.a/libc_locale_for_stdio.a is real "not built yet", not a
# failure of THIS script's real product, so it gets the same soft-skip
# treatment build_libpthread.sh/build_iokituser.sh already use for their
# own forward-referencing smoketests (see their "skip: ... not yet built
# -- re-run this script later" messages) instead of a hard exit.
if [ ! -f "$OUT/libc_stdio.a" ] || [ ! -f "$OUT/libc_locale_for_stdio.a" ]; then
    echo "skip: libc_gen_smoketest.macho ($OUT/libc_stdio.a/$OUT/libc_locale_for_stdio.a"
    echo "      not yet built -- build_libc_stdio.sh needs this script's libc_gen.a"
    echo "      first, so this script must run before it. libc_gen.a is written;"
    echo "      re-run this script later for real smoketest coverage.)"
else
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -include libc_build_prototypes.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -I $LIBC/include \
    libc_gen_smoketest.c -o "$OUT/libc_gen_smoketest.o"
# Real gap, found 2026-09-13 via a live build_everything.sh run (first
# time this smoketest ever actually linked real libc_stdio.a with real
# pthread-backed locking): findfp.o/libc_stdio_shims.o's real
# pthread_mutex_lock/unlock/trylock/destroy calls (findfp.c's real
# stream-table locking, see build_libc_stdio.sh's own header comment for
# the stub->real sweep) need libpthread.a linked in for real, and crt.c's
# own optional weak_import hook (_pthread_main_thread_bootstrap) needs
# either the real archive or the same DAR-302-class -U deferral used
# elsewhere. build_libpthread.sh already runs before this script
# (position 22 vs 27, confirmed via gen_build_order.py, NOT a cycle --
# libc_stdio.a has no dependency back on libc_gen.a), so linking the
# real archive is correct here, matching build_libc_stdio.sh's own
# already-working smoketest recipe (same libclosure.a/libunwind.a
# companions for real pthread_once/TSD machinery, same real raw
# ___mmap/___munmap trap + alias for pthread_create's real stack
# allocation -- mmap/munmap are NO_SYSCALL_STUB entries in
# syscalls.master with no static provider since libSystem.B.dylib took
# over for real, see that script's own header comment for the full
# citation).
MACSDK="$(xcrun --sdk macosx --show-sdk-path 2>/dev/null)"
MMAP_TMPGEN="$(mktemp -d)"
trap 'rm -rf "$MMAP_TMPGEN"' EXIT
ARCHS=arm64 perl "$XNU/libsyscall/xcodescripts/create-syscalls.pl" \
    "$XNU/bsd/kern/syscalls.master" \
    "$XNU/libsyscall/custom" \
    "$XNU/libsyscall/Platforms" \
    iPhoneOS \
    "$MMAP_TMPGEN" >/dev/null
MMAP_SYSCFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -I $XNU/libsyscall/custom -I $XNU/libsyscall/mach -idirafter $XNU/osfmk -idirafter $MACSDK/usr/include"
"$CLANG" $MMAP_SYSCFLAGS "$MMAP_TMPGEN/___mmap.s" -o "$OUT/sys_mmap_for_libc_gen_smoketest.o"
"$CLANG" $MMAP_SYSCFLAGS "$MMAP_TMPGEN/___munmap.s" -o "$OUT/sys_munmap_for_libc_gen_smoketest.o"
rm -rf "$MMAP_TMPGEN"
trap - EXIT
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -o libc_gen_smoketest.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT/libc_gen_smoketest.o" \
    "$OUT/libc_gen.a" "$OUT/libc_stdio.a" "$OUT/libc_locale_for_stdio.a" "$OUT/libc_gen.a" "$OUT/libc_stdio.a" \
    "$OUT/pthread_tsd_shim_libc_bootstrap.o" \
    "$OUT/libpthread.a" \
    "$OUT/sys_mmap_for_libc_gen_smoketest.o" "$OUT/sys_munmap_for_libc_gen_smoketest.o" \
    "$OUT/libclosure.a" \
    "$OUT/libmalloc.a" "$OUT/cerror_stub.o" "$OUT/libplatform.a" \
    "$OUT/libunwind.a" \
    -Wl,-alias,_mprotect,'_mprotect$UNIX2003' \
    -Wl,-alias,___kill,'_kill$UNIX2003' \
    -Wl,-alias,___kill,_kill \
    -Wl,-alias,_write,'_write$UNIX2003' \
    -Wl,-alias,__mal_semwait_sleep,'_sleep$UNIX2003' \
    -Wl,-alias,___kdebug_trace,_kdebug_trace \
    -Wl,-alias,___mmap,_mmap \
    -Wl,-alias,___munmap,_munmap
echo "wrote libc_gen_smoketest.macho"

otool -l libc_gen_smoketest.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l libc_gen_smoketest.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
if nm -u libc_gen_smoketest.macho | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u libc_gen_smoketest.macho >&2
fi
fi
