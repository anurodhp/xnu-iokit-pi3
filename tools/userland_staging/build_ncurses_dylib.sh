#!/bin/bash
# Builds libc_build/system/libncurses.dylib: static-to-dylib migration,
# docs/backlog.md item d8 -- confirmed real, unmodified ncurses source
# builds a versioned, infix-style dylib (`libncurses.<ver>.dylib` --
# third_party/ncurses/ncurses/aclocal.m4:4170's own comment: "version
# numbers are infix (ex: libncurses.<ver>.dylib)"). Real install_name:
# /usr/lib/libncurses.5.4.dylib, matching every real macOS system's own
# actual shipped path for this library -- this checkout's own
# dist.mk reports upstream version 5.7 (NCURSES_MAJOR/MINOR), which is
# NOT the same number as Apple's own dylib compatibility-version scheme
# for this library (Apple's own numbering is chosen independently of
# upstream's release version); noted here rather than silently
# presenting the upstream 5.7 as if it were verified from this
# checkout -- it isn't, this is the one real value in this script not
# directly re-derivable from the pinned source tree itself.
#
# Real, unmodified source: reuses every already-built member object out
# of libc_build/libncurses.a (ar x, not recompiled -- see build_ncurses.sh's
# own header for the full scope), plus real fixes for two genuine gaps a
# real dylib link surfaces that the archive's own narrower smoketest
# never needed:
#   - __global_locale/__locale_key: SAME real gap + SAME real fix as
#     build_libresolv_dylib.sh's own pilot (see that script's header for
#     the full citation) -- `-Wl,-U` cannot defer these (real arm64
#     adrp/lo12 direct data-address fixup, confirmed there already), so
#     this dylib also compiles libc_locale_globals_compat.c plus
#     table.o/none.o (extracted from libc_locale.a) as real local
#     definitions, verbatim, not re-derived.
#   - g_mach_ipc_clock_port/g_mach_ipc_clock_sem/mach_ipc_clock_init:
#     real, already-existing source (mach_ipc_clock_init.c, already
#     compiled by build_libmach_ipc.sh) -- ncurses' own napms()-family
#     delay support goes through this project's real
#     simple_cmds_nanosleep_pub_compat.o (already part of libncurses.a),
#     which needs these two real globals + the init function as real
#     local definitions (same "must be local, not -Wl,-U" reasoning as
#     the locale globals above -- these are also directly-addressed data
#     symbols). mach_ipc_clock_init.o extracted from the already-built
#     libmach_ipc.a, not recompiled.
#
# Real, honestly-classified `-Wl,-U` deferrals: clock_get_time,
# mach_host_self, host_get_clock_service, semaphore_create -- real Mach
# trap/MIG client routines (function calls, not data symbols, so
# `-Wl,-U` is valid here unlike the two data-symbol cases above),
# confirmed genuinely absent from every sibling dylib below via
# `nm -gU` (found via a real link attempt, not guessed preemptively --
# the first three are mach_ipc_clock_init.c's own real setup path for
# the two data globals above; without them, that init function would
# just no-op/return early at runtime, same "no real clock service
# reachable" tolerance as the rest of this file's own real code).
# simple_cmds_nanosleep_pub_compat.c's own real fallback path already
# handles a failed clock_get_time call, so this is a real, already-
# tolerated gap, not a new one.
set -e
cd "$(dirname "$0")"

OUT="libc_build"
EXTRACT="$OUT/libncurses_dylib/extracted"
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
if [ ! -f "$OUT/libncurses.a" ]; then
    echo "error: $OUT/libncurses.a not found -- run build_ncurses.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/libc_locale.a" ]; then
    echo "error: $OUT/libc_locale.a not found -- run build_libc_locale.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/libmach_ipc.a" ]; then
    echo "error: $OUT/libmach_ipc.a not found -- run build_libmach_ipc.sh first" >&2
    exit 1
fi
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

