#!/bin/bash
# Stale-weak-def-coalesce-bind audit for this project's dyld userland.
#
# WHY THIS EXISTS (DAR-200, 2026-09-02). ld64 emits a
# `<weak-def-coalesce>` bind (chained-fixups library ordinal
# BIND_SPECIAL_DYLIB_WEAK_DEF_COALESCE) for any reference whose
# definition happened to be a *weak definition* in some dylib at the time
# that image was linked. At runtime real dyld-940 resolves those binds in
# dyld/Loader.cpp's resolveSymbol(), and that search only ever looks
# inside images whose Mach header carries MH_WEAK_DEFINES (0x8000):
#
#     for ( const Loader* ldr : state.loaded ) {
#         if ( ldr->loadAddress(state)->flags & MH_WEAK_DEFINES ) { ... }
#     }
#     ...
#     else { diag.error("weak-def symbol not found '%s'", symbolName); }
#
# If no image loaded IN THAT PROCESS has both MH_WEAK_DEFINES and the
# symbol, dyld aborts the process before main(). This is a real, silent,
# cross-artifact staleness hazard specific to this project's build model:
# nothing relinks automatically, so flipping a symbol from a weak
# definition to a strong one in ONE dylib instantly breaks every
# already-built image that still carries a coalesce bind for it -- with a
# clean `nm`, a clean link, and no build error anywhere.
#
# That is exactly what happened: commit f8b82ce made libsystem_c.dylib
# export a strong `_getenv` (correctly -- the previous export was
# libc_locale_env_stubs.o's always-NULL weak stub, DAR-153), which
# dropped MH_WEAK_DEFINES from libsystem_c.dylib. Every image built
# before that still bound `_getenv` through the coalesce ordinal, so
# every such process died at launch with
# `weak-def symbol not found '_getenv'`.
#
# Resolution is PER PROCESS, so this audits each image against its own
# transitive LC_LOAD_DYLIB closure, not against every artifact on disk --
# `launchd.macho` and `syslogd.macho` do happen to export a weak
# `_getenv` of their own, but that rescues only their own processes.
#
# Usage:
#   tools/userland_staging/weak_def_audit.sh [image ...]
#
# With no arguments, audits every .dylib under libc_build/system/, every
# .macho in libc_build/ and tools/userland_staging/, and every Mach-O image
# in the staged install trees (libc_build/x11/server/root, gnustep/root,
# fontstack/{root,bin}, ...; see staged_images.sh).
#
# C++ mangled names are reported separately: those are genuine
# template/inline weak defs that normally resolve inside libc++ itself
# (which does carry MH_WEAK_DEFINES), so they are expected.
#
# Fix for anything reported STALE: rebuild that image with its own
# build_*.sh. Do NOT "fix" it by re-weakening the provider -- that is
# what DAR-153 already had to undo.
set -uo pipefail
cd "$(dirname "$0")"
. ./staged_images.sh

if ! command -v xcrun >/dev/null 2>&1; then
    echo "error: xcrun not found (need dyld_info)" >&2
    exit 1
fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# install-name -> local file index, built from each dylib's own
# LC_ID_DYLIB. Needed because on-disk basenames and real install names
# genuinely differ here (libresolv.dylib vs /usr/lib/libresolv.9.dylib,
# libc++.dylib vs /usr/lib/libc++.1.dylib, ...).
staged_images > "$WORK/staged"
# Staged dylibs too: libgnustep-base's install name is under /usr/GNUstep,
# not libc_build/system/, and a versioned real file keeps its version
# suffix (the plain .dylib name is the symlink find skips).
for d in libc_build/system/*.dylib; do
    [ -f "$d" ] && echo "$d"
done | cat - <(staged_images DYLIB) | while IFS= read -r d; do
    id=$(otool -D "$d" 2>/dev/null | tail -1)
    [ -z "$id" ] && continue
    printf '%s\t%s\n' "$id" "$d"
done > "$WORK/index"

resolve_install_name() {
    awk -F'\t' -v n="$1" '$1 == n { print $2; exit }' "$WORK/index"
}

# Transitive LC_LOAD_DYLIB closure of $1, as local file paths.
closure() {
    local start="$1" queue=("$1") seen="$WORK/seen.$$"
    : > "$seen"
    echo "$start" >> "$seen"
    while [ "${#queue[@]}" -gt 0 ]; do
        local cur="${queue[0]}"
        queue=("${queue[@]:1}")
        local dep
        while read -r dep; do
            [ -z "$dep" ] && continue
            local path
            path=$(resolve_install_name "$dep")
            [ -z "$path" ] && continue
            grep -qxF "$path" "$seen" && continue
            echo "$path" >> "$seen"
            queue+=("$path")
        done < <(otool -L "$cur" 2>/dev/null | tail -n +2 | awk '{print $1}')
    done
    cat "$seen"
    rm -f "$seen"
}

if [ "$#" -gt 0 ]; then
    IMAGES=("$@")
else
    IMAGES=()
    for f in libc_build/system/*.dylib libc_build/*.macho *.macho; do
        [ -f "$f" ] && IMAGES+=("$f")
    done
    # Everything the staged install trees ship (staged_images.sh's header).
    while IFS= read -r f; do IMAGES+=("$f"); done < "$WORK/staged"
fi

# A bind line looks like
#   __DATA_CONST __got 0x8068  bind  <weak-def-coalesce>/_getenv
# and may carry a trailing "+ 0x10" addend, so pick the field that
# actually starts with the ordinal rather than trusting $NF.
coalesce_binds() {
    xcrun dyld_info -fixups "$1" 2>/dev/null |
        awk '{ for (i = 1; i <= NF; i++) if (index($i, "<weak-def-coalesce>/") == 1) print substr($i, 21) }'
}

stale=0
for f in "${IMAGES[@]}"; do
    # Whole-process view: a coalesce bind anywhere in the closure kills
    # the process, not just one in the image being audited. Learned the
    # hard way (DAR-200, 2026-09-02): a smoketest with zero coalesce
    # binds of its own still aborted, because /usr/lib/libSystem.B.dylib
    # reexports libresolv.9.dylib, which pulls libsystem_notify.dylib,
    # which pulls libncurses/libdispatch -- and those were the stale
    # ones. An audit that only looked at the target image reported it
    # clean while a real QEMU boot kept panicking.
    : > "$WORK/binds"
    : > "$WORK/providers"
    while read -r img; do
        [ -f "$img" ] || continue
        coalesce_binds "$img" >> "$WORK/binds"
        flags=$(otool -h "$img" 2>/dev/null | tail -1 | awk '{print $NF}')
        [ -z "$flags" ] && continue
        if (( (flags & 0x8000) != 0 )); then
            xcrun dyld_info -exports "$img" 2>/dev/null |
                awk '{ for (i = 1; i <= NF; i++) if (index($i, "_") == 1) { print $i; break } }' >> "$WORK/providers"
        fi
    done < <(closure "$f")
    sort -u -o "$WORK/binds" "$WORK/binds"
    sort -u -o "$WORK/providers" "$WORK/providers"
    [ -s "$WORK/binds" ] || continue

    missing_c=""
    missing_cxx=0
    while IFS= read -r sym; do
        [ -z "$sym" ] && continue
        grep -qxF "$sym" "$WORK/providers" && continue
        case "$sym" in
            __Z*|_ZN*) missing_cxx=$((missing_cxx + 1)) ;;
            *)         missing_c="$missing_c $sym" ;;
        esac
    done < "$WORK/binds"

    if [ -n "$missing_c" ]; then
        echo "STALE  $f"
        for s in $missing_c; do
            owners=""
            while read -r img; do
                coalesce_binds "$img" | grep -qxF "$s" && owners="$owners $(basename "$img")"
            done < <(closure "$f")
            echo "         unresolvable weak-def bind: $s (from:$owners)"
        done
        stale=$((stale + 1))
    elif [ "$missing_cxx" -gt 0 ]; then
        echo "note   $f -- $missing_cxx unresolvable C++ weak-def bind(s) (usually fine: resolved inside libc++ at runtime)"
    fi
done

echo
if [ "$stale" -eq 0 ]; then
    echo "weak_def_audit: no image has an unresolvable non-C++ weak-def-coalesce bind"
    exit 0
fi
echo "weak_def_audit: $stale image(s) would abort at dyld load -- rebuild each with its own build_*.sh"
exit 1
