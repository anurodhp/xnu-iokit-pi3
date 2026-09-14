#!/bin/bash
# Builds libc_build/libremovefile.a: real Apple removefile (real
# removefile()/removefileat()/removefile_state_alloc/removefile_cancel -
# the library `rm -r` actually links against). Continues the tier
# structure of build_libc_string.sh/build_libplatform.sh/
# build_libc_locale.sh/build_libc_stdio.sh/build_libc_gen.sh/
# build_libc_printf.sh/build_libmalloc.sh/build_libpthread.sh/
# build_libclosure.sh (see docs/roadmap.md Phase 4 for the running tier
# breakdown). third_party/removefile is already cloned+pinned
# (removefile-49.40.3, see setup_third_party.sh).
#
# Scope: all 5 real Apple .c files (removefile.c, removefile_rename_unlink.c,
# removefile_sunlink.c, removefile_tree_walker.c, removefile_random.c) - 0
# skipped. Real removefile() is NOT a thin unlink() wrapper even for a
# single file: it unconditionally calls __removefile_tree_walker(), which
# opens an FTS(3) stream even on a single path - so getting ANY real
# removefile() call working, not just REMOVEFILE_RECURSIVE ones, requires
# a real, working FTS underneath. This project's Libc checkout only has
# the MODERN fts.c (third_party/Libc/gen/fts.c), which uses real
# getattrlistbulk(2) for directory reads (a newer, faster replacement for
# classic readdir-based fts - Apple's real current implementation, not a
# simplification this project chose) plus real Blocks (Block_copy/
# Block_release on the optional comparator, __BLOCKS__-gated) and real
# os_assert(). All three are genuinely available now:
#   - getattrlistbulk (syscall 461, third_party/xnu-7195/bsd/kern/
#     syscalls.master) is NOT NO_SYSCALL_STUB - the real Apple Perl
#     generator (same one build_libsyscall_smoketest.sh already uses)
#     produces a real stub for it, regenerated fresh below rather than
#     hand-written asm.
#   - Block_copy/Block_release: real libc_build/libclosure.a (see
#     build_libclosure.sh).
#   - os_assert(x): same real command-line-macro fallback
#     build_libclosure.sh already established for runtime.cpp
#     (evaluates x and traps via __builtin_trap() on failure - a real
#     halt, not a silent no-op).
#
# Real SDK-vintage gap found and fixed (same class as build_libc_gen.sh's
# getdirentries64_flags_t / build_libplatform.sh's SWITCH_OPTION_*):
# F_GETPATH is absent from the Xcode-12/iPhoneOS14.4 SDK's fcntl.h
# (postdates this SDK vintage) but IS a real, stable Darwin fcntl command
# (value 50, confirmed against this project's own kernel source,
# third_party/xnu-7195/bsd/sys/fcntl.h) - defined directly, not guessed.
#
# HAVE_CHFLAGS/HAVE_LINUX_EXT2_FS_H/HAVE_SYS_VFS_H/HAVE_SYS_PARAM_H/
# HAVE_SYS_MOUNT_H are real configure-time feature macros removefile_
# sunlink.c checks via #if - none of them are ever defined by Apple's own
# non-autoconf build either (confirmed: no config.h/HAVE_* definition
# anywhere in third_party/removefile), so on real Apple builds these all
# evaluate to 0/false and the guarded blocks (chflags-clearing,
# Linux-only ext2 secure-delete ioctls, BSD statfs-based rotational-disk
# detection) are simply real, normally-dead code paths here too - not a
# gap this project introduced.
set -e
cd "$(dirname "$0")"

REMOVEFILE="../../third_party/removefile"
CLOSURE="../../third_party/libclosure"
LIBC="../../third_party/Libc"
XNU="../../third_party/xnu-7195"
PTHREAD="../../third_party/libpthread"
OUT="libc_build"
mkdir -p "$OUT/removefile" "$OUT/compat_include"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
for dep in libmalloc libplatform libclosure libc_gen libc_stdio; do
    if [ ! -f "$OUT/$dep.a" ]; then
        echo "error: $OUT/$dep.a not found -- run build_$dep.sh first" >&2
        exit 1
    fi