(cd "$EXTRACT" && ar x "../../libncurses.a")
# ______third_party_Libc_stdlib_FreeBSD_{atoi,abs}_c.o REMOVED entirely
# 2026-08-31 (static-linking cleanup, dylib-vs-dylib duplicate-symbol
# pass): these are real, unmodified Libc/stdlib/FreeBSD/{atoi,abs}.c,
# each defining ONLY `_atoi`/`_atoi_l` and `_abs` respectively (confirmed
# via nm -- no other symbols) -- full, real duplicates of
# libsystem_c.dylib's own real copies of the same source. Every other
# real consumer inside libncurses.a (lib_setup.o/lib_termcap.o for
# `_atoi`, hashmap.o/lib_mvcur.o for `_abs`) already holds an UNDEFINED
# reference to these names (confirmed via nm), so removing the two
# defining objects just makes those real references resolve externally
# to the already-linked libsystem_c.dylib sibling below instead of a
# second, private copy -- not narrowed, since nothing else in this
# archive independently defines them.
rm -f "$EXTRACT"/______third_party_Libc_stdlib_FreeBSD_atoi_c.o \
      "$EXTRACT"/______third_party_Libc_stdlib_FreeBSD_abs_c.o
# Libc_sys_sigaction.o / libplatform_sigaction.o REMOVED entirely
# 2026-09-02 (DAR-194, dylib-vs-dylib duplicate-symbol regression from
# DAR-164's fix in fdc375d). fdc375d gave `_sigaction`/
# `__sigaction_nobind`/`___platform_sigaction` a real canonical home in
# libsystem_c.dylib and `___sigaction`/`__sigtramp` a real canonical
# home in libsystem_kernel.dylib -- both already linked siblings below
# -- but left this dylib's own pre-existing, independently-compiled
# copies of the same real sources in place, so 5 symbols shipped with
# two live definitions each (dup_symbol_audit.sh FAIL).
# (DAR-405, 2026-09-27: ___platform_sigaction and the trampoline have
# since moved to libsystem_platform.dylib; libsystem_c keeps _sigaction.)
#
# Removal (not narrowing) is correct for these two specific objects:
# confirmed via `nm -m` that they define NOTHING else at all --
# Libc_sys_sigaction.o defines exactly `_sigaction` + `__sigaction_nobind`
# (and references `___platform_sigaction`), libplatform_sigaction.o
# defines exactly `___platform_sigaction` (and references `___sigaction`
# + `__sigtramp`). The entire real wrapper chain sigaction() ->
# __platform_sigaction() -> __sigaction()/__sigtramp is self-contained
# inside these two objects plus ncurses_extra_syscalls.o (narrowed
# below), and this archive's ONE real consumer is lib_tstp.o's genuine
# undefined reference to plain `_sigaction` -- which now resolves
# externally to the real libsystem_c.dylib sibling, same real Apple
# source, same shape as the atoi/abs removal just above.
rm -f "$EXTRACT/Libc_sys_sigaction.o" "$EXTRACT/libplatform_sigaction.o"
# ncurses_extra_syscalls.o narrowed 2026-08-30 (static-linking cleanup,
# dylib-vs-dylib duplicate-symbol pass): this project-authored compat
# file's own `_gettimeofday`/`_pthread_testcancel`/`_read`/
# `_sigprocmask` all predate this dylib linking real libsystem_c.dylib/
# libsystem_pthread.dylib as siblings (both already below on the final
# link line) -- all four are real, fully-external duplicates of those
# siblings' own real exports (confirmed via nm -m). Other real members
# of libncurses.a (lib_getch.o/lib_mouse.o/read_entry.o/lib_tstp.o/
# lib_twait.o/tty_update.o) hold real, undefined references to these
# same names; narrowing just demotes ncurses_extra_syscalls.o's
# definitions to local so those OTHER objects' references resolve
# externally to the real siblings instead, same
# `ld -r -keep_private_externs -unexported_symbol` technique as
# narrow_rune_locale.sh. `_select` (also in this object) was previously
# left exported here on purpose ("this dylib does not currently link
# either sibling [libresolv/libxpc]") -- no longer true as of 2026-08-31
# (static-linking cleanup, BSD socket family pass): `_select` now has a
# real, canonical home in libsystem_kernel.dylib (build_libsystem_
# kernel.sh's own "BSD socket family" comment), and this dylib already
# links libsystem_kernel.dylib as a sibling (it always did -- the
# earlier deferral was about libresolv/libxpc specifically, which are
# moot now that kernel owns it). Narrowed alongside the other four for
# the same reason and via the same technique.
#
# `_access`/`_mkdir`/`_strstr` (also in this same object) added to this
# narrowing list 2026-08-31 (static-linking cleanup continuation):
# same real-duplicate shape, confirmed real single canonical owner
# libsystem_c.dylib (already a linked sibling below) plus real, other,
# already-undefined consumers inside this archive (access.o/
# write_entry.o/lib_mouse.o+lib_mvcur.o+lib_setup.o+trim_sgr0.o) via
# nm. `_fpathconf`/`_sleep`/`___darwin_check_fd_set_overflow` (also in
# this object) NARROWED here 2026-08-31 (static-linking cleanup, "needs
# a new home" batch): each now has a real single canonical owner --
# `_fpathconf` -> libsystem_kernel.dylib (new real generated syscall
# stub, build_libsystem_kernel.sh), `_sleep` -> libsystem_c.dylib (new
# libsystem_c_sleep_compat.c, see that file's own header), `___darwin_
# check_fd_set_overflow` -> libsystem_c.dylib (extracted from the same
# libxpc_syscalls2.a member build_libsystem_c.sh already pulls the rest
# of the real `_FORTIFY_SOURCE` `_chk` tier from) -- this dylib already
# links both siblings below. Confirmed via nm -m this object's own
# `getpgrp()`/`read()`/`select()` neighbors are the only other real
# consumers inside it and none of them reference these three, so
# narrowing is a pure export-visibility change, not a functional one.
#
# `___sigaction`/`__sigtramp` (also in this same object) added to this
# narrowing list 2026-09-02 (DAR-194): real canonical owner is
# libsystem_kernel.dylib (fdc375d, already a linked sibling below).
# NARROWED rather than removed because this object also holds the real
# getpgrp()/read()/select()/etc. neighbors above; after
# libplatform_sigaction.o's removal (see its comment near the top of
# this script) nothing left in this archive references either name at
# all -- confirmed via `nm -m` across every extracted object -- so this
# is a pure export-visibility change with no remaining in-dylib caller.
XCODE12_LD="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
"$XCODE12_LD" -r -arch arm64 -keep_private_externs \
    -unexported_symbol _gettimeofday -unexported_symbol _pthread_testcancel \
    -unexported_symbol _read -unexported_symbol _sigprocmask -unexported_symbol _select \
    -unexported_symbol _access -unexported_symbol _mkdir -unexported_symbol _strstr \
    -unexported_symbol _fpathconf -unexported_symbol _sleep \
    -unexported_symbol ___sigaction -unexported_symbol __sigtramp \
    -unexported_symbol ___darwin_check_fd_set_overflow \
    "$EXTRACT/ncurses_extra_syscalls.o" -o "$EXTRACT/ncurses_extra_syscalls.o.narrowed"
