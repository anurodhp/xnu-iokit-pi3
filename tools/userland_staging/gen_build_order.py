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
     once) whose body checks `$OUT/$a.a` and references `build_${a}.sh`
     in its own error text -- here the dependency is on
     `build_<item>.sh` for every item in the loop's word list, not on
     any literal script name appearing in the source text.

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

        for match in PREREQ_RE.findall(text):
            if match != name and match in script_set:
                deps[name].add(match)

        for var, word_list, body in FOR_LOOP_RE.findall(text):
            loop_ref_re = re.compile(r"build_\$\{?" + re.escape(var) + r"\}?\.sh")
            if not loop_ref_re.search(body):
                continue
            for item in word_list.split():
                item = item.strip("\"'")
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
