#!/bin/bash
# DARW-310: compile each IOAudioFamily kernel source with the real kernel iokit C++
# flags (taken from this tree\'s generated makefile) and report the errors, i.e.
# the gaps to close before vendoring. Same method as tools/ioaudio_gap_audit.sh.
# Usage: tools/ioaudio_gap_audit.sh [file.cpp ...]
set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

XNU="$REPO_ROOT/third_party/xnu-7195"
IOA="$REPO_ROOT/third_party/IOAudioFamily"
HID="$REPO_ROOT/third_party/IOHIDFamily"
PCI="$REPO_ROOT/third_party/IOPCIFamily"
OBJDIR="$XNU/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT"
SDK="$REPO_ROOT/build/xnu-latest-build/sdk-7195-ios/iPhoneOS.sdk"

for d in "$IOA"; do
	if [ ! -d "$d" ]; then
		echo "error: $d not found - run ./setup_third_party.sh" >&2
		exit 1
	fi
done
if [ ! -f "$OBJDIR/Makefile" ]; then
	echo "error: $OBJDIR/Makefile not found." >&2
	echo "       Run tools/build_xnu_7195.sh once first - this script reuses" >&2
	echo "       that build's generated makefile to get the real compile flags." >&2
	exit 1
fi

# ---------------------------------------------------------------------
# Extract the real per-file C++ command line from the kernel build.
#
# `make -n` on one already-built object prints the exact clang++ line xnu
# would use, with every -D, -W and -I already resolved. The env below is
# the same one tools/build_xnu_7195.sh passes (SDKROOT=iphoneos etc.);
# getting it wrong is not cosmetic -- SDKROOT=macosx would flip
# CONFIG_EMBEDDED/XNU_TARGET_OS_OSX and change struct layouts, which is
# the exact desync CLAUDE.md warns about.
#
# BUILD_WERROR=0 matches the real build too: without it xnu's -Weverything
# turns unrelated warnings in xnu's OWN headers into errors and buries the
# IOAudioFamily findings this script exists to report.
#
# CTFCONVERT/CTFMERGE are pointed at /bin/echo purely so the dry run does
# not shell out to a dtrace toolchain it does not need; nothing is built.
# ---------------------------------------------------------------------
PROBE_OBJ="BCM2837Mailbox.cpo"
PROBE_SRC="$XNU/iokit/Drivers/BCM2837/BCM2837Mailbox.cpp"
RAW="$(cd "$OBJDIR" && env -u DEVELOPER_DIR CTFCONVERT=/bin/echo CTFMERGE=/bin/echo \
	make -n "$PROBE_OBJ" \
		SRCROOT="$XNU" \
		OBJROOT="$XNU/BUILD/obj" \
		DSTROOT="$XNU/BUILD/dst" \
		SYMROOT="$XNU/BUILD/sym" \
		SDKROOT=iphoneos \
		PLATFORM=iPhoneOS \
		PLATFORMPATH=/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform \
		SDKVERSION=26.5 \
		ARCH_CONFIGS=ARM64 \
		MACHINE_CONFIGS=BCM2837 \
		KERNEL_CONFIGS=DEVELOPMENT \
		BUILD_WERROR=0 2>/dev/null | grep -- "-o $PROBE_OBJ" | head -1)"
if [ -z "$RAW" ]; then
	echo "error: could not extract a compile command from $OBJDIR" >&2
	exit 1
fi

# Turn that one-file command into a reusable one:
#  - drop -o/-MD/-MF/-MP and the probe source (we supply our own, to /dev/null)
#  - -DARM64_BOARD_CONFIG_BCM2837 -mcpu=cortex-a53 are xnu's own
#    MACHINE_FLAGS_ARM64_BCM2837 (makedefs/MakeInc.def:70); `make -n` on a
#    single object does not expand them, and without them osfmk/arm/proc_reg.h
#    fails with "Incompatible CPU type configured".
#  - the SDK's kernel include dir: the dry run names the live Xcode's SDK,
#    while the real build uses the staged copy under build/xnu-latest-build.
#  - EXPORT_HDRS/libsa is on xnu's include list but is not always created;
#    -Wmissing-include-dirs makes a nonexistent -I fatal, so drop it.
CC_LINE="$(python3 - "$RAW" "$PROBE_OBJ" "$PROBE_SRC" "$SDK" <<'PYEOF'
import sys, re, os
raw, obj, src, sdk = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
raw = raw.replace("-o %s " % obj, "")
raw = re.sub(r"\s-M[DP](?=\s)", " ", raw)
raw = re.sub(r"\s-MF\s+\S+", " ", raw)
# Strip the probe source by BASENAME, not by full path: SRCROOT may be
# reached through a symlink (third_party/ inside a git worktree is one),
# in which case the path make prints is not the one this script computed,
# the replace silently does nothing, and every compile then fails with
# "cannot specify -o when generating multiple output files".
# (No apostrophes in this heredoc: bash tracks quote state through a
# heredoc nested in a command substitution inside double quotes, and a
# lone one makes the whole script a syntax error hundreds of lines later.)
raw = re.sub(r"\s\S*%s\b" % re.escape(os.path.basename(src)), " ", raw)
raw = re.sub(r"-I\S+/SDKs/iPhoneOS[0-9.]*\.sdk/usr/local/include/kernel",
             "-I%s/usr/local/include/kernel" % sdk, raw)