mv -f "$EXTRACT/ncurses_extra_syscalls.o.narrowed" "$EXTRACT/ncurses_extra_syscalls.o"
(cd "$EXTRACT" && ar x "../../libc_locale.a" table.o none.o)
# See narrow_rune_locale.sh's own header (static-linking cleanup, Phase
# 1d, 2026-08-30): demotes the 4 public rune-locale globals table.o/
# none.o carry so they bind to libsystem_c.dylib (a sibling below)
# instead of this dylib defining a second, independently-mutable copy.
./narrow_rune_locale.sh "$EXTRACT"
(cd "$EXTRACT" && ar x "../../libmach_ipc.a" mach_ipc_clock_init.o)
# Real duplicate-STATE bug found 2026-08-30 (static-linking cleanup,
# dylib-vs-dylib duplicate-symbol pass), same shape as mach_task_self_/
# g_zone: `_g_mach_ipc_clock_port`/`_g_mach_ipc_clock_sem` are real,
# plain (__DATA,__common) globals caching a live Mach clock-service port
# right and semaphore -- build_libsystem_kernel.sh's own comment already
# documents this exact pair as duplicate state when it moved
# libmach_ipc.a's whole MIG runtime there, but this dylib independently
# extracts its OWN copy of the same mach_ipc_clock_init.o member, so a
# process with both libncurses.dylib and libsystem_kernel.dylib loaded
# had two live clock ports/semaphores. This dylib already links
# libsystem_kernel.dylib as a sibling below, which owns the real
# canonical copy.
#
# NOT simply removed: the header comment above already established
# (empirically, via a real ld64 failure) that `simple_cmds_nanosleep_
# pub_compat.o`'s references to these are direct-addressed data
# symbols that need a real LOCAL definition present in this dylib's own
# link -- `-Wl,-U` alone fails here the same way it does for the
# rune-locale globals. Same fix as narrow_rune_locale.sh instead: keep
# the real local definition (still satisfies the direct-address
# relocations), but demote its externally-visible names via
# `ld -r -keep_private_externs -unexported_symbol` so this dylib stops
# ALSO being a second public exporter. Confirmed via nm -m that
# `simple_cmds_nanosleep_pub_compat_c.o` (a real, separate member of
# libncurses.a extracted above, this dylib's only real consumer) holds
# genuine undefined references to all three names -- narrowing this
# object's own definitions means THAT object's references now resolve
# externally, at this dylib's own link time, to the real
# libsystem_kernel.dylib sibling instead of the local copy.
XCODE12_LD="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
"$XCODE12_LD" -r -arch arm64 -keep_private_externs \
    -unexported_symbol _g_mach_ipc_clock_port -unexported_symbol _g_mach_ipc_clock_sem \
    -unexported_symbol _mach_ipc_clock_init \
    "$EXTRACT/mach_ipc_clock_init.o" -o "$EXTRACT/mach_ipc_clock_init.o.narrowed"
