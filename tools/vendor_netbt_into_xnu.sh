#!/bin/bash
#
# Vendors NetBSD's Bluetooth protocol stack (sys/netbt from our fork
# anurodhp/netbsd-src, branch iokit-port; docs/bluetooth-stack-plan.md S2)
# into the kernel source tree, with this repo's xnu glue and NetBSD-API
# compatibility layer (compat/netbt/):
#
#   bsd/netbt/          <- third_party/netbsd-src/sys/netbt/*.c *.h, unchanged
#   bsd/netbt/xnu/      <- compat/netbt/*.c *.h (glue: domain, protosw and
#                          pr_usrreqs wrappers, mbuf/socket/callout/... maps)
#   bsd/netbt/xnu/sys/  <- compat/netbt/sys/*.h (NetBSD headers xnu lacks:
#                          sys/kmem.h, sys/callout.h, ...; found only through
#                          the netbt objects' own -I, so nothing else sees them)
#
# Same rules as tools/vendor_iohid_into_xnu.sh:
#   - a copy, not an include path, so the xnu tree builds on its own;
#   - fixes to netbt itself are commits on anurodhp/netbsd-src iokit-port,
#     never edits in the xnu tree: a vendored file that differs from its
#     source AND has uncommitted edits in xnu stops the script;
#   - bsd/conf/files and bsd/conf/Makefile.template get a generated block
#     (between markers) listing the objects and their flags: every netbt
#     object is compiled with -include xnu/netbt_compat.h and -I to
#     bsd/netbt/xnu, so NetBSD's <sys/kmem.h> etc. resolve to the compat
#     redirects and nothing outside netbt is affected.
#
# netbt's own debug printing is compiled in (BLUETOOTH_DEBUG) and off unless
# the kernel boots with netbt-debug=N (netbt_xnu.c).
#
# Called by tools/build_xnu_7195.sh. Gated in the kernel by boot-arg bt=1 at
# run time (BCM43430Bluetooth attaches the controller); the domain itself
# registers unconditionally, like the other built-in domains.
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"
XNU="$REPO_ROOT/third_party/xnu-7195"
SRC="$REPO_ROOT/third_party/netbsd-src/sys/netbt"
COMPAT="$REPO_ROOT/compat/netbt"
[ -d "$SRC" ] || { echo "error: $SRC not found -- run ./setup_third_party.sh" >&2; exit 1; }
[ -d "$XNU/bsd" ] || { echo "error: $XNU not found" >&2; exit 1; }

D="$XNU/bsd/netbt"
mkdir -p "$D/xnu/sys"

diverged=()
check_divergence() {
	local dst="$1" src="$2"
	[ -e "$dst" ] || return 0
	cmp -s "$src" "$dst" && return 0
	local rel="${dst#$XNU/}"
	if [ -n "$(git -C "$XNU" status --porcelain -- "$rel" 2>/dev/null)" ]; then
		diverged+=("$rel")
		return 1
	fi
}
copy_into() {
	local dst="$1"; shift
	local f
	for f in "$@"; do
		[ -e "$f" ] || continue
		if check_divergence "$dst/$(basename "$f")" "$f"; then
			cp "$f" "$dst/$(basename "$f")"
		fi
	done
}

