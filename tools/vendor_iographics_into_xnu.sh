#!/bin/bash
#
# Vendors real Apple IOGraphics (third_party/IOGraphics, IOGraphics-585,
# macos-112 manifest pin) into the kernel source tree so its kernel half
# is COMPILED INTO the kernel -- same shape and same reasons as
# tools/vendor_usb_into_xnu.sh (read that header first; the copy-not-
# include-path rationale and the divergence guard are identical):
#
#   third_party/xnu-7195/iokit/Graphics/IOGraphicsFamily/  <- 9 .cpp + private headers
#   third_party/xnu-7195/iokit/Graphics/GMetric/           <- GMetric.cpp/.hpp/Types.h
#   third_party/xnu-7195/iokit/Graphics/GTrace/Kernel/     <- GTrace.cpp/.hpp
#   third_party/xnu-7195/iokit/Graphics/tl/                <- osmemory.cpp
#   third_party/xnu-7195/iokit/Graphics/compat/            <- our force-included gap headers
#   third_party/xnu-7195/iokit/Graphics/xxhash/            <- upstream xxhash.h + limits.h redirect
#   third_party/xnu-7195/iokit/IOKit/graphics/(tl/)        <- public IOGraphics headers
#   third_party/xnu-7195/iokit/IOKit/i2c/                  <- IOI2CInterface headers
#   third_party/xnu-7195/iokit/IOKit/ndrvsupport/          <- IONDRVSupport headers (headers only)
#   third_party/xnu-7195/iokit/IOKit/hidsystem/IOHIDTypes.h
#   third_party/xnu-7195/iokit/IOKit/acpi/IOACPIPlatformExpert.h (ours)
#   + one line per source in iokit/conf/files
#   + a per-object flags block in iokit/conf/Makefile.template
#
# The directory layout mirrors Apple's repo on purpose: the sources reach
# each other with quoted, repo-relative includes ("GMetric/GMetric.hpp",
# "GTrace.hpp", "IOGraphicsKTrace.h"), and <IOKit/...> resolves through
# INCFLAGS_GEN's -I$(SRCROOT)/iokit (makedefs/MakeInc.def:792).
#
# WHY PER-OBJECT FLAGS, NOT KERNEL-WIDE. Everything IOGraphics needs that
# the rest of the kernel must not see -- Apple's own GTRACE_IMPL (set in
# all three IOGraphicsFamily configs; IOG_GMETRIC is Development-only and
# not set, see tools/iographics_gap_audit.sh), the two
# force-included gap headers, the <limits.h> redirect for xxhash.h, and
# the tl/ angle-bracket include dir -- goes on exactly these 12 objects
# via xnu's own <obj>.cpo_CFLAGS_ADD hook
# (makedefs/MakeInc.rule:375, P_RULE_1A), keyed on the .cpo name. IONetworkingFamily's
# __PRIVATE_SPI__ went kernel-wide (Makefile.template:21) and needed a
# paragraph defending that; this avoids needing one.
#
# SOURCE FIXES are not in this script. They are commits on this
# project's fork, anurodhp/IOGraphics branch iokit-port (IOGraphics-585
# plus one commit per fix, each with its citation in the commit message;
# DAR-355), which setup_third_party.sh clones. They used to be applied in
# place by tools/patch_iographics_gaps.sh, which is now retired. This
# script only checks that the checkout is the fork.
# tools/iographics_gap_audit.sh compiles the same tree with the same flags
# and is the fast pre-check.
#
# NOT HANDLED HERE, and needed for the family to actually run:
#   - IOGraphicsFamilyModuleStart() (IOFramebuffer.cpp:1978) is the kext's
#     MODULE_START (project.pbxproj:2188). Built in, nothing calls it, and
#     it creates gIOFBSystemWorkLoop and the global framebuffer arrays.
#     iokit/BCM2837ArmIO/BCM2837Personalities.cpp calls it, immediately
#     before publishing the IOGraphics personalities -- the same order the
#     kext loader uses.
#   - Its DEBG_CATEGORIES_BUILD sysctls (IOFramebuffer.cpp:362-366,
#     1990-1997) are SYSCTL_QUAD linker-set entries that ModuleStart ALSO
#     registers by hand: built in, that is vendor_usb_into_xnu.sh fix 1d's
#     double-registration boot panic. Not reachable: DEBG_CATEGORIES_BUILD
#     is 0 in Apple's shipping configs (project.pbxproj:2038,2067) and is
#     never defined here, so the whole block compiles out. The guard at
#     the bottom of this script fails the vendoring if that ever changes.
#
# IMPORTANT: the copies under third_party/xnu-7195 are GENERATED. Fix
# Apple's source with a commit on anurodhp/IOGraphics (iokit-port) and
# our headers under compat/. Never edit the vendored copy.
#
# Run directly, or let tools/build_xnu_7195.sh call it (it does).
# Idempotent.

