#!/bin/bash
# Two-level-bind target audit for this project's dyld userland.
#
# WHY THIS EXISTS (DAR-217, 2026-09-02). ld64 and real dyld disagree about
# how far to search when resolving a two-level-namespace bind, and the
# disagreement is completely silent until the process launches:
#
#   * ld64, resolving an undefined symbol, searches the transitive
#     LC_LOAD_DYLIB closure of every dylib on the link line, in order --
#     then records the bind against whichever dylib was LISTED, not
#     against the one that actually defines the symbol.
#   * real dyld-940, resolving that bind, looks only inside the named
#     image and the images it RE-EXPORTS (LC_REEXPORT_DYLIB). A plain
#     LC_LOAD_DYLIB dependency of the named image is not searched.
#
# So a symbol that is a real export of dylib B, reached at link time
# through dylib A because A plain-links B, links clean, `nm` clean, and
# then aborts before main():
#
#     dyld[7]: Symbol not found: ___assert_rtn
#       Referenced from: /usr/sbin/syslogd
#       Expected in: /usr/lib/system/libdispatch.dylib
#
# That is the real failure this script was written after: libdispatch.dylib
# plain-links (does not re-export) libsystem_malloc.dylib, which is the
# only image in this project exporting ___assert_rtn, and syslogd.macho
# listed libdispatch.dylib before libsystem_malloc.dylib on its link line.
# Fix is always to LIST the real owning dylib earlier on the link line
# (build_syslogd.sh's own "ORDER MATTERS" comment) -- never to add a
# re-export to an unrelated dylib just to make the bind resolve.
#
# As a launchd job this class of failure is close to invisible: the abort
# shows up only as a bare "Failed to send exception EXC_CORPSE_NOTIFY.
# error code: 5 for pid N" line on the console, with no symbol name and no
# image name anywhere. Running the same binary by hand from the root shell
# is what surfaced the real dyld message. This script finds the same thing
# from the build host in seconds -- same motivation as gap_audit.sh and
# weak_def_audit.sh, and it deliberately reuses weak_def_audit.sh's
# install-name index/closure shape rather than inventing another one.
#
# Usage:
#   tools/userland_staging/bind_target_audit.sh [image ...]
#
# With no arguments, audits every .dylib under libc_build/system/, every
# .macho in libc_build/ and tools/userland_staging/, and every target
# (non-macOS-platform) Mach-O anywhere else under libc_build/ -- the staged
# trees inject_into_sd_image.sh copies onto the image (x11/server/root,
# gnustep/root, fontstack/...). Those were not swept before DAR-447, and a
# relink sweep missed Xorg/Xvfb there: the real Pi then aborted Xorg at
# dyld load ("Symbol not found: _atan2, Expected in libsystem_c.dylib").
#
# Exit status is 1 if any image has an unresolvable two-level bind.
#
# NOT checked here (each has its own tool/reason):
#   <weak-def-coalesce>  -- weak_def_audit.sh
#   <flat-namespace>     -- a real, deliberate `-Wl,-U` deferral in
#                           several build_*.sh scripts; resolution is
#                           process-wide, not per-image
#   <this-image>         -- self/umbrella bind, resolved through the
#                           image's own re-exports; checked, since that is
#                           the same re-export walk
set -uo pipefail
cd "$(dirname "$0")"

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
: > "$WORK/index"
for d in libc_build/system/*.dylib icu_build/target/lib/*.dylib \
         libc_build/gnustep/root/usr/GNUstep/System/Library/Libraries/*.dylib; do
    [ -f "$d" ] && [ ! -L "$d" ] || continue
    id=$(otool -D "$d" 2>/dev/null | tail -1)
    [ -z "$id" ] && continue
    printf '%s\t%s\n' "$id" "$d" >> "$WORK/index"
done

resolve_install_name() {
    awk -F'\t' -v n="$1" '$1 == n { print $2; exit }' "$WORK/index"
}

# LC_REEXPORT_DYLIB entries of $1, as install names. This is the ONLY
# edge real dyld follows when resolving a two-level bind, which is the
# whole point of this script -- do not "helpfully" widen it to
# LC_LOAD_DYLIB.
reexports_of() {
    otool -l "$1" 2>/dev/null | awk '
        /^ *cmd LC_REEXPORT_DYLIB/ { want = 1; next }
        want && /^ *name / { print $2; want = 0 }
    '
}

# Transitive re-export closure of $1, as local file paths (including $1).
reexport_closure() {
    local start="$1" seen
    seen=$(mktemp "$WORK/rx.XXXXXX")
    local -a queue=("$1")
    : > "$seen"
    echo "$start" >> "$seen"
    while [ "${#queue[@]}" -gt 0 ]; do
        local cur="${queue[0]}"
        queue=("${queue[@]:1}")
        local dep path
        while read -r dep; do
            [ -z "$dep" ] && continue
            path=$(resolve_install_name "$dep")
            [ -z "$path" ] && continue
            grep -qxF "$path" "$seen" && continue
            echo "$path" >> "$seen"
            queue+=("$path")
        done < <(reexports_of "$cur")
    done
    cat "$seen"
    rm -f "$seen"
}

# dyld_info labels a bind target with the install name's leaf up to its
# first '.', so /usr/lib/libSystem.B.dylib prints as "libSystem" and
# /usr/lib/libc++.1.dylib as "libc++". Map that back to the image's own
# dependency list rather than guessing an on-disk filename.
dep_install_names() {
    otool -l "$1" 2>/dev/null | awk '
        /^ *cmd LC_(LOAD_DYLIB|LOAD_WEAK_DYLIB|REEXPORT_DYLIB|LOAD_UPWARD_DYLIB)/ { want = 1; next }
        want && /^ *name / { print $2; want = 0 }
    '
}

fail=0
if [ "$#" -gt 0 ]; then
    IMAGES=("$@")
else
    IMAGES=()
    for f in libc_build/system/*.dylib libc_build/*.macho *.macho; do
        [ -f "$f" ] && IMAGES+=("$f")
    done
    # Staged trees: every other target Mach-O under libc_build (skipping
    # the gnustep *-src configure trees,
    # object dirs, symlinks, and host build tools, which are platform 1 =
    # macOS in LC_BUILD_VERSION).
    while IFS= read -r f; do
        case "$f" in libc_build/system/*.dylib|libc_build/*.macho) continue ;; esac
        file -b "$f" | grep -q '^Mach-O' || continue
        otool -l "$f" 2>/dev/null | grep -q '^ *platform 1$' && continue
        IMAGES+=("$f")
    done < <(find libc_build -type f \( -perm -u+x -o -name '*.dylib' -o -name '*.so' \) \
                 -not -path '*/obj/*' -not -path '*_obj/*' -not -path 'libc_build/gnustep/*-src/*' 2>/dev/null)
fi

for img in "${IMAGES[@]}"; do
    [ -f "$img" ] || { echo "skip: $img (not found)"; continue; }

    # label -> local dylib path, for this image's own dependency list
    : > "$WORK/labels"
    while read -r dep; do
        [ -z "$dep" ] && continue
        leaf=${dep##*/}
        label=${leaf%%.*}
        path=$(resolve_install_name "$dep")
        [ -z "$path" ] && continue
        printf '%s\t%s\n' "$label" "$path" >> "$WORK/labels"
    done < <(dep_install_names "$img")

    bad=""
    warn=""
    while read -r target; do
        [ -z "$target" ] && continue
        label=${target%%/*}
        sym=${target#*/}
        case "$label" in
            '<weak-def-coalesce>'|'<flat-namespace>'|'<missing-weak-import>') continue ;;
            '<this-image>') path="$img" ;;
            *) path=$(awk -F'\t' -v l="$label" '$1 == l { print $2; exit }' "$WORK/labels") ;;
        esac
        if [ -z "$path" ]; then
            # Not a failure: the bind target is simply a dylib this script
            # has no local copy of to inspect (anything outside
            # libc_build/system/ and icu_build/target/lib/). Reported so a
            # genuinely missing dylib is still visible.
            warn+="  UNMAPPED  $target (no local dylib matches label '$label' -- not checked)"$'\n'
            continue
        fi
        # One cached, fully-materialised export set per bind-target dylib.
        # Deliberately NOT an nm-per-symbol probe inside a nested process
        # substitution: that earlier shape was both O(binds x closure) slow
        # and genuinely racy (it reported a DIFFERENT bogus UNRESOLVABLE
        # symbol on each run of the same unchanged binary).
        cache="$WORK/exports.$(echo "$path" | tr '/.' '__')"
        if [ ! -f "$cache" ]; then
            : > "$cache"
            for c in $(reexport_closure "$path"); do
                nm -gUj "$c" 2>/dev/null >> "$cache"
            done
            sort -u -o "$cache" "$cache"
        fi
        if ! grep -qxF "$sym" "$cache"; then
            bad+="  UNRESOLVABLE  $target (not exported by $path or anything it re-exports)"$'\n'
        fi
    done < <(xcrun dyld_info -fixups "$img" 2>/dev/null |
             awk '{ for (i = 1; i < NF; i++) if ($i == "bind") { print $(i+1); break } }' |
             sort -u)

    if [ -n "$bad" ]; then
        echo "$img:"
        printf '%s' "$bad"
        fail=1
    elif [ -n "$warn" ] && [ "${BIND_AUDIT_VERBOSE:-0}" = "1" ]; then
        echo "$img: (unchecked bind targets)"
        printf '%s' "$warn"
    fi
done

if [ "$fail" -eq 0 ]; then
    echo "bind_target_audit: every two-level bind resolves inside its target's own re-export closure"
else
    echo "bind_target_audit: FAILURES above -- relink the reporting image with the real owning dylib listed EARLIER on its link line" >&2
fi
exit "$fail"
