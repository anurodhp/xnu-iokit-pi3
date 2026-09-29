#!/bin/bash
# Generates config.h (or pathnames.h -- same template style) for third_party/sudo from its real config.h.in plus
# sudo_compat/sudo_config_darwin.def (DAR-340). Usage:
#   gen_sudo_config_h.sh <config.h.in> <darwin.def> <out config.h>
# Hard-fails if the .def names anything config.h.in does not declare, so
# a typo can never silently become "undefined". A value of `<empty>` emits
# `#define NAME ` (empty expansion).
set -e
IN="$1"; DEF="$2"; OUT="$3"
[ -f "$IN" ] && [ -f "$DEF" ] && [ -n "$OUT" ] || {
    echo "usage: $0 config.h.in darwin.def out.h" >&2; exit 2; }
awk -v deffile="$DEF" '
BEGIN {
    while ((getline line < deffile) > 0) {
        if (line ~ /^[[:space:]]*(#|$)/) continue
        n = index(line, " ")
        name = (n ? substr(line, 1, n - 1) : line)
        val  = (n ? substr(line, n + 1) : "1")
        if (val == "<empty>") val = ""   # explicit empty expansion (e.g. PUTENV_CONST)
        if (name in v) { printf("error: %s defined twice in %s\n", name, deffile) > "/dev/stderr"; bad = 1 }
        v[name] = val; want[name] = 1
    }
}
# config.h.in style: `#undef X`; pathnames.h.in style: `# undef X` (inside
# an #ifndef X guard). Both keep their own indentation.
/^#[[:space:]]*undef[[:space:]]+[A-Za-z_]/ {
    pre = $0; sub(/undef.*/, "", pre)
    name = $0; sub(/^#[[:space:]]*undef[[:space:]]+/, "", name); sub(/[[:space:]].*$/, "", name)
    if (name in v) { print pre "define " name " " v[name]; delete want[name]; next }
    print "/* " $0 " */"; next
}
{ print }
END {
    for (k in want) { printf("error: %s is in %s but not in config.h.in\n", k, deffile) > "/dev/stderr"; bad = 1 }
    exit bad
}' "$IN" > "$OUT"
echo "wrote $OUT ($(grep -c '^#define' "$OUT") defines)"