set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

XNU="third_party/xnu-7195"
IOG="third_party/IOGraphics"
HID="third_party/IOHIDFamily"
XXH="third_party/xxHash"
LZF="third_party/lzfse/src"
CONF="$XNU/iokit/conf/files"
MKT="$XNU/iokit/conf/Makefile.template"

if [ ! -d "$XNU/iokit/conf" ]; then
	echo "error: $XNU not found - run ./setup_third_party.sh" >&2
	exit 1
fi
for f in "$IOG/IOGraphicsFamily/IOFramebuffer.cpp" \
	 "$HID/IOHIDSystem/IOKit/hidsystem/IOHIDTypes.h" \
	 "$XXH/xxhash.h" \
	 "$LZF/lzvn_decode_base.c"; do
	if [ ! -f "$f" ]; then
		echo "error: $f not found - run ./setup_third_party.sh" >&2
		exit 1
	fi
done
if ! grep -q "XXH_NO_STDLIB" "$XXH/xxhash.h"; then
	echo "error: $XXH/xxhash.h has no XXH_NO_STDLIB -- pinned below v0.8.2?" >&2
	echo "       compat/IOKit/graphics/IOGraphicsXXHash.h needs it; see setup_third_party.sh." >&2
	exit 1
fi

# Apple's sources must already carry our fixes, i.e. come from the
# anurodhp/IOGraphics fork (DAR-355). Check the first and last fix
# commits' markers so a plain upstream clone fails here, not with a
# dozen compile errors later.
if ! grep -q "void sysAssertGated(void);" "$IOG/IOGraphicsFamily/IOKit/graphics/IOFramebufferPrivate.h" ||
   ! grep -q "IOReturn IOFramebuffer::diagnoseReport(" "$IOG/IOGraphicsFamily/IOFramebuffer.cpp"; then
	echo "error: $IOG lacks this project's fixes -- it is not the" >&2
	echo "       anurodhp/IOGraphics iokit-port fork. Re-clone it:" >&2
	echo "       rm -rf $IOG && ./setup_third_party.sh" >&2
	exit 1
fi

G="$XNU/iokit/Graphics"
G_FAM="$G/IOGraphicsFamily"
G_GM="$G/GMetric"
G_GT="$G/GTrace/Kernel"
G_TL="$G/tl"
G_COMPAT="$G/compat"
G_XXH="$G/xxhash"
G_LZVN="$G/lzvn"
G_LZVN_K="$G/lzvn/kernel_compat"
H_GFX="$XNU/iokit/IOKit/graphics"
H_TL="$H_GFX/tl"
H_I2C="$XNU/iokit/IOKit/i2c"
H_NDRV="$XNU/iokit/IOKit/ndrvsupport"
H_HID="$XNU/iokit/IOKit/hidsystem"
H_ACPI="$XNU/iokit/IOKit/acpi"

mkdir -p "$G_FAM" "$G_GM" "$G_GT" "$G_TL" "$G_COMPAT" "$G_XXH" "$G_LZVN_K" \
	 "$H_TL" "$H_I2C" "$H_NDRV" "$H_HID" "$H_ACPI"

# Same divergence guard as vendor_usb_into_xnu.sh (DAR-299): a vendored
# file that differs from its source AND has uncommitted state in the xnu
# tree may hold real work, so it is not overwritten.
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
		if check_divergence "$dst/$(basename "$f")" "$f"; then
			cp "$f" "$dst/$(basename "$f")"
		fi
	done
}

# The 12 kernel-side sources -- exactly tools/iographics_gap_audit.sh's
# SOURCES list. IONDRVSupport's own .cpp files are not built: they drive
# PCI NDRV (Open Firmware) display ROMs, which this board cannot have.
FAM_SRCS="IOFramebuffer IOBootFramebuffer IOFramebufferUserClient
	IODisplayWrangler IODisplayWranglerUserClients IODisplay
	IOGraphicsDevice IOI2CInterface IOBacklightDisplay"
for s in $FAM_SRCS; do
	copy_into "$G_FAM" "$IOG/IOGraphicsFamily/$s.cpp"