mv -f "$EXTRACT/mach_ipc_clock_init.o.narrowed" "$EXTRACT/mach_ipc_clock_init.o"

# _nanosleep (from simple_cmds_nanosleep_pub_compat_c.o, extracted above
# via `ar x ../../libncurses.a`) NARROWED 2026-08-31 (static-linking
# cleanup, "needs a new home" batch): this object's own public
# `nanosleep()` now has a real single canonical owner,
# libsystem_kernel.dylib (build_libsystem_kernel.sh's own new
# nanosleep_compat.o, compiled from this exact same real source) -- this
# dylib already links libsystem_kernel.dylib as a sibling below. NOT
# removed outright the same way libc++.dylib's copy was (that dylib
# compiled it as a standalone step with no other in-archive consumer):
# confirmed via nm -m that `lib_napms.o` (this archive's real, only
# consumer, napms()'s own delay support) holds a genuine undefined
# reference to plain `_nanosleep` -- same "keep the local definition for
# an internal consumer, hide the public export" shape as
# mach_ipc_clock_init.o's own narrowing just above, not a full removal.
XCODE12_LD="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
"$XCODE12_LD" -r -arch arm64 -keep_private_externs \
    -unexported_symbol _nanosleep \
    "$EXTRACT/simple_cmds_nanosleep_pub_compat_c.o" -o "$EXTRACT/simple_cmds_nanosleep_pub_compat_c.o.narrowed"
mv -f "$EXTRACT/simple_cmds_nanosleep_pub_compat_c.o.narrowed" "$EXTRACT/simple_cmds_nanosleep_pub_compat_c.o"

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/osfmk -I $XNU/libsyscall \
    -I ../../generated"
"$CLANG" $CFLAGS libc_locale_globals_compat.c -o "$EXTRACT/libc_locale_globals_compat.o"

mkdir -p "$OUT/system"
rm -f "$OUT/system/libncurses.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/libncurses.5.4.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-U,_clock_get_time -Wl,-U,_mach_host_self \
    -Wl,-U,_host_get_clock_service -Wl,-U,_semaphore_create \
    -o "$OUT/system/libncurses.dylib" \
    "$EXTRACT"/*.o \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_pthread.dylib" \
    2>"$OUT/libncurses_dylib_link.err" \
    && echo "wrote $OUT/system/libncurses.dylib" \
    || { echo "libncurses.dylib link found real undefined symbols:" >&2; grep '^  "' "$OUT/libncurses_dylib_link.err" | sed 's/,.*//' | sort -u >&2; exit 1; }

otool -hv "$OUT/system/libncurses.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
echo "declared -Wl,-U deferrals (expect: <flat-namespace> bind target):"
xcrun dyld_info -fixups "$OUT/system/libncurses.dylib" 2>/dev/null | grep '<flat-namespace>' | awk '{print "  " $NF}'
UNEXPECTED=$(xcrun dyld_info -fixups "$OUT/system/libncurses.dylib" 2>/dev/null | grep -v -E '<flat-namespace>|<weak-def-coalesce>' | grep 'bind' | grep -v -E 'libsystem_c/|libsystem_malloc/|libsystem_platform/|libsystem_kernel/|libsystem_pthread/' || true)
if [ -n "$UNEXPECTED" ]; then
    echo "error: real, unexpected non-sibling bind targets found:" >&2
    echo "$UNEXPECTED" >&2
    exit 1
fi
echo "libncurses.dylib: every non-deferred external reference binds to a real, named sibling dylib -- no unexpected/dangling references"
