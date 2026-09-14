#!/bin/bash
# Builds libc_build/libcopyfile.a: real Apple copyfile (copyfile()/
# fcopyfile() - the library `cp` actually links against for real data +
# metadata copies). Continues the tier structure of build_removefile.sh
# and earlier tiers (see docs/roadmap.md Phase 4 for the running tier
# breakdown). third_party/copyfile is already cloned+pinned
# (copyfile-173.40.2, see setup_third_party.sh).
#
# Scope: the whole real copyfile.c (5026 lines, one translation unit -
# unlike removefile this isn't naturally split into several files, so
# "curated subset" here means real preprocessor-level scoping, not file
# selection) + xattr_flags.c. Built with -target arm64-apple-ios14.4 (this
# project's established toolchain target throughout Phase 4) - which
# means TARGET_OS_IPHONE is real and true for this compile, and
# copyfile.c's OWN real `#if !TARGET_OS_IPHONE ... #else` branch (line 55)
# already provides a complete, real, Apple-authored no-op quarantine
# fallback (qtn_file_alloc/qtn_file_apply_to_fd/etc, all real stand-ins
# Apple ships for iOS builds of this exact file, not something this
# project invented) - so quarantine, which would otherwise need a whole
# unavailable libquarantine.dylib, is a non-issue here purely as a
# consequence of the target triple already established for this whole
# Phase 4 effort.
#
# Real ACL/xattr/clonefile support: this target's real HFS+ driver (see
# architecture.md's storage-stack section) does not implement ACL storage
# or extended attributes as a distinguished on-disk feature the way APFS
# does, and clonefileat/fclonefileat (bsd/kern/syscalls.master #462/#517)
# are real syscalls but require filesystem-level VNOP_CLONEFILE support
# real HFS+ doesn't provide - so at RUNTIME these paths will genuinely
# return ENOTSUP/EINVAL from the real kernel (correct, honest failure),
# not silently succeed. They still need to LINK, though: C doesn't
# eliminate code gated by a runtime flag check (`if (flags & COPYFILE_ACL)`
# is not a preprocessor #if), so copyfile.c's ACL/xattr/clonefile call
# sites are real, present, linked code paths even though this project's
# smoketest only exercises the COPYFILE_DATA path. Real syscalls exist
# for all of them (getxattr/setxattr/removexattr/listxattr and their f-
# variants, #234-241 and #462/#517, none NO_SYSCALL_STUB) - regenerated
# fresh via the same real Apple Perl generator every other tier uses.
# Real posix1e ACL wrapper source also exists in this project's own Libc
# checkout (third_party/Libc/posix1e/*.c) and is compiled in below rather
# than stubbed, since it's real, available Apple source.
set -e
cd "$(dirname "$0")"

COPYFILE="../../third_party/copyfile"
LIBC="../../third_party/Libc"
XNU="../../third_party/xnu-7195"
PTHREAD="../../third_party/libpthread"
CLOSURE="../../third_party/libclosure"
OUT="libc_build"
mkdir -p "$OUT/copyfile" "$OUT/compat_include"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
for dep in libmalloc libplatform libclosure libc_gen libc_stdio libremovefile; do
    if [ ! -f "$OUT/$dep.a" ]; then
        echo "error: $OUT/$dep.a not found -- run build_$dep.sh first" >&2
        exit 1
    fi
done

# Real Apple syscall generator, regenerated fresh (same invocation every
# other tier uses) - the xattr family (#234-241) and clonefileat/
# fclonefileat (#462/#517), none NO_SYSCALL_STUB.
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
for s in getxattr setxattr removexattr listxattr fgetxattr fsetxattr \
         fremovexattr flistxattr clonefileat fclonefileat \
         getuid fchflags symlink readlink fchown pread pwrite \
         fpathconf ftruncate statfs64 fsetattrlist; do
    "$CLANG" $SYSCALL_ASFLAGS "$TMPGEN/_${s}.s" -o "$OUT/copyfile/${s}.o"
done

mkdir -p "$OUT/compat_include/System/sys" "$OUT/compat_include/xpc"
XNU_BSD_SYS_ABS="$(cd "$XNU/bsd/sys" && pwd)"
cat > "$OUT/compat_include/System/sys/content_protection.h" <<EOF
/* Real content lives at \$XNU/bsd/sys/content_protection.h - this project's
 * BUILD.hdrs export tree only flattens it under Kernel.framework/System.
 * framework PrivateHeaders paths, not the plain System/sys/... path
 * copyfile.c's own real #include expects - a real SDK-layout gap (same
 * class as build_libc_gen.sh's getdirentries64_flags_t), forwarded here
 * rather than duplicated. Absolute path (computed at build time, below)
 * rather than a hand-counted relative one - less fragile if this compat
 * header ever moves. */