done
copy_into "$G_FAM" "$IOG/IOGraphicsFamily"/*.h "$IOG/IOGraphicsFamily"/*.hpp
copy_into "$G_GM" "$IOG/GMetric/GMetric.cpp" "$IOG/GMetric/GMetric.hpp" "$IOG/GMetric/GMetricTypes.h"
copy_into "$G_GT" "$IOG/GTrace/Kernel/GTrace.cpp" "$IOG/GTrace/Kernel/GTrace.hpp"
copy_into "$G_TL" "$IOG/IOGraphicsFamily/IOKit/graphics/tl/osmemory.cpp"

# Public headers.
copy_into "$H_GFX" "$IOG/IOGraphicsFamily/IOKit/graphics"/*.h "$IOG/IOGraphicsFamily/IOKit/graphics"/*.hpp
for f in "$IOG/IOGraphicsFamily/IOKit/graphics/tl"/*; do
	case "$f" in *.cpp) continue ;; esac
	copy_into "$H_TL" "$f"
done
copy_into "$H_I2C" "$IOG/IOGraphicsFamily/IOKit/i2c"/*.h
copy_into "$H_NDRV" "$IOG/IONDRVSupport/IOKit/ndrvsupport"/*.h
# One header from IOHIDFamily: IOFramebufferShared.h:26 includes it for the
# IOGPoint/IOGBounds cursor types (see setup_third_party.sh's IOHIDFamily note).
copy_into "$H_HID" "$HID/IOHIDSystem/IOKit/hidsystem/IOHIDTypes.h"
copy_into "$H_ACPI" compat/IOKit/acpi/IOACPIPlatformExpert.h

# Our gap headers. IOGraphicsXXHash.h is reached as
# <IOKit/graphics/IOGraphicsXXHash.h> (included by IODisplay.cpp on the fork);
# the other two are force-included, see the Makefile.template block.
copy_into "$H_GFX" compat/IOKit/graphics/IOGraphicsXXHash.h
copy_into "$G_COMPAT" \
	compat/IOKit/graphics/IOGraphicsTypesPrivateMissing.h \
	compat/IOKit/graphics/IOBacklightDisplayTraceMissing.h
copy_into "$G_XXH" "$XXH/xxhash.h" compat/xxhash_kernel/limits.h

# LZVN: Apple's real decoder from lzfse (unmodified) + our 4-argument
# lzvn_decode() wrapper for IOFramebuffer.cpp:2317/2390. See
# compat/IOKit/graphics/IOGraphicsLZVN.c for the wrapper and the rename.
copy_into "$G_LZVN" "$LZF/lzvn_decode_base.c" "$LZF/lzvn_decode_base.h" \
	"$LZF/lzfse_internal.h" "$LZF/lzfse_fse.h" "$LZF/lzfse_tunables.h" \
	compat/IOKit/graphics/IOGraphicsLZVN.c
copy_into "$G_LZVN_K" compat/lzvn_kernel/assert.h compat/lzvn_kernel/stdlib.h \
	compat/lzvn_kernel/limits.h

if [ ${#diverged[@]} -ne 0 ]; then
	echo "error: these vendored files differ from their source AND have" >&2
	echo "       uncommitted edits in the XNU tree, so the XNU-tree copy may" >&2
	echo "       hold real work. Refusing to overwrite:" >&2
	for f in "${diverged[@]}"; do
		echo "         git -C $XNU diff -- $f" >&2
	done
	echo "       Move the fix into anurodhp/IOGraphics (iokit-port) or compat/, then re-run." >&2
	exit 1
fi

# The sysctl double-registration panic (see header) is only reachable with
# DEBG_CATEGORIES_BUILD nonzero. Fail here, not 26 seconds into a boot, if
# anything ever starts defining it.
if grep -rqs "DEBG_CATEGORIES_BUILD" "$MKT" "$G_COMPAT"; then
	echo "error: DEBG_CATEGORIES_BUILD is being defined for the kernel build." >&2
	echo "       IOGraphicsFamilyModuleStart() would then sysctl_register_oid() two" >&2
	echo "       SYSCTL_QUAD oids __sysctl_set already registered -> boot panic" >&2
	echo "       (vendor_usb_into_xnu.sh fix 1d). Remove those calls first." >&2
	exit 1
fi

# ---------------------------------------------------------------------
# iokit/conf/files entries: one block at the end of the file, after the
# USB block. Regenerated on every run (everything from the marker to the
# next blank line), so adding a source here cannot be silently skipped on
# an already-vendored tree.
# ---------------------------------------------------------------------
IOG_OBJS=""
LZVN_OBJS="lzvn_decode_base.o IOGraphicsLZVN.o"
MARKER="# --- Graphics (IOGraphics-585), vendored by tools/vendor_iographics_into_xnu.sh ---"
CONFBLOCK="$(mktemp)"
{
	echo "$MARKER"
	for s in $FAM_SRCS; do
		printf 'iokit/Graphics/IOGraphicsFamily/%s.cpp\t\toptional iokitcpp\n' "$s"
	done
	printf 'iokit/Graphics/GMetric/GMetric.cpp\t\toptional iokitcpp\n'
	printf 'iokit/Graphics/GTrace/Kernel/GTrace.cpp\t\toptional iokitcpp\n'
	printf 'iokit/Graphics/tl/osmemory.cpp\t\toptional iokitcpp\n'
	printf 'iokit/Graphics/lzvn/lzvn_decode_base.c\t\toptional iokitcpp\n'
	printf 'iokit/Graphics/lzvn/IOGraphicsLZVN.c\t\toptional iokitcpp\n'
} > "$CONFBLOCK"
python3 - "$CONF" "$CONFBLOCK" "$MARKER" <<'PYEOF2'
import sys
conf, blockf, marker = sys.argv[1:4]
lines = open(conf).read().split("\n")
block = open(blockf).read().rstrip("\n").split("\n")
if marker in lines:
    a = lines.index(marker)
    b = a
    while b < len(lines) and lines[b].strip() != "":
        b += 1
    lines = lines[:a] + block + lines[b:]
else:
    while lines and lines[-1] == "":
        lines.pop()
    lines += [""] + block + [""]
open(conf, "w").write("\n".join(lines))
PYEOF2
rm -f "$CONFBLOCK"

missing=()
for f in "$G_LZVN"/*.c; do
	rel="${f#$XNU/}"
	LC_ALL=C grep -q "^$rel[[:space:]]" "$CONF" || missing+=("$rel")
done
for f in "$G_FAM"/*.cpp "$G_GM"/*.cpp "$G_GT"/*.cpp "$G_TL"/*.cpp; do
	rel="${f#$XNU/}"
	LC_ALL=C grep -q "^$rel[[:space:]]" "$CONF" || missing+=("$rel")
	# C++ objects are "<name>.cpo" in this build (MakeInc.rule:371), and the
	# per-object hooks are keyed on that name -- e.g. libkern/conf/
	# Makefile.template:66's OSKext.cpo_CXXWARNFLAGS_ADD. A ".o" key is
	# accepted by make and silently applies to nothing.
	IOG_OBJS="$IOG_OBJS $(basename "${f%.cpp}").cpo"
done
if [ ${#missing[@]} -ne 0 ]; then
	echo "error: vendored but not listed in $CONF (would never compile):" >&2
	printf '  %s\n' "${missing[@]}" >&2
	exit 1
fi

# Object basenames must be unique kernel-wide: xnu names every object by
# basename alone, so a clash would silently build one and not the other.
for o in $IOG_OBJS $LZVN_OBJS; do
	src="${o%.cpo}"; src="${src%.o}"
	n=$(LC_ALL=C grep -cE "/$src\.(c|cpp|s)[[:space:]]" "$XNU"/*/conf/files || true)
	n=$(echo "$n" | awk -F: '{s+=$NF} END {print s+0}')
	if [ "$n" != 1 ]; then
		echo "error: object name $o is listed $n times across */conf/files" >&2
		exit 1
	fi