raw = re.sub(r" -I\S+/EXPORT_HDRS/libsa ", " ", raw)
raw = raw.replace("-c ", "-c -DARM64_BOARD_CONFIG_BCM2837 -mcpu=cortex-a53 ", 1)
print(raw.strip())
PYEOF
)"

# Include paths IOAudioFamily itself needs, in the same shape its own
# Xcode project uses (IOAudioFamily.xcodeproj HEADER_SEARCH_PATHS:
# IOAudioFamilyFamily/IOKit/graphics/tl, IOAudioFamilyFamily, IONDRVSupport),
# plus:
#   compat/            -> IOKit/acpi/IOACPIPlatformExpert.h (see its header)
#   $XNU/iokit         -> IOKit/pwr_mgt/IOPMPrivate.h, IOKit/acpi/... (xnu
#                         in-tree private headers; this is xnu's own
#                         -I$(SRCROOT)/iokit, which the iokit component
#                         compiles with)
#   IOPCIFamily        -> IOKit/pci/IOPCIDevice.h (IODisplay.cpp:36)
#   IOHIDFamily        -> IOKit/hidsystem/IOHIDTypes.h (IOFramebufferShared.h:26)
#   $IOA, $IOA/GMetric, $IOA/GTrace/Kernel -> "GMetric/GMetric.hpp",
#                         "GTrace.hpp", "GMetricTypes.h"
IOG_INC=(
	-I"$XNU/iokit"
	-I"$IOA"
	-I"$IOA/PCMBlitterLib"
)

SOURCES=(
	$(ls "$IOA"/*.cpp)
)

if [ $# -gt 0 ]; then
	want=("$@")
	filtered=()
	for s in "${SOURCES[@]}"; do
		for w in "${want[@]}"; do
			[ "$(basename "$s")" = "$w" ] && filtered+=("$s")
		done
	done
	SOURCES=("${filtered[@]}")
	if [ ${#SOURCES[@]} -eq 0 ]; then
		echo "error: no known IOAudioFamily source matched: $*" >&2
		exit 1
	fi
fi

# The header/source fixes are commits on the anurodhp/IOAudioFamily fork
# (branch iokit-port, DAR-355), which setup_third_party.sh clones. They
# are no longer patched in place (tools/patch_ioaudio_gaps.sh is
# retired). To audit the pristine Apple drop, check out the fork's base
# commit 9d031bd (upstream tag IOAudioFamily-585; the fork itself has no tag by
# that name, so use the hash or `git fetch upstream tag IOAudioFamily-585`),
# then switch back to iokit-port afterwards.

LOGDIR="$(mktemp -d /tmp/ioaudio_gap_audit.XXXXXX)"
# IOA_AUDIT_KEEP_LOGS=1 keeps the per-file compiler logs (full
# diagnostics, not just the summary below) and prints where they are.
if [ -n "${IOA_AUDIT_KEEP_LOGS:-}" ]; then
	trap 'echo "logs kept in $LOGDIR"' EXIT
else
	trap 'rm -rf "$LOGDIR"' EXIT
fi

# Run the extracted command line through a generated script rather than
# expanding it inline. xnu's CFLAGS contain defines whose values hold
# spaces and parentheses -- e.g. -DCONFIG_NMBCLUSTERS="((1024 * 256) /
# MCLBYTES)" -- and an unquoted $CC_LINE splits those on whitespace,
# which makes every file fail with the same three bogus errors no matter
# what is in it. Letting bash parse the line as a line keeps the quoting.
# The sources include their own headers as <IOKit/audio/X.h>.
mkdir -p "$LOGDIR/inc/IOKit/audio"
ln -sf "$IOA"/*.h "$LOGDIR/inc/IOKit/audio/"
IOG_INC+=(-I"$LOGDIR/inc")
printf '#!/bin/bash\ncd %q\n%s "$@"\n' "$OBJDIR" "$CC_LINE" > "$LOGDIR/cc.sh"
# IOA_AUDIT_CC_OUT=<path>: also save the generated compiler wrapper
# there, for re-running one file by hand (e.g. with -H to see which
# header path wins). Takes the same -I/-include flags as below.
[ -n "${IOA_AUDIT_CC_OUT:-}" ] && cp "$LOGDIR/cc.sh" "$IOA_AUDIT_CC_OUT"

echo "=== IOAudioFamily gap audit (real kernel iokit C++ flags) ==="
echo "    source: $IOA  ($(cd "$IOA" && git describe --tags 2>/dev/null || echo 'unknown tag'))"
echo

total=0
clean=0
for src in "${SOURCES[@]}"; do
	name="$(basename "$src")"
	log="$LOGDIR/$name.log"
	bash "$LOGDIR/cc.sh" "${IOG_INC[@]}" -o /dev/null "$src" > "$log" 2>&1 || true
	n=$(grep -c "error:" "$log" || true)
	total=$((total + n))
	if [ "$n" -eq 0 ]; then
		clean=$((clean + 1))
		printf '%-34s %s\n' "$name" "compiles clean"
	else
		printf '%-34s %s\n' "$name" "$n errors"
		grep "error:" "$log" \
			| sed 's/.*error: //' \
			| sort | uniq -c | sort -rn \
			| sed 's/^/        /'
	fi
done

echo
echo "=== $clean/${#SOURCES[@]} sources compile clean; $total errors total ==="
