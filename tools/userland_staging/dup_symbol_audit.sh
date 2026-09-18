#!/bin/bash
# Duplicate-symbol audit for this project's real shipped userland images.
#
# WHY THIS EXISTS
# ===============
# This project spent its first several phases in a static-linking-only
# world: every build_*.sh here produced one flat static archive, and
# every consumer -- including, later, the real .dylib files -- linked
# those archives directly. That is fine when there is exactly one image
# in the process. It is a real bug the moment there are two, because
# each image then gets its OWN private copy of every process-global
# piece of state the archive carries.
#
# That exact bug class has now bitten this project at least five separate
# times, each found the slow way (a real QEMU boot, a real crash, a
# multi-session root-cause hunt):
#
#   - mach_task_self_ separately-uninitialized in libdyld.dylib vs
#     libSystem.B.dylib (Phase 6.5 Phase D, docs/daemon-roadmap.md)
#   - g_zone: two live heap zones in one process
#     (build_libsystem_malloc.sh's own header comment)
#   - a duplicate static copy of mig_reply_port.o
#   - a duplicate static copy of mach_msg_real.o
#   - a duplicate static copy of pthread_create (launchd bug #6, session 5)
#
# Every one of those was findable from `nm` in seconds. None of them was
# found that way, because nothing ever ran `nm` across the whole shipped
# set at once. That is what this script is: the cheap, repeatable check
# that turns this bug class from "discovered by a crash three sessions
# later" into "reported by a build step".
#
# WHAT IT CHECKS
# ==============
# 1. dylib mode: any symbol DEFINED (not imported) by two or more of the
#    real shipped dylibs. On real Darwin a symbol has exactly one owning
#    image; two owners means one of them statically bundled a copy of
#    something a sibling already provides, and the two copies have
#    separate state.
#
# 2. binary mode: any symbol a shipped executable defines LOCALLY that is
#    also exported by one of the dylibs that same executable actually
#    loads (via its own real LC_LOAD_DYLIB list, not a guess). Same bug,
#    one level up: the binary's own references bind to its private copy
#    while the dylib keeps using its own.
#
# Neither check is a heuristic -- both read real Mach-O symbol tables of
# the real built artifacts.
#
# BASELINE, NOT ALLOWLIST
# =======================
# There are too many pre-existing duplicates to fix in one pass, so this
# script compares against a checked-in baseline
# (dup_symbol_baseline.txt) and fails only on symbols that are NEW since
# that baseline was taken. Fixing duplicates never fails the check; it
# just makes the baseline stale, which `--update-baseline` rewrites.
# This makes progress monotonic: the count can go down freely and cannot
# silently go back up.
#
# USAGE
# =====
#   ./dup_symbol_audit.sh                  # both checks against baseline
#   ./dup_symbol_audit.sh --dylibs         # dylib check only
#   ./dup_symbol_audit.sh --binaries       # binary check only
#   ./dup_symbol_audit.sh --summary        # ranked offender table, no pass/fail
#   ./dup_symbol_audit.sh --update-baseline
#   ./dup_symbol_audit.sh --explain SYM    # which images define one symbol
#
# Exit status: 0 = no new duplicates, 1 = new duplicates (or a real
# error). A drop below the baseline is reported but is not a failure.
#
# WHY LC_ALL=C IS PINNED BELOW
# ============================
# Every fact list here is built with `sort`/`sort -u` and then compared
# against the baseline (and against each binary's loaded-export set) with
# `comm`, which is only correct when BOTH inputs are in the byte order it
# compares with. Nothing pinned the locale until 2026-09-18, so both the
# baseline write and every comparison run inherited the user's own
# locale -- `en_US.UTF-8`, whose collation largely ignores `_` and `$`,
# i.e. exactly the two characters that lead or punctuate almost every
# symbol name in this project. `comm` then walked input sorted in an
# order it was not comparing with and emitted garbage IN BOTH
# DIRECTIONS, which is the dangerous part: not merely extra noise, but
# false all-clears too. Real observed output (DAR-325): four symbols
# reported as "newly defined by 2+ dylibs" that are present VERBATIM in
# dup_symbol_baseline.txt, plus 1676 "new" binary facts and 182 "fixed"
# ones. The truth for that same tree, established by diffing the baseline
# before vs after regeneration with BOTH sides normalized to byte order,
# was 89 new facts and 0 fixed (recorded in full in DAR-327). Note that
# merely running the old script with LC_ALL=C did NOT give the right
# answer either -- it reported 176 new / 86 fixed, because the baseline
# FILE on disk was still in locale order, so one side of the comm was
# still mis-sorted. Both the comparison AND the stored baseline have to
# be in the same order, which is why the pin below is paired with a
# regenerated baseline.
# `sort -c dup_symbol_baseline.txt` passed under en_US.UTF-8 and failed
# under LC_ALL=C at line 10, which is how the locale-order baseline was
# confirmed rather than guessed.
# LC_ALL (not just LC_COLLATE) because LC_ALL overrides an LC_COLLATE the
# user may have exported, and nothing here wants locale-dependent
# `grep`/`sed`/`awk` character classes either -- these are ASCII Mach-O
# symbol names, always.
# If you ever regenerate the baseline by hand, regenerate it through this
# script so it stays byte-order-sorted:
# `LC_ALL=C sort -c dup_symbol_baseline.txt` must pass.
set -u
export LC_ALL=C
cd "$(dirname "$0")"

