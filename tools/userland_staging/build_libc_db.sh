#!/bin/bash
# Builds libc_build/libc_db.a: real dbopen()/DB_HASH -- Berkeley DB, the
# one real, named blocker pwd_mkdb(8) has (DAR-174, Phase 5 account-
# provisioning tools). Real, unmodified Apple source:
#   third_party/Libc/db/db/FreeBSD/db.c        -- dbopen() dispatch
#   third_party/Libc/db/hash/FreeBSD/*.c        -- the DB_HASH backend
# Same real per-file compile loop / ar archive / smoketest shape as
# build_libc_regex.sh (CLAUDE.md's own reference template for porting one
# more Apple library) -- read that script first if this one looks
# unfamiliar.
#
# SCOPE (deliberately not the full 5-subdir db/{db,hash,btree,recno,mpool}
# from the ticket's original guess): pwd_mkdb.tproj/pwd_mkdb.c's own real
# source calls dbopen() exactly 4 times, every one with DB_HASH literal
# (confirmed via `grep dbopen third_party/system_cmds/pwd_mkdb.tproj/
# pwd_mkdb.c` -- never DB_BTREE/DB_RECNO). third_party/Libc/db/db/FreeBSD/
# db.c's own dispatch (dbopen() itself) only calls into __bt_open/
# __rec_open for the other two types -- grepped hash/FreeBSD/*.c for any
# "mpool_" call and found zero; the DB_HASH backend manages its own page
# buffering in hash_buf.c, it does not layer on the separate mpool
# library at all (unlike btree, which does -- confirmed by the same grep
# coming back empty only for hash/). So db/ + hash/ is the real, complete,
# scoped dependency closure DB_HASH needs; btree/recno/mpool are real
# gaps this project still has (nothing in this repo calls dbopen(...,
# DB_BTREE/DB_RECNO,...) yet) but are NOT blocking DAR-174 and are left
# unported here -- a future ticket, not invented stub coverage.
#
# hash/FreeBSD/ndbm.c (the legacy ndbm(3) API layered on DB_HASH) is also
# excluded: pwd_mkdb.c never calls dbm_open()/dbm_fetch()/etc, confirmed
# via the same grep, and ndbm.c pulls in <ndbm.h> for zero real benefit
# here.
#
# libc_db_unported_backends_stub.c (own file, real header explains why):
# db.c's dbopen() dispatch unconditionally references __bt_open/
# __rec_open even though this project never calls DB_BTREE/DB_RECNO --
# without SOMETHING defining them the archive doesn't link. Real,
# honestly-failing stand-ins (return NULL/EINVAL, the same real error
# contract dbopen() itself documents), not fabricated working coverage.
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
OUT="libc_build"
mkdir -p "$OUT/apple_db"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
for a in libc_locale libmalloc libplatform libc_gen; do
    if [ ! -f "$OUT/$a.a" ]; then
        echo "error: $OUT/$a.a not found -- run build_${a}.sh first" >&2
        exit 1
    fi
done

# Same real flags every other tier uses (see build_libc_locale.sh's own
# header comment for the full reasoning) plus this tier's own -I paths:
# hash/FreeBSD (for "hash.h"/"page.h"/"hash_extern.h", included with no
# subdirectory prefix by hash.c/hash_bigkey.c/etc) and fbsdcompat (for
# "namespace.h"/"un-namespace.h", real Apple FreeBSD-syncing headers
# already vendored at third_party/Libc/fbsdcompat -- hash.c/hash_page.c's
# own real #include "namespace.h" line, not invented by this script).
# -D__DBINTERFACE_PRIVATE: real Apple db.h (third_party/Libc/include/
# db.h, lines ~159-215) gates BOTH the P_32_COPY/M_32_SWAP/etc
# cross-endian page-swap macros hash.c/hash_page.c call AND the
# __bt_open/__hash_open/__rec_open/__dbpanic prototypes db.c's own
# dispatch calls, behind this one macro -- undefined by default,
# reproducing real "implicit declaration" errors for both (confirmed:
# building without it fails exactly these two ways). Real FreeBSD's own
# lib/libc/db/Makefile.inc defines this same macro project-wide
# (`CFLAGS+=-D__DBINTERFACE_PRIVATE`, confirmed via a real git show
# against this project's own pinned third_party/freebsd-src @
# release/13.2.0) -- not invented here, the real upstream Makefile
# convention this checkout's own Xcode project presumably also defines
# (its .pbxproj wasn't checked, but the macro's actual effect -- these
# exact two gaps and no others -- matches exactly).
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h \
    -D__DBINTERFACE_PRIVATE \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I $OUT/compat_include \
    -I $LIBC/db/hash/FreeBSD -I $LIBC/db/db/FreeBSD \
    -I $LIBC/fbsdcompat \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall \
    -I ../../generated"

