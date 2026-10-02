#!/bin/bash
# stage_gnustep_mac_layout.sh <gnustep stage root> <out dir>
#
# Rearranges the GNUstep tree that build_gnustep_*.sh install (gnustep-make's
# "gnustep" layout under /usr/GNUstep, libc_build/gnustep/root) into the
# Mac layout of third_party/gnustep-make/FilesystemLayouts/mac, as a new
# tree in <out dir> that inject_into_sd_image.sh ditto's onto the image:
#
#   /usr/GNUstep/System/Applications/X.app  -> /Applications/X.app
#           (Applications/Admin, if any     -> /Applications/Utilities)
#   /usr/GNUstep/System/Library             -> /System/Library
#           (its Documentation              -> /Library/Documentation)
#   /usr/GNUstep/System/Tools               -> /usr/local/bin
#           (Tools/Admin, if any            -> /usr/local/sbin)
#   /etc/GNUstep/GNUstep.conf               the mac layout's GNUSTEP_MAKEFILES
#           and SYSTEM/NETWORK/LOCAL path keys; user keys are left alone.
#
# The libraries are NOT rebuilt. Their install names (and every
# LC_LOAD_DYLIB naming them: /usr/GNUstep/System/Library/Libraries/./
# libgnustep-base.1.31.dylib, .../Frameworks/FSNode.framework/Versions/1/
# ./FSNode, ...) stay the old paths, and resolve through the compatibility
# symlinks left at the old locations:
#   /usr/GNUstep/System/Applications -> ../../../Applications
#   /usr/GNUstep/System/Library      -> ../../../System/Library
#   /usr/GNUstep/System/Tools        -> ../../local/bin
# (relative, so the staged tree is self-consistent on the build host too).
# No image carries LC_RPATH. Every image refers to each library by the same
# install-name string, so dyld sees one path per library.
#
# What reads the new paths at run time: gnustep-base reads every path key
# from /etc/GNUstep/GNUstep.conf when the process starts
# (third_party/gnustep-base/Source/NSPathUtilities.m: GNUstepConfig()
# :903-1018 parses GNUSTEP_TARGET_CONFIG_FILE, InitialisePathUtilities()
# :1195 calls ExtractValuesFromConfig(), which assigns gnustepSystemApps
# from GNUSTEP_SYSTEM_APPS at :447-450; the compiled-in /usr/GNUstep values
# are only a fallback for keys the file lacks, ASSIGN_DEFAULT_PATH :704.
# NSSearchPathForDirectoriesInDomains(NSApplicationDirectory) returns
# gnustepSystemApps for the system domain, :2240-2246). openapp, opentool
# and gnustep-config source the same file (their baked GNUSTEP_MAKEFILES is
# used only when the file does not set it).
#
# Sourced by nobody; run by inject_into_sd_image.sh. Not named build_*.sh,
# so gen_build_order.py does not treat it as a build step.
set -euo pipefail
SRC="${1:?usage: $0 <gnustep stage root> <out dir>}"
OUT="${2:?usage: $0 <gnustep stage root> <out dir>}"
REPO="$(cd "$(dirname "$0")/../.." && pwd -P)"
LAYOUT="$REPO/third_party/gnustep-make/FilesystemLayouts/mac"
S="$SRC/usr/GNUstep/System"
CONF=etc/GNUstep/GNUstep.conf

[ -f "$LAYOUT" ] || { echo "error: $LAYOUT not found -- run setup_third_party.sh" >&2; exit 1; }
for d in "$S/Applications" "$S/Library" "$S/Tools" "$SRC/$CONF"; do
    [ -e "$d" ] || { echo "error: $d not found -- run the build_gnustep_*.sh scripts first" >&2; exit 1; }
done
# Nothing else may be under /usr/GNUstep: it would be left behind unmoved.
extra="$(cd "$SRC" && find usr -mindepth 1 -maxdepth 3 | grep -vxE 'usr/GNUstep|usr/GNUstep/System|usr/GNUstep/System/(Applications|Library|Tools)' || true)"
[ -z "$extra" ] || { echo "error: unexpected entries in the GNUstep stage, not handled here:" >&2; echo "$extra" >&2; exit 1; }
# The image already has /System/Library/SystemConfiguration, /Library/
# {LaunchDaemons,Preferences} and /usr/local/bin/*_smoketest; GNUstep must
# not bring any of those names.
for n in SystemConfiguration LaunchDaemons Preferences; do
    [ ! -e "$S/Library/$n" ] || { echo "error: GNUstep's Library has $n, which collides with the image's own /System/Library or /Library" >&2; exit 1; }
done
if ls "$S/Tools" | grep -q '_smoketest$'; then
    echo "error: a GNUstep tool is named *_smoketest, which collides with /usr/local/bin's smoketests" >&2; exit 1
fi

rm -rf "$OUT"
mkdir -p "$OUT/Applications" "$OUT/System" "$OUT/Library" "$OUT/usr/local/bin" "$OUT/usr/GNUstep/System"
ditto "$SRC/etc" "$OUT/etc"