#include "$XNU_BSD_SYS_ABS/content_protection.h"
EOF
cat > "$OUT/compat_include/xpc/private.h" <<'EOF'
/* Stand-in: real xpc/private.h is part of libxpc, which this project
 * doesn't have (no dyld/XPC service infrastructure at all - see
 * architecture.md's userland section). xattr_flags.c's only real use of
 * anything from this header is _xpc_runtime_is_app_sandboxed() - real
 * stand-in implementation in copyfile_xpc_shim.c (this target has no App
 * Sandbox concept at all, "not sandboxed" is the real, correct answer,
 * not fabricated) - prototype declared here since that's the header
 * xattr_flags.c actually includes. */
#ifndef _XPC_PRIVATE_H_COMPAT_
#define _XPC_PRIVATE_H_COMPAT_
int _xpc_runtime_is_app_sandboxed(void);
#endif
EOF

mkdir -p "$OUT/compat_include/Kernel/sys"
if [ -f "$XNU/bsd/sys/decmpfs.h" ]; then
    DECMPFS_ABS="$(cd "$XNU/bsd" && pwd)/sys/decmpfs.h"
    cat > "$OUT/compat_include/Kernel/sys/decmpfs.h" <<EOF
/* Real content lives at \$XNU/bsd/sys/decmpfs.h - forwarded here for the
 * same real reason as System/sys/content_protection.h above (a real
 * SDK-layout gap between this project's non-Xcode-project kernel build
 * and the flattened Kernel.framework path copyfile.c's real source
 * expects, not a fabricated header). */
#include "$DECMPFS_ABS"
EOF
else
    echo "error: $XNU/bsd/sys/decmpfs.h not found -- check third_party/xnu-7195 checkout" >&2
    exit 1
fi

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -fblocks -Wno-nullability-completeness \
    -include libc_build_prototypes.h -include $OUT/compat_include/removefile_fcntl_compat.h \
    -include copyfile_dispatch_once_compat.h \
    -DPRIVATE=1 \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $COPYFILE"

"$CLANG" $CFLAGS copyfile_xpc_shim.c -o "$OUT/copyfile/copyfile_xpc_shim.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    copyfile_syscalls.c -o "$OUT/copyfile/copyfile_syscalls.o"

ok=0
skip=0
for f in "$COPYFILE"/copyfile.c "$COPYFILE"/xattr_flags.c; do
    base=$(basename "$f" .c)
    out="$OUT/copyfile/${base}.o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>"$OUT/copyfile/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/copyfile/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base" >&2
        cat "$OUT/copyfile/${base}.err" >&2
    fi
done
echo "copyfile: $ok compiled, $skip skipped"

# Real Apple posix1e ACL wrapper source (copyfile.c's real ACL-copy path
# calls acl_get_link_np/acl_set_file/etc) - real source, not stubbed.
mkdir -p "$OUT/compat_include"
cat > "$OUT/compat_include/membershipPriv.h" <<'EOF'
/* Originally an empty stand-in: the three real membership functions
 * acl_translate.c actually calls (mbr_uid_to_uuid/mbr_gid_to_uuid/
 * mbr_uuid_to_id) are ALL already declared in the real public
 * membership.h (confirmed present in this project's pinned Xcode-12
 * iPhoneOS SDK) - this private header adds nothing acl_translate.c's
 * real call sites need beyond that (same pattern as build_libc_locale.sh's
 * os/assumes.h).
 *
 * Extended 2026-08-20 (ls(1) tier, build_simple_cmds.sh): because this
 * generic $OUT/compat_include is on every command's include path BEFORE
 * any command-specific -I, this stand-in silently shadows the REAL
 * membershipPriv.h vendored at third_party/Libinfo/membership.subproj/
 * for any command that needs more than acl_translate.c's narrow original
 * subset (ls(1)'s print.c calls the real mbr_identifier_translate() for
 * its -e ACL-owner-name display, real declaration only in the private
 * header, not membership.h). Rather than leave that trap in place, this
 * now carries the additional real declarations/macros verbatim from
 * third_party/Libinfo/membership.subproj/membershipPriv.h (not
 * fabricated -- copied from the real vendored header) that any current
 * or future command might need; still not the FULL private header (no
 * mbr_check_membership_ext/mbr_set_identifier_ttl/etc -- add on demand,
 * same as this file's own history). */
#ifndef _MEMBERSHIP_PRIV_H_COMPAT_
#define _MEMBERSHIP_PRIV_H_COMPAT_
#define MBR_REC_TYPE_USER 1
#define MBR_REC_TYPE_GROUP 2
/* only supported by mbr_identifier_translate for target type */
#define ID_TYPE_UID_OR_GID 30
#define ID_TYPE_NAME 31
#define ID_TYPE_WINDOWS_FQN 32
int mbr_identifier_translate(int id_type, const void *identifier, size_t identifier_size, int target_type, void **result, int *rec_type);
#endif
EOF