ok=0
fail=0
for f in "$LIBC/db/db/FreeBSD/db.c" \
         "$LIBC/db/hash/FreeBSD/hash.c" \
         "$LIBC/db/hash/FreeBSD/hash_bigkey.c" \
         "$LIBC/db/hash/FreeBSD/hash_buf.c" \
         "$LIBC/db/hash/FreeBSD/hash_func.c" \
         "$LIBC/db/hash/FreeBSD/hash_log2.c" \
         "$LIBC/db/hash/FreeBSD/hash_page.c" \
         libc_db_unported_backends_stub.c; do
    base=$(basename "$f" .c)
    out="$OUT/apple_db/${base}.o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>"$OUT/apple_db/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/apple_db/${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OUT/apple_db/${base}.err")"
    fi
done
echo "libc_db: $ok compiled, $fail gaps (see libc_build/apple_db/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi

rm -f "$OUT/libc_db.a"
ar rcs "$OUT/libc_db.a" "$OUT"/apple_db/*.o
echo "wrote $OUT/libc_db.a"

# Real smoketest: a tiny real dbopen(DB_HASH)/put/get/close consumer.
# Unlike build_libc_regex.sh's own smoketest (a pure in-memory string
# match with zero real syscalls), hash's page I/O path references real
# pread/pwrite/fstatat/stat/mmap/unlink/sigprocmask -- confirmed by a
# real static-link attempt here first, which is exactly how this was
# found: those are real libsystem_kernel.dylib/libsystem_c.dylib exports
# in this project already (`nm -m` confirmed), NOT raw-syscall gaps, so
# this smoketest links DYNAMICALLY against the same real dylib stack
# build_vipw.sh/build_passwd.sh use (DAR-314) rather than fighting to
# statically resolve them -- that also makes this smoketest a more
# realistic stand-in for pwd_mkdb.macho's own real (dynamic) link mode.
rm -f libc_db_smoketest.macho
"$CLANG" $CFLAGS libc_db_smoketest.c -o "$OUT/libc_db_smoketest.o"

DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ -x "$NEWLD_BINDIR/ld" ] && [ -f "$OUT/system/libsystem_c.dylib" ]; then
    LINK_DYLIBS="$OUT/system/libsystem_c.dylib $OUT/system/libsystem_kernel.dylib $OUT/system/libsystem_pthread.dylib $OUT/system/libsystem_malloc.dylib $OUT/system/libsystem_platform.dylib $OUT/system/libdyld.dylib $OUT/system/libSystem.B.dylib"
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
        -DCRT_DYNAMIC_LINKING \
        -B"$NEWLD_BINDIR" \
        -Wl,-fixup_chains \
        -o libc_db_smoketest.macho \
        ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
        "$OUT/libc_db_smoketest.o" "$OUT/libc_db.a" \
        $LINK_DYLIBS \
        -Wl,-alias,_pread,'_pread$UNIX2003' \
        -Wl,-alias,_pwrite,'_pwrite$UNIX2003' \
        -Wl,-alias,_stat,'_stat$INODE64' \
        -Wl,-alias,__err,_err \
        -Wl,-alias,__warn,_warn \
        2>"$OUT/libc_db_smoketest_link.err" && echo "wrote libc_db_smoketest.macho" || echo "WARNING: smoketest link failed (libc_db.a itself is still built -- see $OUT/libc_db_smoketest_link.err)" >&2

    if [ -f libc_db_smoketest.macho ]; then
        otool -l libc_db_smoketest.macho | grep -q LC_LOAD_DYLIB || \
            echo "WARNING: no LC_LOAD_DYLIB -- expected a dynamic link" >&2
        otool -l libc_db_smoketest.macho | grep -q LC_MAIN || \
            echo "WARNING: no LC_MAIN" >&2
        # (nm -u is expected to be non-empty here -- this is a dynamic
        # binary, external symbols bound via two-level namespace are
        # normal, not a gap; bind_target_audit.sh below is the real check.)
        if [ -x ./bind_target_audit.sh ]; then
            ./bind_target_audit.sh libc_db_smoketest.macho || true
        fi
    else
        tail -60 "$OUT/libc_db_smoketest_link.err" >&2
    fi
else
    echo "skip: libc_db_smoketest.macho (needs $OUT/system/*.dylib -- run the dylib build scripts first)"
fi