done

mkdir -p "$OUT/compat_include/mach"
cat > "$OUT/compat_include/mach/clock_reply_server.h" <<'EOF'
/* Empty stand-in: real content is a MIG-generated Mach IPC server-side
 * dispatch header (osfmk/mach/mach_interface.h pulls it in unconditionally
 * as part of the general Mach IPC bundle). Confirmed by grep that
 * removefile_rename_unlink.c - the only real removefile source file that
 * transitively reaches this include (via ctype.h -> xlocale_private.h ->
 * tsd_private.h -> spinlock_private.h -> mach.h -> mach_interface.h) -
 * never calls any clock_reply_server function; this project has no MIG
 * pipeline to generate the real header from clock_reply.defs anyway (same
 * class of gap as build_libc_locale.sh's os/assumes.h/mach-o/dyld_priv.h
 * stand-ins - unused include, empty stand-in, not a stub of real logic
 * anything calls). */
#ifndef _MACH_CLOCK_REPLY_SERVER_H_COMPAT_
#define _MACH_CLOCK_REPLY_SERVER_H_COMPAT_
#endif
EOF

# Same reasoning as clock_reply_server.h above - mach_interface.h pulls in
# a whole batch of MIG-generated *_server.h dispatch headers unconditionally
# (this project has no MIG pipeline), none of which removefile_rename_
# unlink.c's real code path ever calls into (it only reaches mach.h
# transitively through xlocale_private.h's TSD/spinlock machinery, not for
# any Mach IPC of its own).
for h in exc_server mach_exc_server notify_server memory_object_default_server; do
    hupper=$(echo "$h" | tr '[:lower:]' '[:upper:]')
cat > "$OUT/compat_include/mach/${h}.h" <<EOF
#ifndef _MACH_${hupper}_H_COMPAT_
#define _MACH_${hupper}_H_COMPAT_
#endif
EOF
done

cat > "$OUT/compat_include/removefile_fcntl_compat.h" <<'EOF'
/* F_GETPATH: real, stable Darwin fcntl(2) command (value 50), postdates
 * this project's pinned Xcode-12/iPhoneOS14.4 SDK's own fcntl.h - see
 * build_removefile.sh's header comment for the cross-check against this
 * project's own kernel source (bsd/sys/fcntl.h). */
#ifndef F_GETPATH
#define F_GETPATH 50
#endif
EOF

# Real Apple syscall generator (same invocation build_libsyscall_smoketest.sh
# uses) - regenerated fresh each build so this can't silently drift from
# syscalls.master. getattrlistbulk (461) is real Libc/gen/fts.c's own
# directory-read primitive, not something this project invented.
TMPGEN="$(mktemp -d)"
trap 'rm -rf "$TMPGEN"' EXIT
ARCHS=arm64 perl "$XNU/libsyscall/xcodescripts/create-syscalls.pl" \
    "$XNU/bsd/kern/syscalls.master" \
    "$XNU/libsyscall/custom" \
    "$XNU/libsyscall/Platforms" \
    iPhoneOS \
    "$TMPGEN" >/dev/null
SYSCALL_ASFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -arch arm64 \
    -I $XNU/libsyscall/custom -I $XNU/libsyscall/mach -I $XNU/osfmk \
    -I $XNU/BUILD.hdrs/dst/usr/include"
