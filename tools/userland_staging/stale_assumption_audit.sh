#!/bin/bash
# Stale-assumption audit for this project's hand-rolled compat shims.
#
# WHY THIS EXISTS
# ===============
# A hand-rolled substitute for real Apple code gets written and narrowed
# on the strength of an assumption about the environment ("no dyld
# exists", "this target is provably single-threaded", "daemon() can
# never actually run"). The environment later changes -- a stub goes
# real, a subsystem gets wired up -- and nobody sweeps for the
# assumption. The narrowing silently becomes wrong, and is found by a
# crash several sessions later instead of by a comment review.
#
# That happened for real, more than once (DAR-222: pthread_create()'s
# real thread-creation half was left dead-code-incomplete on the strength
# of "bsdthread_create always returns ENOTSUP", which stopped being true
# on 2026-08-20 and cost a multi-session investigation to find; DAR-226's
# own filing found a second instance, openpam_dynamic_compat.c's "no dyld
# at all" claim, stale since Phase 6.5 and never acted on until this
# script's own first real run swept it up). Same argument as
# dup_symbol_audit.sh's own header: every instance was findable from a
# grep in seconds, and none of them was found that way because nothing
# ever ran the grep.
#
# WHAT THIS DOES AND DOES NOT DO
# ===============================
# This is a candidate finder, not a pass/fail gate. There is no
# mechanical way to know whether an assertion like "never reached" is
# still true today -- that needs a human (or an agent) to read the real
# source the comment claims to be citing and check it against the
# CURRENT build, exactly as DAR-226's own description says. This script
# only does the cheap, repeatable part: surfacing every file that makes
# one of these assertions, so that triage starts from a real list
# instead of being re-derived from scratch (or not done at all) each
# time. Expect most hits to still be correct -- the point is finding the
# minority that are not.
#
# WHEN TO RUN THIS
# =================
# Any time a subsystem this project's shims commonly assume-away goes
# from stub to real: dyld, real threading (bsdthread_create), a real
# launchd/bootstrap namespace, real dispatch, a new dylib gaining a
# symbol it didn't export before. Per this project's own standing memory
# rule ("when a dependency goes stub->real, sweep for stale-assumption
# code", 2026-08-24) -- this script is that sweep, made mechanical
# instead of ad hoc.
#
# Usage: tools/userland_staging/stale_assumption_audit.sh [-v]
#   -v   also print the matching line(s), not just the filename

set -euo pipefail
cd "$(dirname "$0")"

VERBOSE=0
[ "${1:-}" = "-v" ] && VERBOSE=1

PATTERN='never (dereferenced|called|reached|runs|happens)|dead code|can never|cannot ever|always fails|not reachable|structurally inapplicable|provably'

FILES=$(grep -rlniE "$PATTERN" ./*.c ./*.h ./*/*.h 2>/dev/null | sort)
COUNT=$(printf '%s\n' "$FILES" | grep -c . || true)

echo "stale_assumption_audit: $COUNT file(s) assert an environment-dependent claim"
echo "(\"never happens\" / \"can never\" / \"provably X\" / \"dead code\" / ...)."
echo "Each one is a real claim about the environment that was true when written"
echo "and may not be now -- read the cited real source (not the comment's own"
echo "reasoning) and check it against the CURRENT build before trusting it."
echo

if [ "$VERBOSE" = "1" ]; then
    printf '%s\n' "$FILES" | while read -r f; do
        [ -z "$f" ] && continue
        echo "=== $f ==="
        grep -niE "$PATTERN" "$f"
        echo
    done
else
    printf '%s\n' "$FILES"
    echo
    echo "(re-run with -v to see the matching line(s) in each file)"
fi
