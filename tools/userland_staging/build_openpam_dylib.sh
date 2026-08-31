#!/bin/bash
# Builds libc_build/system/libopenpam.dylib: static-to-dylib migration,
# docs/backlog.md item d8 -- BASE LIBRARY ONLY. Real, honestly-scoped
# exclusion, not attempted here: real per-module PAM dylibs (each PAM
# module is independently dlopen'd on real macOS -- this project has no
# dlopen at all, same documented architectural gap class as OpenPAM's
# own module loader elsewhere in this project's history). This script
# only converts the base `libopenpam.a` (openpam_*.c -- the
# configuration/chain/policy engine itself) to a real dylib; the
# module-loading question is a separate, deeper architectural gap this
# pass does not close.
#
# Real install_name: /usr/lib/libopenpam.dylib -- no real per-version
# infix confirmed from this checkout's own upstream build config (unlike
# libbsm/libncurses, no explicit `-install_name`/version macro found in
# third_party's own OpenPAM build files); using the plain, unversioned
# real macOS path this library actually ships at.
#
# Real, unmodified source: reuses every already-built member object out
# of libc_build/libopenpam.a (ar x, not recompiled -- see build_openpam.sh's
# own header for the full scope), plus real, cheap, already-compiled
# objects a real dylib link surfaces as genuine external references this
# archive's own narrower smoketest never needed (found via the same real
# nm-based intra-archive resolution pass build_libresolv_dylib.sh's
# header documents -- not guessed):
#   - geteuid.o (syslogd_libc_extras.a), pthread_getegid.o (libpthread.a):
#     real, cheap, already-verified-safe (only dep is _cerror_nocancel,
#     resolved via the libsystem_kernel.dylib sibling link below) -- same
#     precedent build_bsm_dylib.sh already established for geteuid.o.
#   - sys__setgroups.o (liblaunchd_syscalls.a): real setgroups(2)
#     wrapper, only dep _cerror_nocancel.
#   - __global_locale/__locale_key: SAME real gap + SAME real fix as
#     build_libresolv_dylib.sh's own pilot (libc_locale_globals_compat.c
#     + table.o/none.o, real local definitions -- `-Wl,-U` cannot defer
#     these, real arm64 adrp/lo12 direct data-address fixup).
#
# Real, honestly-classified `-Wl,-U` deferrals:
#   - alarm, NXArgv, setegid, seteuid, getgroups: real, confirmed
#     genuinely absent from every archive in this checkout (`nm -g` swept
#     across every .a in libc_build/ -- zero hits for any of these five)
#     -- not a resolv/bsm-specific gap, project-wide.
#   - __ldpart_free_extra/__part_load_locale: real LC_TIME loader
#     (locale/FreeBSD/ldpart.c) -- tried compiling it in for real first
#     (docs/backlog.md item d9 reports it compiling clean elsewhere), but
#     its own __part_load_locale calls a real, deeper helper
#     (__open_path_locale) that's genuinely absent from this checkout's
#     entire third_party/Libc (confirmed via `find`, not guessed) -- the
#     same deeper LC_TIME/locale-path-search wall d9's own writeup
#     already flags for a different command set. Reverted the ldpart.c
#     addition rather than chase that wall here; deferred instead.
#   - sigaction: real source for a raw wrapper exists
#     (Libc_sys_sigaction.o, syslogd_libc_extras.a) but it calls a real
#     `__platform_sigaction` (third_party/libplatform/src/setjmp/generic/
#     sigaction.c) not yet compiled into libsystem_platform.dylib --
#     closing this for real means editing that SHARED dylib's own build
#     script, which belongs to this migration's later Stage B, not this
#     single-library conversion. Deferred here instead of a partial,
#     broken pull-in.
#   - initgroups: real source DOES exist in this project
#     (launchd_passwd_compat.o, liblaunchd_syscalls.a) -- but it's a much
#     larger real dependency chain than the other additions above
#     (li_getpwuid/li_gr_at/li_gr_entry_count/li_gr_load/li_pw_load, a
#     real flat-file passwd/group database loader this project wrote for
#     launchd specifically), not a small, self-contained addition.
#     Deferred rather than pulled in wholesale for openpam's sake;
#     docs/backlog.md item d6's "no dedicated wrapper source anywhere in
#     this project's pinned repos" note is now stale for initgroups
#     specifically (a real, custom, project-local implementation DOES
#     exist) -- worth a real followup to reconcile, not attempted here.
#   - connect$UNIX2003/send$UNIX2003/writev$UNIX2003: same real,
#     project-wide BSD socket syscall gap build_libresolv_dylib.sh's own
#     header already documents (docs/backlog.md item d7's CFSocket.c
#     prerequisite) -- not resolv/openpam-specific.
set -e
cd "$(dirname "$0")"

