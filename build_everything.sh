#!/bin/bash
# Rebuilds this entire project from a fresh clone -- or forces a fully
# clean rebuild on an existing checkout -- in real dependency order.
# See Linear DAR-275.
#
# Why this exists, specifically: DAR-266 burned a full session chasing
# what looked like a real crash in configd. The actual cause was a
# stale, gitignored, leftover-instrumented .o silently relinked from
# tools/userland_staging/libc_build/ (the DAR-249/DAR-263 staleness
# class -- see that directory's staleness_check.sh for the per-script
# mitigation, and DAR-271 for extending it to configd's own chain).
# This script is the structural fix: it wipes every gitignored,
# regenerated userland_staging output directory before rebuilding
# anything, so stale state cannot carry over between runs by
# construction, not by a per-script mtime check that has to be
# remembered and added everywhere.
#
# Usage:
#   ./build_everything.sh              # real, full rebuild
#   ./build_everything.sh --dry-run    # resolve + print/validate the
#                                       # build order; runs nothing
#
# Does NOT boot the result afterward -- that's ./runvm.sh, run
# separately and explicitly (see DAR-275's own stated scope).
#
# Does NOT invoke tools/pwtool/build.py directly -- tools/init_binary/
# inject_into_sd_image.sh already rebuilds pwtool.macho itself as part
# of installing onto the SD image (see that script's own header).

set -euo pipefail
cd "$(dirname "$0")"

DRY_RUN=0
if [ "${1:-}" = "--dry-run" ]; then
    DRY_RUN=1
fi

ORDER_FILE="build/build_everything_order.txt"
mkdir -p build

step() {
    echo
    echo "==> $*"
}

# Runs a top-level (non-userland_staging) build step: validates the
# script exists either way, then either runs it for real or just says
# so under --dry-run. Invoked via `bash`, not `./`, since not every
# build_*.sh in this repo carries the executable bit (they're normally
# run as `bash script.sh`) -- checking for +x here would reject real,
# working scripts.
run_step() {
    local path="$1"
    if [ ! -f "$path" ]; then
        echo "error: $path missing" >&2
        exit 1
    fi
    if [ "$DRY_RUN" = "1" ]; then
        echo "  (dry-run) would run: $path"
    else
        bash "$path"
    fi
}

# ---- 1. third_party/ ----
step "1/6 clone third_party/ dependencies (setup_third_party.sh)"
run_step ./setup_third_party.sh

# ---- 2. kernel ----
step "2/6 build kernel (tools/build_xnu_7195.sh)"
run_step tools/build_xnu_7195.sh

# ---- 3. kexts ----
step "3/6 build IOKit kexts (tools/build_kexts.sh)"
run_step tools/build_kexts.sh

# ---- 4. wipe stale generated userland_staging state ----
# Full list of regenerated (gitignored) userland_staging output dirs --
# kept in sync with .gitignore's own userland_staging entries; each of
# those has its own comment there explaining what regenerates it.
step "4/6 wipe gitignored userland_staging generated dirs (staleness fix)"
STALE_DIRS=(
    tools/userland_staging/libc_build
    tools/userland_staging/bash_build
    tools/userland_staging/bash_survey_out
    tools/userland_staging/icu_build
)
for d in "${STALE_DIRS[@]}"; do
    if [ "$DRY_RUN" = "1" ]; then
        echo "  (dry-run) would rm -rf $d"
    else
        rm -rf "$d"
        echo "  removed $d"
    fi
done

# ---- 5. userland libraries/binaries, in real dependency order ----
step "5/6 build userland_staging libraries/binaries in dependency order"
if [ ! -f tools/userland_staging/gen_build_order.py ]; then
    echo "error: tools/userland_staging/gen_build_order.py missing" >&2
    exit 1
fi
ORDER="$(python3 tools/userland_staging/gen_build_order.py)"
echo "$ORDER" > "$ORDER_FILE"
SCRIPT_COUNT="$(echo "$ORDER" | grep -c .)"
echo "  resolved order for $SCRIPT_COUNT scripts written to $ORDER_FILE"

while IFS= read -r script; do
    [ -z "$script" ] && continue
    path="tools/userland_staging/$script"
    if [ ! -f "$path" ]; then
        echo "error: $path missing" >&2
        exit 1
    fi
    if [ "$DRY_RUN" = "1" ]; then
        echo "  (dry-run) would run: $path"
    else
        echo "  running $script"
        ( cd tools/userland_staging && bash "$script" )
    fi
done <<< "$ORDER"

# ---- 6. install onto the SD image (also rebuilds pwtool.macho) ----
step "6/6 install onto SD image (tools/init_binary/inject_into_sd_image.sh)"
run_step tools/init_binary/inject_into_sd_image.sh

echo
if [ "$DRY_RUN" = "1" ]; then
    echo "build_everything.sh --dry-run: order validated, nothing built. See $ORDER_FILE."
else
    echo "build_everything.sh: done. Boot with ./runvm.sh."
fi