"$CLANG" $SYSCALL_ASFLAGS "$TMPGEN/_getattrlistbulk.s" -o "$OUT/removefile/getattrlistbulk.o"
# mkdir(2): needed by the smoketest to build a real directory tree (not
# strictly a removefile dependency, but this is the natural place to add
# it - same real generator, same real syscalls.master entry, #136, not
# NO_SYSCALL_STUB).
"$CLANG" $SYSCALL_ASFLAGS "$TMPGEN/_mkdir.s" -o "$OUT/removefile/mkdir.o"
# geteuid (#25) and chflags (#34): real removefile_tree_walker.c calls,
# neither marked NO_SYSCALL_STUB in syscalls.master, so the real
# generator produces real stubs for both too.
"$CLANG" $SYSCALL_ASFLAGS "$TMPGEN/_geteuid.s" -o "$OUT/removefile/geteuid.o"
"$CLANG" $SYSCALL_ASFLAGS "$TMPGEN/_chflags.s" -o "$OUT/removefile/chflags.o"
# fchdir (#13), fsync (#95), fstatat64 (#470): real fts.c calls (via the
# public headers' __DARWIN_ALIAS/$INODE64 aliasing - see the fstatat64
# real stub aliased to the caller-visible fstatat$INODE64 symbol below),
# none NO_SYSCALL_STUB.
"$CLANG" $SYSCALL_ASFLAGS "$TMPGEN/_fchdir.s" -o "$OUT/removefile/fchdir.o"
"$CLANG" $SYSCALL_ASFLAGS "$TMPGEN/_fsync.s" -o "$OUT/removefile/fsync.o"
"$CLANG" $SYSCALL_ASFLAGS "$TMPGEN/_fstatat64.s" -o "$OUT/removefile/fstatat64.o"

# Real flags: same __LIBC__/DARWIN_UNIX03/etc set build_libc_gen.sh uses
# (fts.c/removefile.c both real Libc-adjacent Apple source expecting the
# same real preprocessor environment), plus -fblocks (real Block_copy
# call in fts.c's optional comparator) and the same real os_assumes/
# os_assert command-line macro fallback build_libclosure.sh established.
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -fblocks -Wno-nullability-completeness \
    -include libc_build_prototypes.h -include $OUT/compat_include/removefile_fcntl_compat.h \
    '-Dos_assumes(x)=(x)' '-Dos_assert(x)=((x)?(void)0:__builtin_trap())' \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/gen -I $LIBC/locale \
    -I $CLOSURE \
    -I $XNU/bsd \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I ../../third_party/libmalloc/private \
    -I ../../generated"

ok=0
skip=0

# Real Libc/gen/fts.c: this tier's genuinely new dependency, see header
# comment above.
if eval "$CLANG $CFLAGS \"$LIBC/gen/fts.c\" -o \"$OUT/removefile/fts.o\""; then
    ok=$((ok + 1))
else
    skip=$((skip + 1))
    echo "  skip: fts (removefile cannot work without it - see script output above)" >&2
fi
echo "fts: $ok compiled, $skip skipped"

REMOVEFILE_CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h -include $OUT/compat_include/removefile_fcntl_compat.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/locale -I $LIBC/locale/FreeBSD -I $LIBC/darwin \
    -I $LIBC/stdtime/FreeBSD \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $REMOVEFILE"

ok=0
skip=0
for f in "$REMOVEFILE"/removefile.c "$REMOVEFILE"/removefile_rename_unlink.c \
         "$REMOVEFILE"/removefile_sunlink.c "$REMOVEFILE"/removefile_tree_walker.c \
         "$REMOVEFILE"/removefile_random.c; do
    base=$(basename "$f" .c)
    out="$OUT/removefile/${base}.o"
    if "$CLANG" $REMOVEFILE_CFLAGS "$f" -o "$out" 2>"$OUT/removefile/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/removefile/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base" >&2
        cat "$OUT/removefile/${base}.err" >&2
    fi
done
echo "removefile: $ok compiled, $skip skipped"

# This tier's own raw-syscall shims (removefile_syscalls.c: unlink/rmdir/
# unlinkat + real getiopolicy_np - see that file's own header comment).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    removefile_syscalls.c -o "$OUT/removefile/removefile_syscalls.o"