done

# Our extra -I dirs are searched BEFORE the kernel's own INCFLAGS (see the
# Makefile block below), so a header in one of them with the same bare
# name as a kernel header would silently replace it for these objects.
# lzvn/kernel_compat deliberately holds assert.h/stdlib.h/limits.h, which
# is only safe because the kernel has no bare-name copies of them.
# Checked on every run, against the kernel include roots that exist.
shadowed=()
for d in "$G" "$G_FAM" "$G_GM" "$G_GT" "$H_TL" "$G_XXH" "$G_LZVN" "$G_LZVN_K"; do
	for h in "$d"/*; do
		[ -f "$h" ] || continue
		b="$(basename "$h")"
		case "$b" in *.c|*.cpp) continue ;; esac
		for r in "$XNU/iokit" "$XNU/iokit/Network" "$XNU/bsd" "$XNU/osfmk" \
			 "$XNU/EXTERNAL_HEADERS" "$XNU"/BUILD/obj/EXPORT_HDRS \
			 "$XNU"/BUILD/obj/EXPORT_HDRS/*; do
			[ -f "$r/$b" ] && shadowed+=("$b: ${d#$XNU/} shadows ${r#$XNU/}")
		done
	done
done
if [ ${#shadowed[@]} -ne 0 ]; then
	echo "error: a vendored header would shadow a kernel header of the same name:" >&2
	printf '  %s\n' "${shadowed[@]}" >&2
	exit 1
fi

# ---------------------------------------------------------------------
# iokit/conf/Makefile.template: per-object flags for exactly these objects.
# Inserted before the %RULES marker so it is part of the generated
# Makefile's variable section. Rewritten between its markers on every run,
# so the object list and flags cannot drift from this script.
# ---------------------------------------------------------------------
BEGIN="# BEGIN IOGraphics per-object flags -- generated by tools/vendor_iographics_into_xnu.sh, do not edit"
END="# END IOGraphics per-object flags"
BLOCK="$(mktemp)"
trap 'rm -f "$BLOCK"' EXIT
{
	echo "$BEGIN"
	echo "#"
	echo "# GTRACE_IMPL: Apple's IOGraphics.xcodeproj defines it in all three"
	echo "# IOGraphicsFamily configs (project.pbxproj:2140,2207,2262). The -include'd"
	echo "# headers and the extra -I dirs: see tools/vendor_iographics_into_xnu.sh."
	echo "IOGRAPHICS_OBJS =$IOG_OBJS"
	echo "IOGRAPHICS_CFLAGS = -DGTRACE_IMPL \\"
	echo "	-include \$(SRCROOT)/iokit/Graphics/compat/IOGraphicsTypesPrivateMissing.h \\"
	echo "	-include \$(SRCROOT)/iokit/Graphics/compat/IOBacklightDisplayTraceMissing.h"
	echo "IOGRAPHICS_INCFLAGS = -I\$(SRCROOT)/iokit/Graphics \\"
	echo "	-I\$(SRCROOT)/iokit/Graphics/IOGraphicsFamily \\"
	echo "	-I\$(SRCROOT)/iokit/Graphics/GMetric \\"
	echo "	-I\$(SRCROOT)/iokit/Graphics/GTrace/Kernel \\"
	echo "	-I\$(SRCROOT)/iokit/IOKit/graphics/tl \\"
	echo "	-I\$(SRCROOT)/iokit/Graphics/xxhash"
	# The -I dirs go in _CFLAGS_ADD, NOT the <obj>_INCFLAGS hook. P_RULE_1A
	# ends with ${$@_INCFLAGS} and the generated rule appends the source
	# path to it with no space (MakeInc.rule:375 + the generated
	# "${P_RULE_1A}$(SOURCE_DIR)/..." line), so the last -I and the .cpp
	# fuse into one token and clang reports "no input files". Nothing in
	# xnu itself uses _INCFLAGS, which is how that went unnoticed.
	# Consequence: these dirs precede the kernel's own INCFLAGS; checked
	# that no header in them shares a bare name with one on the kernel's
	# include path.
	echo "\$(foreach o,\$(IOGRAPHICS_OBJS),\$(eval \$(o)_CFLAGS_ADD += \$(IOGRAPHICS_CFLAGS) \$(IOGRAPHICS_INCFLAGS)))"
	echo "#"
	echo "# LZVN (C objects, so .o): lzfse's state decoder is renamed so the"
	echo "# 4-argument lzvn_decode() IOGraphics calls can exist; see"
	echo "# iokit/Graphics/lzvn/IOGraphicsLZVN.c."
	echo "LZVN_OBJS = $LZVN_OBJS"
	echo "LZVN_CFLAGS = -Dlzvn_decode=lzfse_lzvn_decode_state \\"
	echo "	-I\$(SRCROOT)/iokit/Graphics/lzvn \\"
	echo "	-I\$(SRCROOT)/iokit/Graphics/lzvn/kernel_compat"
	echo "\$(foreach o,\$(LZVN_OBJS),\$(eval \$(o)_CFLAGS_ADD += \$(LZVN_CFLAGS)))"
	echo "$END"
} > "$BLOCK"
python3 - "$MKT" "$BLOCK" "$BEGIN" "$END" <<'PYEOF'
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
if [ "$(grep -c "^$BEGIN$" "$MKT")" != 1 ]; then
	echo "error: IOGraphics flags block did not apply exactly once to $MKT" >&2
	exit 1
fi

echo "=== IOGraphics vendored into $XNU ==="
echo "  objects:$IOG_OBJS"