# The kernel sources: everything in sys/netbt except bt_proto.c, NetBSD's
# domain/protosw table and module glue, which compat/netbt/netbt_xnu.c
# replaces with xnu ones (it is still copied, for reference).
NETBT_SRCS=$(cd "$SRC" && ls *.c | grep -v '^bt_proto\.c$' | sed 's/\.c$//' | sort)
copy_into "$D" "$SRC"/*.c "$SRC"/*.h
copy_into "$D/xnu" "$COMPAT"/*.c "$COMPAT"/*.h
copy_into "$D/xnu/sys" "$COMPAT"/sys/*.h
GLUE_SRCS=$(cd "$COMPAT" && { ls *.c 2>/dev/null || true; } | sed 's/\.c$//' | sort)

if [ ${#diverged[@]} -ne 0 ]; then
	echo "error: these vendored files differ from their source AND have" >&2
	echo "       uncommitted edits in the XNU tree. Refusing to overwrite:" >&2
	for f in "${diverged[@]}"; do echo "         git -C $XNU diff -- $f" >&2; done
	echo "       Move the fix into anurodhp/netbsd-src (iokit-port) or compat/netbt/, then re-run." >&2
	exit 1
fi

# --- bsd/conf/files block ---
CONF="$XNU/bsd/conf/files"
BEGIN_F="# --- netbt (NetBSD Bluetooth), vendored by tools/vendor_netbt_into_xnu.sh ---"
END_F="# --- end netbt ---"
BLOCK="$(mktemp)"
{
	echo "$BEGIN_F"
	for s in $NETBT_SRCS; do printf 'bsd/netbt/%s.c\t\t\toptional sockets\n' "$s"; done
	for s in $GLUE_SRCS; do printf 'bsd/netbt/xnu/%s.c\t\t\toptional sockets\n' "$s"; done
	echo "$END_F"
} > "$BLOCK"
python3 - "$CONF" "$BLOCK" "$BEGIN_F" "$END_F" <<'PYEOF'
import sys
path, blockf, begin, end = sys.argv[1:5]
s = open(path).read()
block = open(blockf).read()
if begin in s:
    a = s.index(begin)
    b = s.index(end, a) + len(end) + 1
    s = s[:a] + block + s[b:]
else:
    if not s.endswith("\n"):
        s += "\n"
    s += "\n" + block
open(path, "w").write(s)
PYEOF

# --- bsd/conf/Makefile.template flags block ---
MKT="$XNU/bsd/conf/Makefile.template"
BEGIN_M="# --- netbt flags (tools/vendor_netbt_into_xnu.sh) ---"
END_M="# --- end netbt flags ---"
{
	echo "$BEGIN_M"
	printf 'NETBT_OBJS ='
	for s in $NETBT_SRCS $GLUE_SRCS; do printf ' %s.o' "$s"; done
	echo
	echo "NETBT_CFLAGS = -I\$(SRCROOT)/bsd/netbt/xnu -include \$(SRCROOT)/bsd/netbt/xnu/netbt_compat.h \\"
	echo "	-D_NETBT_XNU -DBLUETOOTH_DEBUG -Wno-error -Wno-everything"
	echo "\$(foreach o,\$(NETBT_OBJS),\$(eval \$(o)_CFLAGS_ADD += \$(NETBT_CFLAGS)))"
	echo "$END_M"
} > "$BLOCK"
python3 - "$MKT" "$BLOCK" "$BEGIN_M" "$END_M" <<'PYEOF'
import sys
mkt, blockf, begin, end = sys.argv[1:5]
s = open(mkt).read()
block = open(blockf).read()
if begin in s:
    a = s.index(begin)
    b = s.index(end, a) + len(end) + 1
    s = s[:a] + block + s[b:]
else:
    anchor = "\n%RULES\n"
    if s.count(anchor) != 1:
        sys.exit("error: %RULES anchor not found exactly once in " + mkt)
    s = s.replace(anchor, "\n" + block + anchor)
open(mkt, "w").write(s)
PYEOF
rm -f "$BLOCK"

# Object names must be unique across bsd/ (they share one obj directory).
for s in $NETBT_SRCS $GLUE_SRCS; do
	n=$(grep -E "/$s\.c[[:space:]]" "$CONF" | wc -l | tr -d ' ')
	if [ "$n" != 1 ]; then
		echo "error: object name $s.o is used by $n files in $CONF" >&2
		exit 1
	fi
done

# Commit the vendored state inside the xnu fork, as sync_iokit_drivers.sh
# does (its header explains why): the divergence check above is only a real
# signal of a hand edit if the vendored files are normally committed.
if [ -n "$(git -C "$XNU" status --porcelain -- bsd/netbt bsd/conf/files bsd/conf/Makefile.template)" ]; then
	git -C "$XNU" add -- bsd/netbt bsd/conf/files bsd/conf/Makefile.template
	git -C "$XNU" commit -q -m "netbt: vendored by tools/vendor_netbt_into_xnu.sh" \
		-m "NetBSD sys/netbt from anurodhp/netbsd-src iokit-port, glue from compat/netbt." \
		-- bsd/netbt bsd/conf/files bsd/conf/Makefile.template
fi

echo "=== netbt vendored into $XNU/bsd/netbt ($(echo $NETBT_SRCS | wc -w | tr -d ' ') netbt + $(echo $GLUE_SRCS | wc -w | tr -d ' ') glue objects) ==="