# Real Apple rand()/srand() (Libc/stdlib/FreeBSD/rand.c) built on real
# random()/srandom() (Libc/stdlib/FreeBSD/random.c) - removefile_random.c's
# non-cryptographic buffer-filler path calls plain rand()/srand(), real
# Apple source for both, needs fbsdcompat's namespace.h/un-namespace.h
# (same real compat headers build_libc_string.sh's FreeBSD tier already
# established a use for).
FBSD_CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D_FORTIFY_SOURCE=0 \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat"
"$CLANG" $FBSD_CFLAGS "$LIBC/stdlib/FreeBSD/random.c" -o "$OUT/removefile/random.o"
"$CLANG" $FBSD_CFLAGS "$LIBC/stdlib/FreeBSD/rand.c" -o "$OUT/removefile/rand.o"

# Real qsort()/qsort_b() (fts.c's real comparator sort step): qsort.c is
# plain real FreeBSD source; qsort_b.c is real Apple source needing
# Block_private.h (real libclosure, -fblocks already established above).
"$CLANG" $FBSD_CFLAGS "$LIBC/stdlib/FreeBSD/qsort.c" -o "$OUT/removefile/qsort.o"
"$CLANG" $FBSD_CFLAGS -fblocks -I "$CLOSURE" "$LIBC/stdlib/qsort_b.c" -o "$OUT/removefile/qsort_b.o"
# qsort.c's real worst-case fallback (heapsort) and qsort_b.c's real
# underlying qsort_r - both real FreeBSD source, same tier.
"$CLANG" $FBSD_CFLAGS "$LIBC/stdlib/FreeBSD/heapsort.c" -o "$OUT/removefile/heapsort.o"
"$CLANG" $FBSD_CFLAGS "$LIBC/stdlib/FreeBSD/qsort_r.c" -o "$OUT/removefile/qsort_r.o"
"$CLANG" $FBSD_CFLAGS "$LIBC/stdlib/FreeBSD/heapsort_r.c" -o "$OUT/removefile/heapsort_r.o"

