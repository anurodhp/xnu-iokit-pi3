#!/usr/bin/env python3
"""Derives a real dependency order across tools/userland_staging/build_*.sh.

Why this exists: build_everything.sh (repo root) needs to run all ~100
build_*.sh scripts here in an order that actually satisfies their real
prerequisites, not a hand-guessed one. Every script in this directory
already documents its own real prerequisites the same way -- an
`if [ ! -f "$OUT/<archive-or-dylib>" ]` guard whose error message says
"run build_<other-script-name>.sh first" (see build_libc_regex.sh or
build_configd.sh for two real examples). That textual convention is
exactly what DAR-271 found missing as an *enforced* mtime gate for
configd's own chain -- this script does not add that gate (DAR-271 does,
per-script); it only reads the existence-check convention that already
exists everywhere else to answer "what order do all 100+ of these need
to run in".

Method: every real prerequisite check in this directory follows one of
two established idioms (see build_libc_regex.sh or build_configd.sh for
real examples of both):

  1. A literal `[ ! -f "$OUT/<archive>" ]` guard whose own error message
     says "run build_<other-script>.sh first".
  2. A `for a in libc_locale libmalloc libplatform; do ... done` loop
     (used by ~30 scripts to check several short-name prerequisites at
     once) whose body checks `$OUT/$a.a` and references either the
     parameterized `build_${a}.sh` or the generic `build_*.sh` phrase
     (both real, both in active use -- e.g. build_libc_regex.sh uses the
     former, build_bash.sh/build_cron.sh/build_getty.sh/build_ifconfig.sh
     /build_libc_stdio.sh/build_libnotify_client.sh/build_notifyd.sh/
     build_ping.sh/build_login.sh/build_ps.sh/build_syslogd.sh use the
     latter, confirmed 2026-09-11 after the ordering bug this caused --
     `build_*.sh` reads fine to a human but silently failed to confirm
     the loop as build-script-related, so all 11 scripts' real
     word-list dependencies were being dropped) -- here the dependency
     is on `build_<item>.sh` for every item in the loop's word list, not
     on any literal script name appearing in the source text.

Both are *directional* signals (the script names its own real
prerequisite, not mutual chatter), unlike a bare mention of another
script's name anywhere in the file -- an earlier version of this tool
matched on any mention and produced 74 false dependency cycles out of
103 scripts, because scripts routinely describe their relationship to
siblings in both directions in prose ("build_launchd.sh only links;
sources are compiled by build_launchd_core.sh" appears in both scripts'
own comments). Matching only these two idioms eliminates that: zero
cycles across all 103 real scripts as of this writing.

Cycles: if the derived graph still contains one (a script and its own
real prerequisite both claiming to need the other), it's broken
arbitrarily (stable sort order) and reported on stderr so a human can
check whether the edge was real.
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
# "run build_X.sh" / "Run tools/userland_staging/build_X.sh" -- idiom 1
# above. Case-insensitive "run", optional path prefix before the name.
PREREQ_RE = re.compile(r"(?i)\brun\s+(?:\S+/)?(build_[A-Za-z0-9_]+\.sh)")
# "run build_X.sh, build_Y.sh, and build_Z.sh first" -- a real, common
# variant of idiom 1 (found 2026-09-13) that plain PREREQ_RE above only
# ever captures the FIRST name from, silently dropping the rest as real
# dependencies (build_libpthread_qos.sh's own "run build_libplatform.sh,
# build_libpthread.sh, and build_launchd_syscalls.sh first" only ever
# contributed the first of its three real prerequisites). Only extracts
# from a "run ... first" span on one line, and truncates that span at
# the first literal " or " -- some real scripts genuinely phrase an
# EITHER/OR alternative this way too (build_login.sh: "run
# build_simple_cmds.sh (or build_getty.sh, which shares ...) first"),
# and forcing both sides of a real alternative as required prerequisites
# would be a real, incorrect edge, not just a redundant one.
RUN_SPAN_RE = re.compile(r"(?i)\brun\s+([^\n]*?)\s+first\b")
# "for VAR in item1 item2 ...; do ... done" -- idiom 2 above. Captures
# the loop variable, its word list, and the loop body separately so the
# body can be checked for a `build_${VAR}.sh`-style reference before
# the word list is trusted as a real dependency list.
FOR_LOOP_RE = re.compile(r"for\s+(\w+)\s+in\s+([^;\n]+?)\s*;\s*do(.*?)\bdone\b", re.DOTALL)


def main() -> int:
    scripts = sorted(p.name for p in HERE.glob("build_*.sh"))
    if not scripts:
        print("error: no build_*.sh scripts found next to gen_build_order.py", file=sys.stderr)
        return 1

    script_set = set(scripts)
    deps: dict[str, set[str]] = {s: set() for s in scripts}

    for name in scripts:
        text = (HERE / name).read_text(errors="replace")
        # Join shell line-continuations (a trailing backslash-newline) into
        # one logical line first, the same way the shell itself would --
        # otherwise a `for dep in a b \<newline> c d; do` loop's word list
        # is split across a real newline character, which FOR_LOOP_RE's
        # word-list group deliberately excludes (it must stop before a
        # bare newline so it doesn't swallow the next statement), so the
        # whole loop silently fails to match at all. Found 2026-09-11:
        # build_login.sh's real dependency loop wraps this way and was
        # completely invisible to this script before this fix.
        text = re.sub(r"\\\n", " ", text)

        for match in PREREQ_RE.findall(text):
            if match != name and match in script_set:
                deps[name].add(match)

        for span in RUN_SPAN_RE.findall(text):
            # Drop a real EITHER/OR alternative -- word-boundary split, not
            # a literal " or " substring match: build_login.sh's real text
            # is "build_simple_cmds.sh (or build_getty.sh, ...) first", where
            # "or" is preceded by "(" with no space, which a literal " or "
            # search misses entirely (confirmed by testing this exact
            # string during development -- silently extracted both sides
            # of a real alternative as if both were required).
            span = re.split(r"\bor\b", span, maxsplit=1)[0]
            for match in re.findall(r"build_[A-Za-z0-9_]+\.sh", span):
                if match != name and match in script_set:
                    deps[name].add(match)

        for var, word_list, body in FOR_LOOP_RE.findall(text):
            # Confirm this loop is a real prerequisite gate structurally --
            # does its body actually check `$OUT/$var` (the existence test
            # every one of these loops performs, e.g.
            # `if [ ! -f "$OUT/$dep" ]`) -- rather than by matching one
            # specific human-readable phrasing of the resulting error
            # message. The docstring above already described the design
            # intent this way ("whose body checks $OUT/$a.a"); the
            # original implementation instead required the literal text
            # `build_${var}.sh` to appear, which is only one of several
            # real phrasings scripts here actually use. Found 2026-09-11
            # chasing three different real phrasings in a row (the
            # parameterized form, the generic "build_*.sh" wildcard, and
            # finally free prose -- "run the libSystem tier build scripts
            # first", no build_*.sh text at all) -- matching the
            # structural existence-check instead of any wording is robust
            # to all of them and to future phrasings nobody's written yet.
            # Real gap, found 2026-09-13: many real `for d in libsystem_c
            # ...; do` loops check `$OUT/system/$d.dylib`, not `$OUT/$d`
            # directly -- a literal subdirectory (system/) sits between
            # $OUT and the loop variable (every *_dylib.sh static-to-
            # dylib migration script's own dependency-check loop uses
            # this exact shape). The original regex only matched a bare
            # `$OUT/$var`, so this whole idiom-2 class of real dependency
            # was silently invisible -- confirmed by build_copyfile_dylib.sh
            # /build_libiconv_dylib.sh/build_libm_dylib.sh/
            # build_ncurses_dylib.sh/build_openpam_dylib.sh/
            # build_removefile_dylib.sh all already having a real,
            # correct `for d in libsystem_c ... libsystem_pthread; do if
            # [ ! -f "$OUT/system/$d.dylib" ]` guard that never produced a
            # graph edge.
            #
            # Allowing an arbitrary path segment between $OUT/ and the
            # variable is NOT safe on its own, though (regression found
            # in the same pass): a plain compile-output path like
            # `"$CLANG" ... -o "$OUT/launchd_core_obj/$f.o"` matches the
            # same shape as a real existence check but proves nothing --
            # build_launchd_core.sh's own `for f in ... launchd ...; do`
            # source-compile loop (line ~116) has a bare "launchd" item
            # for launchd.c, which turned into a fabricated
            # build_launchd_core.sh -> build_launchd.sh cycle edge
            # (neither script's real prerequisite) once the path-segment
            # allowance above was added. The real idiom-2 gate is always
            # an `-f` existence TEST, never an `-o` compile-output
            # destination, so requiring a literal `-f "..."` test
            # wrapping the reference (not just textual proximity to
            # $OUT/$var anywhere in the body) is what actually
            # distinguishes the two -- confirmed against every real
            # for-loop idiom-2 user in this directory.
            loop_ref_re = re.compile(
                r'-f\s*"\$OUT/(?:[A-Za-z0-9_]+/)*\$\{?' + re.escape(var) + r'\}?[^"]*"'
            )
            if not loop_ref_re.search(body):
                continue
            for item in word_list.split():
                item = item.strip("\"'")
                item = item.rsplit("/", 1)[-1]  # drop a leading path, e.g. "system/"
                # Strip a real archive/dylib/object suffix before judging
                # whether this is a plain short-name token -- half of the
                # 11 real scripts using this idiom write the loop's word
                # list as bare names (`libplatform`), the other half as
                # the full file they check for (`libplatform.a`), and
                # both are real, already-used conventions (found
                # 2026-09-11 fixing build_cron.sh's silently-dropped
                # dependency on build_libplatform.sh). Stripping is safe
                # even for non-library items like `sys____mmap.o`: the
                # resulting `build_sys____mmap.sh` candidate is simply
                # absent from script_set and dropped below.
                item = re.sub(r"\.(?:a|dylib|o)$", "", item)
                if not re.fullmatch(r"[A-Za-z0-9_]+", item):
                    continue  # not a plain short-name token (e.g. a var/glob)
                cand = f"build_{item}.sh"
                if cand != name and cand in script_set:
                    deps[name].add(cand)

    # First, find and drop only the specific edges that actually
    # participate in a cycle (DFS back-edges), rather than force-ready
    # any stuck node by name -- doing the latter previously discarded
    # real, non-cyclic edges of whichever node happened to sort first
    # (e.g. it put build_configd.sh ahead of its own real
    # build_configd_plugins.sh prerequisite merely because *some other*
    # unrelated pair was cyclic). A dropped edge here is a genuine
    # cycle -- always print it so a human can decide which direction is
    # the real one.
    WHITE, GRAY, BLACK = 0, 1, 2
    color = {s: WHITE for s in scripts}
    stack: list[str] = []
    dropped_edges: list[tuple[str, str]] = []

    def visit(u: str) -> None:
        color[u] = GRAY
        stack.append(u)
        for v in sorted(deps[u]):
            if color[v] == GRAY:
                dropped_edges.append((u, v))
                deps[u].discard(v)
            elif color[v] == WHITE:
                visit(v)
        stack.pop()
        color[u] = BLACK

    sys.setrecursionlimit(max(1000, len(scripts) * 4))
    for s in scripts:
        if color[s] == WHITE:
            visit(s)

    if dropped_edges:
        detail = ", ".join(f"{u} -> {v}" for u, v in dropped_edges)
        print(
            f"warning: {len(dropped_edges)} dependency-cycle edge(s) "
            f"dropped -- check these by hand (which direction is the "
            f"real prerequisite?): {detail}",
            file=sys.stderr,
        )

    # Now the graph is acyclic by construction -- plain Kahn's algorithm,
    # stable (alphabetical) tie-break so the order is reproducible run
    # to run, no further arbitrary breaking needed.
    ordered: list[str] = []
    remaining = {s: set(d) for s, d in deps.items()}
    while remaining:
        ready = sorted(s for s, d in remaining.items() if not d)
        assert ready, "graph should be acyclic after cycle-edge removal above"
        for s in ready:
            ordered.append(s)
            del remaining[s]
        for d in remaining.values():
            d.difference_update(ready)

    for s in ordered:
        print(s)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