OUT="libc_build"
SYSDIR="$OUT/system"
BASELINE="dup_symbol_baseline.txt"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/dupsym.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

MODE="both"
EXPLAIN=""
case "${1:-}" in
    --dylibs)          MODE="dylibs" ;;
    --binaries)        MODE="binaries" ;;
    --summary)         MODE="summary" ;;
    --update-baseline) MODE="update" ;;
    --explain)         MODE="explain"; EXPLAIN="${2:-}" ;;
    "")                ;;
    *) echo "usage: $0 [--dylibs|--binaries|--summary|--update-baseline|--explain SYM]" >&2; exit 1 ;;
esac

if [ ! -d "$SYSDIR" ]; then
    echo "error: $SYSDIR not found -- build the real dylibs first (build_libsystem.sh and friends)" >&2
    exit 1
fi

# ------------------------------------------------------------------
# Real shipped image set.
#
# Dylibs: everything inject_into_sd_image.sh actually installs, which is
# every *.dylib under libc_build/system/ plus ICU's three real target
# dylibs. Deliberately NOT the host-side ICU build under
# icu_build/host/ -- those never reach the guest.
#
# Binaries: the real .macho files inject_into_sd_image.sh installs.
# dyld.macho and pwtool.macho are excluded ON PURPOSE and not as an
# oversight: both run before or entirely without dyld by design
# (CLAUDE.md's own "must stay fully static" invariant), so a private
# static copy of libc in them is correct, not debt. (init.macho was a
# third entry here until tools/init_binary/init.c was retired
# 2026-08-31 -- see docs/static-linking-cleanup-plan.md.)
# ------------------------------------------------------------------
DYLIBS=()
for f in "$SYSDIR"/*.dylib; do
    [ -f "$f" ] && DYLIBS+=("$f")
done
for f in icu_build/target/lib/libicuuc.66.1.dylib \
         icu_build/target/lib/libicui18n.66.1.dylib \
         icu_build/target/lib/libicudata.66.1.dylib; do
    [ -f "$f" ] && DYLIBS+=("$f")
done
if [ "${#DYLIBS[@]}" -eq 0 ]; then
    echo "error: no dylibs found under $SYSDIR" >&2
    exit 1
fi

STATIC_BY_DESIGN="dyld.macho pwtool.macho"
BINARIES=()
for f in *.macho "$OUT"/*.macho; do
    [ -f "$f" ] || continue
    case " $STATIC_BY_DESIGN " in *" $(basename "$f") "*) continue ;; esac
    # A binary with no LC_LOAD_DYLIB at all is fully static; it has no
    # sibling image to collide with, so it cannot exhibit this bug.
    [ "$(otool -l "$f" 2>/dev/null | grep -c LC_LOAD_DYLIB)" -gt 0 ] || continue
    BINARIES+=("$f")
done

# defined_syms <image> -- real defined external symbols, one per line.
# -U restricts to defined, -j drops the address/type columns (which
# otherwise inject "(indirect ...)" text into the name field).
defined_syms() { nm -gUj "$1" 2>/dev/null | sed '/^$/d' | sort -u; }

# defined_syms_dylib_local <dylib> -- real LOCALLY-DEFINED external
# symbols only, one per line -- i.e. `defined_syms` MINUS anything that
# is merely a `-Wl,-reexport_library`/`-Wl,-alias`-of-a-nonlocal-base
# passthrough of a SIBLING dylib's own real definition.
#
# Found 2026-08-31 auditing libdyld.dylib: `nm -gUj` cannot tell "this
# image really defines this symbol" apart from "this image's export
# trie merely republishes another image's symbol at the same address"
# -- both show up as a plain external symbol. `libdyld.dylib` alone had
# 17 of its 28 "duplicates" turn out to be exactly this: real, valid
# `[re-export]` entries (confirmed via `xcrun dyld_info -exports`,
# e.g. "_kill$UNIX2003 (___kill from libsystem_malloc)") pointing at
# the SAME real address as their sibling's own copy -- not a second,
# independently-compiled definition at all. Real Darwin does not
# consider a reexport a duplicate owner; this audit shouldn't either.
# `dyld_info -exports` distinguishes the two: a genuine local
# definition prints as "0xADDR  symbol", a reexport prints as
# "[re-export] symbol (...)" with no address of its own.
#
# Deliberately NOT used for `defined_syms` above (binaries, and the
# `--explain`/general-purpose helper): ordinary executables don't
# reexport, so `nm -gUj` is already correct and cheaper there. Only
# dylib-mode's Pass 1 (real bundle of siblings that legitimately
# reexport each other -- libSystem.B.dylib/libdyld.dylib/libc++.dylib
# all do) needs this distinction.
defined_syms_dylib_local() {
    xcrun dyld_info -exports "$1" 2>/dev/null \
        | awk '{ gsub(/^[ \t]+/, ""); if ($1 ~ /^0x/) print $2 }' \
        | sed 's/\[.*//' \
        | sed '/^$/d' | sort -u
}