rm -f "$OUT/libremovefile.a"
ar rcs "$OUT/libremovefile.a" "$OUT"/removefile/*.o
echo "wrote $OUT/libremovefile.a"

# Real smoketest: builds a real 2-level directory tree with real files on
# the real HFS+ root, calls real removefile() with REMOVEFILE_RECURSIVE,
# verifies via real stat() that everything is actually gone.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -include libc_build_prototypes.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -I $LIBC/include -I $REMOVEFILE \
    removefile_smoketest.c -o "$OUT/removefile_smoketest.o"
# New real dependency as of build_libunwind.sh (later Phase 4 pass):
# libc_printf_mach_stub.c's pthread_rwlock_rdlock/unlock no-op stand-ins
# (linked in transitively via libc_printf.a above) were retired in favor
# of real libpthread.a - see that file's updated header comment and
# build_libunwind.sh's own header comment for the real sched_yield()
# dependency this drags in too.
if [ ! -f "$OUT/libpthread.a" ]; then
    echo "error: $OUT/libpthread.a not found -- run build_libpthread.sh first" >&2
    exit 1
fi
# Real gaps, found 2026-09-14 via a live build_everything.sh run (same
# real classes already fixed in build_cron.sh/build_libc_stdio.sh/
# build_libc_gen.sh, see those scripts' own header comments for the
# full citations):
# 1. _mmap undefined -- libpthread.a's real pthread_create() needs it;
#    mmap/munmap are NO_SYSCALL_STUB syscalls with no static provider
#    since libSystem.B.dylib took over for real. Confirmed via grep this
#    script has no existing real mmap()/munmap() provider of its own
#    (unlike crontab_extra_syscalls.o's case, where adding this fix
#    would have been a real duplicate).
# 2. ___gdtoa_locks undefined -- a real COMMON symbol in
#    libc_printf.a(glue.o) that a plain archive reference doesn't
#    reliably pull in; `ar x` it directly instead of -force_load
#    (which pulls in genuinely unwanted files elsewhere, confirmed in
#    build_cron.sh's own investigation).
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
"$CLANG" $MMAP_SYSCFLAGS "$MMAP_TMPGEN/___mmap.s" -o "$OUT/sys_mmap_for_removefile.o"
"$CLANG" $MMAP_SYSCFLAGS "$MMAP_TMPGEN/___munmap.s" -o "$OUT/sys_munmap_for_removefile.o"
rm -rf "$MMAP_TMPGEN"
trap - EXIT
mkdir -p "$OUT/removefile_glue_extract"
(cd "$OUT/removefile_glue_extract" && ar x "../libc_printf.a" glue.o)
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    libunwind_sched_yield.s -o "$OUT/removefile_sched_yield_trap.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libunwind_sched_yield.c -o "$OUT/removefile_sched_yield.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -o removefile_smoketest.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT/removefile_smoketest.o" "$OUT/libremovefile.a" \
    "$OUT/libclosure.a" \
    "$OUT/libc_printf.a" \
    "$OUT/removefile_glue_extract/glue.o" \
    "$OUT/libc_gen.a" "$OUT/libc_stdio.a" "$OUT/libc_locale_for_stdio.a" "$OUT/libc_gen.a" "$OUT/libc_stdio.a" \
    "$OUT/pthread_tsd_shim_libc_bootstrap.o" \
    "$OUT/libpthread.a" "$OUT/removefile_sched_yield_trap.o" "$OUT/removefile_sched_yield.o" \
    "$OUT/sys_mmap_for_removefile.o" "$OUT/sys_munmap_for_removefile.o" \
    "$OUT/libmalloc.a" "$OUT/cerror_stub.o" "$OUT/libplatform.a" \
    "$OUT/libunwind.a" \
    -Wl,-alias,___mmap,_mmap \
    -Wl,-alias,___munmap,_munmap \
    -Wl,-alias,'_pthread_rwlock_wrlock$UNIX2003',_pthread_rwlock_wrlock \
    -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003',_pthread_rwlock_rdlock \
    -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003',_pthread_rwlock_unlock \
    -Wl,-alias,_mprotect,'_mprotect$UNIX2003' \
    -Wl,-alias,___kill,'_kill$UNIX2003' \
    -Wl,-alias,___kill,_kill \
    -Wl,-alias,_write,'_write$UNIX2003' \
    -Wl,-alias,__mal_semwait_sleep,'_sleep$UNIX2003' \
    -Wl,-alias,___kdebug_trace,_kdebug_trace \
    -Wl,-alias,_open,'_open$UNIX2003' \
    -Wl,-alias,_close,'_close$UNIX2003' \
    -Wl,-alias,_closedir,'_closedir$UNIX2003' \
    -Wl,-alias,_opendir,'_opendir$INODE64$UNIX2003' \
    -Wl,-alias,_readdir,'_readdir$INODE64' \
    -Wl,-alias,_fcntl,'_fcntl$UNIX2003' \
    -Wl,-alias,_fstat,'_fstat$INODE64' \
    -Wl,-alias,_lstat,'_lstat$INODE64' \
    -Wl,-alias,_stat,'_stat$INODE64' \
    -Wl,-alias,_fstatfs,'_fstatfs$INODE64' \
    -Wl,-alias,_fstatat64,'_fstatat$INODE64' \
    -Wl,-alias,_fsync,'_fsync$UNIX2003' \
    -Wl,-alias,_read,'_read$UNIX2003' \
    -Wl,-alias,_reallocarray,'_reallocarray$DARWIN_EXTSN'
echo "wrote removefile_smoketest.macho"

otool -l removefile_smoketest.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l removefile_smoketest.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
if nm -u removefile_smoketest.macho | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u removefile_smoketest.macho >&2
fi