OUT="libc_build"
EXTRACT="$OUT/libopenpam_dylib/extracted"
rm -rf "$EXTRACT"
mkdir -p "$EXTRACT"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
for a in libopenpam libc_locale syslogd_libc_extras liblaunchd_syscalls libpthread; do
    if [ ! -f "$OUT/$a.a" ]; then
        echo "error: $OUT/$a.a not found -- run build_${a}.sh first" >&2
        exit 1
    fi
done
for d in libsystem_c libsystem_malloc libsystem_platform libsystem_kernel libsystem_pthread; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2
        exit 1
    fi
done

# Real, verified fix (docs/backlog.md item d9): Xcode 12's own bundled
# ld64-609 has a real internal crash under -Wl,-fixup_chains that a much
# newer ld does not. Compiling still uses Xcode 12's clang/SDK above;
# only the final link step's linker binary changes, via clang's -B
# override.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi

(cd "$EXTRACT" && ar x "../../libopenpam.a")
(cd "$EXTRACT" && ar x "../../libc_locale.a" table.o none.o)
# See narrow_rune_locale.sh's own header (static-linking cleanup, Phase
# 1d, 2026-08-30): demotes the 4 public rune-locale globals table.o/
# none.o carry so they bind to libsystem_c.dylib (a sibling below)
# instead of this dylib defining a second, independently-mutable copy.
./narrow_rune_locale.sh "$EXTRACT"
(cd "$EXTRACT" && ar x "../../liblaunchd_syscalls.a" sys__setgroups.o)
# geteuid.o (syslogd_libc_extras.a) / pthread_getegid.o (libpthread.a)
# REMOVED 2026-08-30 (static-linking cleanup, dylib-vs-dylib duplicate-
# symbol pass), same fix + same real reasoning as build_bsm_dylib.sh's
# own geteuid.o/getuid.o removal: _geteuid/_getegid are both real
# libsystem_kernel.dylib exports as of 2026-08-29 (see that script's own
# "geteuid/getegid" comment), and this dylib already links
# libsystem_kernel.dylib as a sibling below -- libopenpam.a's own
# undefined references now resolve there instead of a second, private
# copy of each raw syscall trap.

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD -I $LIBC/gen \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/osfmk -I $XNU/libsyscall \
    -I ../../generated"
"$CLANG" $CFLAGS libc_locale_globals_compat.c -o "$EXTRACT/libc_locale_globals_compat.o"

mkdir -p "$OUT/system"
rm -f "$OUT/system/libopenpam.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/libopenpam.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-U,_alarm -Wl,-U,_NXArgv -Wl,-U,_setegid -Wl,-U,_seteuid -Wl,-U,_getgroups \
    -Wl,-U,_initgroups -Wl,-U,_sigaction \
    -Wl,-U,___ldpart_free_extra -Wl,-U,___part_load_locale \
    -Wl,-U,'_connect$UNIX2003' -Wl,-U,'_send$UNIX2003' -Wl,-U,'_writev$UNIX2003' \
    -o "$OUT/system/libopenpam.dylib" \
    "$EXTRACT"/*.o \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_pthread.dylib" \
    2>"$OUT/libopenpam_dylib_link.err" \
    && echo "wrote $OUT/system/libopenpam.dylib" \
    || { echo "libopenpam.dylib link found real undefined symbols:" >&2; grep '^  "' "$OUT/libopenpam_dylib_link.err" | sed 's/,.*//' | sort -u >&2; exit 1; }

otool -hv "$OUT/system/libopenpam.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
echo "declared -Wl,-U deferrals (expect: <flat-namespace> bind target):"
xcrun dyld_info -fixups "$OUT/system/libopenpam.dylib" 2>/dev/null | grep '<flat-namespace>' | awk '{print "  " $NF}'
UNEXPECTED=$(xcrun dyld_info -fixups "$OUT/system/libopenpam.dylib" 2>/dev/null | grep -v -E '<flat-namespace>|<weak-def-coalesce>' | grep 'bind' | grep -v -E 'libsystem_c/|libsystem_malloc/|libsystem_platform/|libsystem_kernel/|libsystem_pthread/' || true)
if [ -n "$UNEXPECTED" ]; then
    echo "error: real, unexpected non-sibling bind targets found:" >&2
    echo "$UNEXPECTED" >&2
    exit 1
fi
echo "libopenpam.dylib: every non-deferred external reference binds to a real, named sibling dylib -- no unexpected/dangling references"