# Applications.
for a in "$S/Applications"/*; do
    n="$(basename "$a")"
    case "$n" in
        Admin) ditto "$a" "$OUT/Applications/Utilities" ;;
        *.app) ditto "$a" "$OUT/Applications/$n" ;;
        *) echo "error: $a is neither an .app nor Admin" >&2; exit 1 ;;
    esac
done

# Library.
ditto "$S/Library" "$OUT/System/Library"
if [ -d "$OUT/System/Library/Documentation" ]; then
    mv "$OUT/System/Library/Documentation" "$OUT/Library/Documentation"
fi

# Tools. gnustep-make links each app's executable into Tools as
# ../Applications/X.app/X; from /usr/local/bin that is
# ../../../Applications/X.app/X.
for t in "$S/Tools"/*; do
    n="$(basename "$t")"
    if [ -L "$t" ]; then
        l="$(readlink "$t")"
        case "$l" in
            ../Applications/*) ln -s "../../../Applications/${l#../Applications/}" "$OUT/usr/local/bin/$n" ;;
            *) echo "error: $t -> $l is a symlink this script does not know how to move" >&2; exit 1 ;;
        esac
    elif [ "$n" = Admin ] && [ -d "$t" ]; then
        ditto "$t" "$OUT/usr/local/sbin"
    else
        ditto "$t" "$OUT/usr/local/bin/$n"
    fi
done

# Compatibility symlinks at the old locations.
ln -s ../../../Applications "$OUT/usr/GNUstep/System/Applications"
ln -s ../../../System/Library "$OUT/usr/GNUstep/System/Library"
ln -s ../../local/bin "$OUT/usr/GNUstep/System/Tools"

# GNUstep.conf: every GNUSTEP_MAKEFILES / SYSTEM_* / NETWORK_* / LOCAL_* key
# of the mac layout, except the *_USERS_DIR keys (home directories stay
# /home). The USER_DIR_* keys (~/GNUstep/...) and the user config/defaults
# locations are left as they are, so existing users' defaults still load.
keys=0
while IFS='=' read -r k v; do
    case "$k" in
        GNUSTEP_MAKEFILES|GNUSTEP_SYSTEM_*|GNUSTEP_NETWORK_*|GNUSTEP_LOCAL_*) ;;
        *) continue ;;
    esac
    case "$k" in *_USERS_DIR) continue ;; esac
    if grep -q "^$k=" "$OUT/$CONF"; then
        sed -i '' "s|^$k=.*|$k=$v|" "$OUT/$CONF"
    else
        printf '%s=%s\n' "$k" "$v" >> "$OUT/$CONF"
    fi
    grep -qx "$k=$v" "$OUT/$CONF" || { echo "error: could not set $k in $OUT/$CONF" >&2; exit 1; }
    keys=$((keys+1))
done < <(grep -E '^GNUSTEP_[A-Z_]+=' "$LAYOUT")
[ "$keys" -ge 30 ] || { echo "error: only $keys keys taken from $LAYOUT" >&2; exit 1; }
grep -qx 'GNUSTEP_SYSTEM_APPS=/Applications' "$OUT/$CONF" &&
    grep -qx 'GNUSTEP_SYSTEM_LIBRARY=/System/Library' "$OUT/$CONF" &&
    grep -qx 'GNUSTEP_SYSTEM_TOOLS=/usr/local/bin' "$OUT/$CONF" &&
    grep -qx 'GNUSTEP_MAKEFILES=/System/Library/Makefiles' "$OUT/$CONF" ||
    { echo "error: $OUT/$CONF does not have the mac layout" >&2; exit 1; }
if grep -E '^GNUSTEP_(MAKEFILES|SYSTEM_|NETWORK_|LOCAL_)' "$OUT/$CONF" | grep -v '_USERS_DIR=' | grep -q /usr/GNUstep; then
    grep -n /usr/GNUstep "$OUT/$CONF" >&2; echo "error: $OUT/$CONF still names /usr/GNUstep" >&2; exit 1
fi

# The apps' .desktop files (pl2link output, written with the build layout).
find "$OUT/Applications" -name '*.desktop' -type f | while IFS= read -r f; do
    sed -i '' -e 's|/usr/GNUstep/System/Applications|/Applications|g' \
              -e 's|/usr/GNUstep/System/Tools|/usr/local/bin|g' "$f"
done

# Every symlink in the result must resolve inside it.
bad="$(cd "$OUT" && find . -type l | while IFS= read -r l; do [ -e "$l" ] || echo "$l -> $(readlink "$l")"; done)"
[ -z "$bad" ] || { echo "error: dangling symlinks in $OUT:" >&2; echo "$bad" >&2; exit 1; }
[ -x "$OUT/usr/GNUstep/System/Library/Libraries/libgnustep-base.dylib" ] ||
    { echo "error: libgnustep-base does not resolve through the compatibility symlink" >&2; exit 1; }
echo "staged GNUstep mac layout in $OUT: $(ls "$OUT/Applications" | tr '\n' ' ')"