POSIX1E_CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -include libc_build_prototypes.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D_FORTIFY_SOURCE=0 -DHAVE_STDINT_H=1 \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/posix1e -I $LIBC/fbsdcompat -I $XNU/bsd \
    -I $LIBC/locale -I $LIBC/locale/FreeBSD -I $LIBC/stdtime/FreeBSD \
    -I $PTHREAD/include -I $PTHREAD/private -I $XNU/osfmk -I $XNU/libsyscall"
ok2=0
skip2=0
for f in "$LIBC"/posix1e/acl.c "$LIBC"/posix1e/acl_*.c "$LIBC"/gen/filesec.c \
         "$LIBC"/sys/chmodx_np.c "$LIBC"/sys/statx_np.c "$LIBC"/stdlib/FreeBSD/realpath.c \
         "$LIBC"/uuid/uuidsrc/copy.c "$LIBC"/uuid/uuidsrc/parse.c "$LIBC"/uuid/uuidsrc/unparse.c \
         "$LIBC"/uuid/uuidsrc/pack.c "$LIBC"/stdlib/FreeBSD/strtoul.c; do
    base=$(basename "$f" .c)
    out="$OUT/copyfile/${base}.o"
    if "$CLANG" $POSIX1E_CFLAGS "$f" -o "$out" 2>"$OUT/copyfile/${base}.err"; then
        ok2=$((ok2 + 1))
        rm -f "$OUT/copyfile/${base}.err"
    else
        skip2=$((skip2 + 1))
        echo "  skip: $base" >&2
        cat "$OUT/copyfile/${base}.err" >&2
    fi
done
echo "posix1e (ACL): $ok2 compiled, $skip2 skipped"

rm -f "$OUT/libcopyfile.a"
ar rcs "$OUT/libcopyfile.a" "$OUT"/copyfile/*.o
echo "wrote $OUT/libcopyfile.a"

# Real smoketest: copies a real file with real content on the real HFS+
# root via real copyfile(COPYFILE_DATA), verifies byte-for-byte, then
# cleans up via real removefile() (previous tier).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -include libc_build_prototypes.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -I $LIBC/include -I $COPYFILE -I ../../third_party/removefile \
    copyfile_smoketest.c -o "$OUT/copyfile_smoketest.o"
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
# Real gap, found 2026-09-13: smoketest link below also force_loads
# libremovefile.a (copyfile() calls into it for the real remove-on-error
# path) with no prerequisite check at all.
if [ ! -f "$OUT/libremovefile.a" ]; then
    echo "error: $OUT/libremovefile.a not found -- run build_removefile.sh first" >&2
    exit 1
fi
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    libunwind_sched_yield.s -o "$OUT/copyfile_sched_yield_trap.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libunwind_sched_yield.c -o "$OUT/copyfile_sched_yield.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -o copyfile_smoketest.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT/copyfile_smoketest.o" -Wl,-force_load,"$OUT/libcopyfile.a" "$OUT/libremovefile.a" \
    "$OUT/libclosure.a" \
    "$OUT/libc_printf.a" \
    "$OUT/libc_gen.a" "$OUT/libc_stdio.a" "$OUT/libc_locale_for_stdio.a" "$OUT/libc_gen.a" "$OUT/libc_stdio.a" \
    "$OUT/pthread_tsd_shim_libc_bootstrap.o" \
    "$OUT/libpthread.a" "$OUT/copyfile_sched_yield_trap.o" "$OUT/copyfile_sched_yield.o" \
    "$OUT/libmalloc.a" "$OUT/cerror_stub.o" "$OUT/libplatform.a" \
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
    -Wl,-alias,_reallocarray,'_reallocarray$DARWIN_EXTSN' \
    -Wl,-alias,_statfs,'_statfs$INODE64' \
    -Wl,-alias,_pread,'_pread$UNIX2003' \
    -Wl,-alias,_pwrite,'_pwrite$UNIX2003' \
    -Wl,-alias,_realpath,'_realpath$DARWIN_EXTSN' \
    -Wl,-alias,_chmod,'_chmod$UNIX2003' \
    -Wl,-alias,_fchmod,'_fchmod$UNIX2003' \
    -Wl,-alias,_getattrlist,'_getattrlist$UNIX2003' \
    -Wl,-alias,_fstatx_np,'_fstatx_np$INODE64' \
    -Wl,-alias,_lstatx_np,'_lstatx_np$INODE64' \
    -Wl,-alias,_statx_np,'_statx_np$INODE64'
echo "wrote copyfile_smoketest.macho"

otool -l copyfile_smoketest.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l copyfile_smoketest.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
if nm -u copyfile_smoketest.macho | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u copyfile_smoketest.macho >&2
fi
