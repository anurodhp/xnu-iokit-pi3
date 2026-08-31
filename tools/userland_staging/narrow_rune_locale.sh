#!/bin/bash
# narrow_rune_locale.sh <extract-dir> [filename-prefix]
#
# Shared helper for the leaf dylibs (libiconv, libncurses, libcopyfile,
# libresolv, libopenpam) that each extract table.o + none.o out of
# libc_build/libc_locale.a, AND for build_simple_cmds.sh's own
# common_table.o/common_none.o (a separate compile of the same two real
# source files, hence the optional filename-prefix arg -- pass "common_"
# there). Written 2026-08-30 as part of the static-linking cleanup
# (Phase 1d/2a) -- see docs/static-linking-cleanup-plan.md.
#
# THE PROBLEM
# ===========
# table.o and none.o between them define six symbols. Four are real,
# PUBLIC, process-wide, MUTABLE locale state:
#
#   __CurrentRuneLocale   -- pointer to the locale in effect right now
#   __DefaultRuneLocale   -- the "C" locale rune table
#   ___mb_cur_max         -- MB_CUR_MAX for the current locale
#   ___mb_sb_limit        -- single-byte-character limit for it
#
# Five separate leaf dylibs were each extracting the same two objects, so
# a process holding two of them had two "current locales": a setlocale()
# through one image could never be observed through the other. That is
# the same duplicate-MUTABLE-STATE bug class as mach_task_self_ and
# g_zone, not merely duplicate code. libsystem_c.dylib exports all four
# for real, which is where real Apple keeps them.
#
# The other two are the opposite case, and must NOT be centralised:
#
#   __DefaultRuneXLocale  -- table.o
#   __none_*              -- none.o's 5 single-byte conversion functions
#
# Both are `__private_extern__` in real Apple's own source, and this
# project's build honours that -- `nm -m libsystem_c.dylib` reports them
# "non-external (was a private external)". A private extern is invisible
# across an image boundary BY CONSTRUCTION: there is no symbol for
# another image to bind to the wrong copy of, so every image that
# instantiates locale state legitimately carries its own. Verified with
# nm -m, not assumed.
#
# WHAT THIS DOES
# ==============
# Rewrites the two extracted objects in place with `ld -r`, demoting the
# four public globals to file-local symbols. A file-local definition
# cannot satisfy another object's undefined reference, so the leaf's own
# code binds to libsystem_c.dylib's copies at link time -- while
# __DefaultRuneXLocale / __none_* stay resolvable within this link.
#
# Same real `ld -r -unexported_symbol` narrowing idiom this project
# already uses in build_libdispatch_dylib.sh (___gettimeofday_with_mach)
# and build_libsystem_kernel.sh (mach_port_set_attributes).
#
# The caller must additionally pass
#   -Wl,-unexported_symbol,__DefaultRuneXLocale
# to its own final dylib link, so the one symbol that survives here does
# not become a NEW cross-leaf duplicate export. (It cannot simply be
# demoted here too: this object is where it is defined, and the leaf's
# own libc_locale_globals_compat.o genuinely references it.)
set -e
cd "$(dirname "$0")"

EXTRACT="${1:?usage: narrow_rune_locale.sh <extract-dir> [filename-prefix]}"
PREFIX="${2:-}"
[ -d "$EXTRACT" ] || { echo "error: $EXTRACT is not a directory" >&2; exit 1; }

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
LD="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
[ -x "$LD" ] || { echo "error: $LD not found" >&2; exit 1; }

narrow() {
    local obj="$EXTRACT/$1"; shift
    [ -f "$obj" ] || return 0
    local args=()
    for s in "$@"; do args+=(-unexported_symbol "$s"); done
    # -keep_private_externs is load-bearing (see docs/backlog.md's ld64
    # -Wl,-alias bug entry / this project's own repeated finding):
    # without it, `ld -r` demotes EVERY already-private-extern symbol in
    # the object to a plain local -- not just the ones this call names --
    # which breaks it, not narrows it. table.o's __DefaultRuneXLocale and
    # none.o's __none_* functions are private extern already; they must
    # survive this pass untouched.
    "$LD" -r -arch arm64 -keep_private_externs "${args[@]}" "$obj" -o "$obj.narrowed"
    mv -f "$obj.narrowed" "$obj"
}

narrow "${PREFIX}table.o" __CurrentRuneLocale __DefaultRuneLocale
narrow "${PREFIX}none.o" ___mb_cur_max ___mb_sb_limit

# Real check, not a claim: confirm via `nm -m` (which, unlike plain
# `nm -gUj`, actually distinguishes "external" from "private external" --
# the same distinction this project's own dup_symbol_audit.sh had to
# verify by hand for ___gettimeofday_with_mach/the _chk family) that none
# of the four is still a real, fully-external definition anywhere in the
# extract dir.
LEAK=""
for s in __CurrentRuneLocale __DefaultRuneLocale ___mb_cur_max ___mb_sb_limit; do
    for o in "$EXTRACT"/*.o; do
        nm -m "$o" 2>/dev/null | grep -E "\) external $s\$" >/dev/null 2>&1 && LEAK="$LEAK $s(in $(basename "$o"))"
    done
done
if [ -n "$LEAK" ]; then
    echo "error: rune-locale globals still fully external (not private) in $EXTRACT:$LEAK" >&2
    exit 1
fi
echo "narrow_rune_locale: $EXTRACT -- 4 public rune-locale globals now bind to libsystem_c.dylib"