tag() { basename "$1"; }

# ------------------------------------------------------------------
# Pass 1: symbols defined by two or more shipped dylibs.
# ------------------------------------------------------------------
mkdir -p "$WORK/dylib"
for f in "${DYLIBS[@]}"; do
    defined_syms_dylib_local "$f" > "$WORK/dylib/$(tag "$f").syms"
done
cat "$WORK"/dylib/*.syms | sort | uniq -d > "$WORK/dylib_dupes.txt"

owners_dylib() {
    local sym="$1" out=""
    for s in "$WORK"/dylib/*.syms; do
        grep -qxF "$sym" "$s" && out="$out $(basename "$s" .syms)"
    done
    echo "${out# }"
}

# ------------------------------------------------------------------
# Pass 2: for each shipped binary, symbols it defines locally that are
# also exported by a dylib IT ACTUALLY LOADS. The install_name -> file
# map comes from each dylib's own real LC_ID_DYLIB, so a dylib whose
# installed path does not match its filename (libc++.dylib installs as
# /usr/lib/libc++.1.dylib, libncurses.dylib as /usr/lib/libncurses.5.4.dylib)
# still resolves correctly.
# ------------------------------------------------------------------
: > "$WORK/idmap.txt"
for f in "${DYLIBS[@]}"; do
    id=$(otool -l "$f" 2>/dev/null | awk '/LC_ID_DYLIB/{g=1} g&&/name /{print $2; exit}')
    [ -n "$id" ] && echo "$id|$f" >> "$WORK/idmap.txt"
done

# resolve_install_name <install_name> -> local file path, or empty.
resolve_install_name() {
    local line
    line=$(grep -F -- "$1|" "$WORK/idmap.txt" | head -1)
    [ -n "$line" ] && printf '%s' "${line#*|}"
}

# collect_exports <dylib-file> <accumulator-file> <seen-file>
#
# Appends that dylib's own defined symbols PLUS, transitively, those of
# every dylib it re-exports. Following LC_REEXPORT_DYLIB is not optional
# here: this project's libSystem.B.dylib is a thin umbrella that defines
# only ~6 symbols of its own and re-exports ten sub-dylibs, so a binary
# linking just libSystem.B.dylib really does get libsystem_c's entire
# export surface at runtime. Treating the umbrella as 6 symbols would
# under-report nearly every shipped command.
collect_exports() {
    local f="$1" acc="$2" seen="$3" t
    t=$(tag "$f")
    grep -qxF "$t" "$seen" 2>/dev/null && return 0
    echo "$t" >> "$seen"
    [ -f "$WORK/dylib/$t.syms" ] && cat "$WORK/dylib/$t.syms" >> "$acc"
    local rname rfile
    while read -r rname; do
        [ -n "$rname" ] || continue
        rfile=$(resolve_install_name "$rname")
        [ -n "$rfile" ] && collect_exports "$rfile" "$acc" "$seen"
    done < <(otool -l "$f" 2>/dev/null | awk '/LC_REEXPORT_DYLIB/{g=1} g&&/^ *name /{print $2; g=0}')
}

: > "$WORK/binary_dupes.txt"
: > "$WORK/binary_report.txt"
for b in "${BINARIES[@]}"; do
    loaded="$WORK/loaded_$(tag "$b").syms"
    seen="$WORK/seen_$(tag "$b").txt"
    : > "$loaded"; : > "$seen"
    while read -r name; do
        f=$(resolve_install_name "$name")
        [ -n "$f" ] && collect_exports "$f" "$loaded" "$seen"
    done < <(otool -L "$b" 2>/dev/null | tail -n +2 | awk '{print $1}')
    sort -u -o "$loaded" "$loaded"
    defined_syms "$b" > "$WORK/own_$(tag "$b").syms"
    shadow=$(comm -12 "$WORK/own_$(tag "$b").syms" "$loaded")
    n=$(printf '%s' "$shadow" | grep -c . )
    printf '%s\t%s\n' "$n" "$(tag "$b")" >> "$WORK/binary_report.txt"
    [ "$n" -gt 0 ] && printf '%s\n' "$shadow" | sed "s|^|$(tag "$b") |" >> "$WORK/binary_dupes.txt"
done

# ------------------------------------------------------------------
# Canonical baseline form: one line per duplicate fact, sorted. Dylib
# facts are keyed by symbol alone (the owner set is informational and
# churns as sub-dylibs get split); binary facts are keyed by
# "binary symbol" so moving a duplicate between binaries is still caught.
# ------------------------------------------------------------------
{
    sed 's/^/D /' "$WORK/dylib_dupes.txt"
    sed 's/^/B /' "$WORK/binary_dupes.txt"
} | sort > "$WORK/current.txt"

case "$MODE" in
explain)
    [ -n "$EXPLAIN" ] || { echo "error: --explain needs a symbol name" >&2; exit 1; }
    echo "dylibs defining $EXPLAIN:"
    o=$(owners_dylib "$EXPLAIN"); echo "  ${o:-<none>}"
    echo "binaries defining $EXPLAIN locally while also loading a dylib that exports it:"
    grep -E " $(printf '%s' "$EXPLAIN" | sed 's/[.[\*^$]/\\&/g')\$" "$WORK/binary_dupes.txt" \
        | awk '{print "  " $1}' | sort -u
    exit 0
    ;;
summary)
    echo "=== shipped dylibs: exports / duplicated with a sibling ==="
    for s in "$WORK"/dylib/*.syms; do
        tot=$(wc -l < "$s")
        dup=$(comm -12 "$s" "$WORK/dylib_dupes.txt" | wc -l)
        printf '%8d dup  %6d exports  %s\n' "$dup" "$tot" "$(basename "$s" .syms)"
    done | sort -rn
    echo
    echo "total distinct symbols defined by 2+ shipped dylibs: $(wc -l < "$WORK/dylib_dupes.txt")"
    echo
    echo "=== shipped binaries: locally-defined symbols shadowing a loaded dylib ==="
    sort -rn "$WORK/binary_report.txt" | awk '$1>0{printf "%8d shadowed  %s\n", $1, $2}'
    echo
    echo "total binary shadowing facts: $(grep -c . "$WORK/binary_dupes.txt")"
    exit 0
    ;;
update)
    cp "$WORK/current.txt" "$BASELINE"
    echo "wrote $BASELINE ($(grep -c '^D ' "$BASELINE") dylib duplicates, $(grep -c '^B ' "$BASELINE") binary shadowing facts)"
    exit 0
    ;;
esac

if [ ! -f "$BASELINE" ]; then
    echo "error: $BASELINE not found -- run '$0 --update-baseline' once to record the current state" >&2
    exit 1
fi

grep '^D ' "$WORK/current.txt" > "$WORK/cur_d.txt" || true
grep '^B ' "$WORK/current.txt" > "$WORK/cur_b.txt" || true
grep '^D ' "$BASELINE"         > "$WORK/base_d.txt" || true
grep '^B ' "$BASELINE"         > "$WORK/base_b.txt" || true

fail=0
if [ "$MODE" = "both" ] || [ "$MODE" = "dylibs" ]; then
    new=$(comm -13 "$WORK/base_d.txt" "$WORK/cur_d.txt")
    gone=$(comm -23 "$WORK/base_d.txt" "$WORK/cur_d.txt" | grep -c . )
    if [ -n "$new" ]; then
        echo "FAIL: symbols newly defined by 2+ shipped dylibs (not in $BASELINE):"
        printf '%s\n' "$new" | sed 's/^D //' | while read -r s; do
            echo "  $s  <-  $(owners_dylib "$s")"
        done
        echo
        echo "  Each of these means one dylib statically bundled something a sibling"
        echo "  dylib already provides. The two copies have separate state. Link the"
        echo "  sibling .dylib instead of its static ancestor archive."
        fail=1
    else
        echo "OK: no new duplicate symbols across the $(( ${#DYLIBS[@]} )) shipped dylibs"
    fi
    [ "$gone" -gt 0 ] && echo "note: $gone baseline dylib duplicate(s) fixed -- rerun with --update-baseline to lock the gain in"
fi

if [ "$MODE" = "both" ] || [ "$MODE" = "binaries" ]; then
    new=$(comm -13 "$WORK/base_b.txt" "$WORK/cur_b.txt")
    gone=$(comm -23 "$WORK/base_b.txt" "$WORK/cur_b.txt" | grep -c . )
    if [ -n "$new" ]; then
        echo "FAIL: shipped binaries newly defining a symbol their own loaded dylibs export:"
        printf '%s\n' "$new" | sed 's/^B //' | head -60
        cnt=$(printf '%s\n' "$new" | grep -c . )
        [ "$cnt" -gt 60 ] && echo "  ... and $((cnt - 60)) more"
        echo
        echo "  The binary's own code will bind to its private copy while the dylib"
        echo "  keeps using its own -- two FILE tables, two heap zones, two errnos."
        fail=1
    else
        echo "OK: no new dylib-shadowing symbols across the $(( ${#BINARIES[@]} )) shipped binaries"
    fi
    [ "$gone" -gt 0 ] && echo "note: $gone baseline binary shadowing fact(s) fixed -- rerun with --update-baseline to lock the gain in"
fi

exit $fail
