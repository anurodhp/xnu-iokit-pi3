#!/bin/bash
# Generalizes build_launchd.sh's own staleness guard (DAR-200,
# 2026-09-02) into a reusable check any build_*.sh script can call.
#
# Usage: ./staleness_check.sh <output-file> <override-env-var> <source-dir> [find-args...]
#
#   output-file       the .a/.o/.macho/generated-header this run is about
#                      to reuse or produce
#   override-env-var  name of an env var that, if set to "1", downgrades
#                      a hit to a warning (for a deliberate bisect) --
#                      e.g. LAUNCHD_ALLOW_STALE_CORE
#   source-dir        real source tree this output was built FROM
#   find-args...      `find` predicate selecting which files under
#                      source-dir count (e.g. -name '*.c' -o -name '*.h').
#                      This script wraps it in \( ... \) before adding
#                      its own -newer/-print, so a multi-pattern `-o`
#                      predicate here binds the way it reads, not just
#                      to the last alternative -- pass it exactly as you
#                      would inside a bare pair of parens.
#
# Exits 1 (unless the override var is "1") if anything matching under
# source-dir is newer than output-file. Prints the first 5 newer files
# so the operator knows what would silently be missing.
#
# ---- Why this exists, generalized from one script to all of them ----
#
# Three separate real bugs this session (2026-09-07) all had the same
# root cause: tools/userland_staging/libc_build/ is gitignored,
# long-lived, and accumulates state across sessions with nothing
# checking whether that state still matches the real source it was
# built from.
#
#   - DAR-249: build_libmalloc.sh depended on a compat header
#     (os/feature_private.h) that nothing in the repo actually wrote --
#     it existed once, by hand, then vanished from someone's libc_build/,
#     and the script silently compiled 0 of 9 real files before dying at
#     `ar` with a confusing glob error.
#   - DAR-263: os/log_private.h and SCPreferencesKeychainPrivate.h were
#     each `cat >`-written by TWO different scripts to the same shared
#     path, with no ordering contract -- whichever ran last won,
#     silently, and their content had genuinely diverged (one an 8-byte
#     struct-layout skew that was a live out-of-bounds stack write).
#   - Found fixing DAR-263's remaining images: libc_build/openpam_obj/
#     date_compat.o was a STALE compiled object -- its real source,
#     date_compat.c, had its own send()/writev() stubs REMOVED under
#     DAR-203 ("date now links libsystem_kernel.dylib directly"), but
#     the .o predated that fix and nothing had ever recompiled it.
#     Reusing that stale object for login.macho/getty.macho's own
#     unrelated real gap would have silently reintroduced the exact
#     dead-stub-shadowing bug class this same session's DAR-261/DAR-263
#     work had just fixed elsewhere.
#
# The first two are now fixed at the ROOT (single checked-in source,
# `cmp`-verified on install; see os_log_compat_headers/os/log_private.h
# and the bootp-private SCPreferencesKeychainPrivate.h location) -- a
# staleness check would only have masked a design problem there. The
# third is a different, more common shape: script B reuses script A's
# real archive member, and nothing checks A's output is still fresh
# relative to A's own real source. That is exactly what
# build_launchd.sh's own DAR-200 guard already solves for
# liblaunchd_core.a/launchd.macho -- this script is that same check,
# usable by any OTHER script instead of each reinventing it.
#
# Deliberately mtime-based, not content-hash-based: matches
# build_launchd.sh's own already-proven approach, and a real edit always
# advances mtime whereas a content hash needs the check to already know
# what "current" looks like (defeating the point -- the whole failure
# mode here is not knowing that).
set -u

if [ "$#" -lt 3 ]; then
    echo "usage: $0 <output-file> <override-env-var> <source-dir> [find-args...]" >&2
    exit 2
fi

OUTPUT="$1"
OVERRIDE_VAR="$2"
SRC_DIR="$3"
shift 3

if [ ! -f "$OUTPUT" ]; then
    echo "error: $OUTPUT not found -- nothing to check staleness against" >&2
    exit 1
fi
if [ ! -d "$SRC_DIR" ]; then
    # Not an error: some callers pass an optional/conditional source dir
    # (e.g. a third_party checkout that may not be cloned yet). Silently
    # skip rather than fail a check that build_launchd.sh's own original
    # form also treats as optional (its own `[ -d "$LAUNCHD_SRC" ]` guard).
    exit 0
fi

# The caller's find-args are wrapped in \( ... \) here rather than
# spliced in bare, 2026-09-07 (code review): `find DIR -name a -o -name b
# -newer OUT -print` groups as `-name a -o (-name b -newer OUT -print)`
# -- `-newer`/`-print` bind only to the LAST alternative, so the first
# pattern silently never gets the mtime check at all. Confirmed by
# reproducing: an unparenthesized call missed a real, touched
# sigaction.c outright. Every caller in this tree already writes
# multi-pattern find-args as `-name X -o -name Y` (matching this
# script's own now-corrected header example), so the fix belongs here,
# once, rather than requiring every call site to remember to
# parenthesize its own args correctly.
NEWER=$(find "$SRC_DIR" \( "$@" \) -newer "$OUTPUT" -print 2>/dev/null | head -5)
if [ -n "$NEWER" ]; then
    echo "error: real source under $SRC_DIR is newer than $OUTPUT --" >&2
    echo "       whatever reuses it would silently be missing these changes:" >&2
    echo "$NEWER" | sed 's/^/         /' >&2
    echo "       Re-run whichever build_*.sh script produces $OUTPUT, then re-run this." >&2
    OVERRIDE_VAL="$(eval echo "\${${OVERRIDE_VAR}:-0}")"
    if [ "$OVERRIDE_VAL" = "1" ]; then
        echo "       ($OVERRIDE_VAR=1 set -- proceeding with a knowingly stale $OUTPUT anyway)" >&2
        exit 0
    fi
    echo "       (Set $OVERRIDE_VAR=1 to proceed anyway, e.g. for a deliberate bisect.)" >&2
    exit 1
fi
exit 0
