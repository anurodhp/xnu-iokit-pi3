#!/bin/bash
# Rebuilds init.macho and pwtool.macho and writes them into an SD card
# image's real HFS+ root (/sbin/launchd, /sbin/pwtool), replacing whatever's
# there. This is a manual macOS-only hdiutil/diskutil operation (attach the
# raw MBR image, mount just the HFS+ partition, copy, unmount, detach) that
# had previously been done by hand each time init.c changed -- see project
# memory (userspace libc/dyld) for why this needed a script.
#
# Also ensures /etc exists on the image -- fresh images have no /etc dir,
# so pwtool's lock-file open(O_CREAT, "/etc/.pwtool.lock") fails ENOENT on
# first use. Same class of gap as /dev needing to exist for devfs to mount
# onto (see project memory), fixed here the same way: create it host-side
# rather than teaching pwtool to mkdir its own lock directory.
#
# If the image has no /etc/master.passwd yet, also provisions a real
# root account (password "darwin") directly onto the mounted volume via
# tools/pwtool/hash_password_host.c (a real host-built hasher, reusing
# crypt_des.c). This closes the chicken-and-egg gap left by removing
# init.c's implicit-root-on-no-accounts fallback (see init.c's
# LOGIN_NO_ACCOUNTS handling): there is no other way to get a FIRST
# account onto a fresh image, since pwtool.c itself only runs on the
# booted target and needs a shell -- which now needs a login -- which
# needs an account. Only ever runs on a genuinely account-less image;
# never overwrites an existing /etc/master.passwd.
#
# Usage: tools/init_binary/inject_into_sd_image.sh [SD_IMAGE] [HFS_PARTITION_INDEX]
#   SD_IMAGE:             defaults to qemu/hfs_root_test.img (runvm.sh's default)
#   HFS_PARTITION_INDEX:  1-based fdisk slice number for the HFS+ root
#                         partition, defaults to 2 (matches runvm.sh's
#                         default ROOT_DEV=disk0s2)
set -e
cd "$(dirname "$0")/../.."

SD_IMAGE="${1:-qemu/hfs_root_test.img}"
PART_IDX="${2:-2}"

if [ ! -f "$SD_IMAGE" ]; then
    echo "error: SD card image not found: $SD_IMAGE" >&2
    exit 1
fi

python3 tools/init_binary/build.py
python3 tools/pwtool/build.py

DISK=$(hdiutil attach -nomount "$SD_IMAGE" | awk 'NR==1{print $1}')
if [ -z "$DISK" ]; then
    echo "error: hdiutil attach did not report a device node" >&2
    exit 1
fi
SLICE="${DISK}s${PART_IDX}"
echo "+ attached $SD_IMAGE as $DISK (root partition: $SLICE)"

cleanup() {
    diskutil unmount "$SLICE" >/dev/null 2>&1 || true
    hdiutil detach "$DISK" >/dev/null 2>&1 || true
}
trap cleanup EXIT

diskutil mount "$SLICE"
MNT=$(diskutil info "$SLICE" | awk -F': +' '/Mount Point/{print $2}')
if [ -z "$MNT" ]; then
    echo "error: could not determine mount point for $SLICE" >&2
    exit 1
fi

cp tools/init_binary/init.macho "$MNT/sbin/launchd"
chmod 700 "$MNT/sbin/launchd"
echo "+ wrote $(wc -c < tools/init_binary/init.macho) bytes to $MNT/sbin/launchd"

cp tools/pwtool/pwtool.macho "$MNT/sbin/pwtool"
chmod 700 "$MNT/sbin/pwtool"
echo "+ wrote $(wc -c < tools/pwtool/pwtool.macho) bytes to $MNT/sbin/pwtool"

mkdir -p "$MNT/etc"
echo "+ ensured $MNT/etc exists (pwtool lock-file/master.passwd/group directory)"

if [ -f tools/userland_staging/bash.macho ]; then
    mkdir -p "$MNT/bin"
    cp tools/userland_staging/bash.macho "$MNT/bin/bash"
    chmod 755 "$MNT/bin/bash"
    echo "+ wrote $(wc -c < tools/userland_staging/bash.macho) bytes to $MNT/bin/bash"
    echo "  (real static bash-3.2, fully working -- interactive prompt, real"
    echo "  termios/tty, GNU readline tab completion + history all"
    echo "  QEMU-verified; this is now the default login shell init.c execs"
    echo "  after a successful login, see docs/roadmap.md)"
    cp tools/userland_staging/bash.macho "$MNT/bin/sh"
    chmod 755 "$MNT/bin/sh"
    echo "+ wrote $(wc -c < tools/userland_staging/bash.macho) bytes to $MNT/bin/sh (same real"
    echo "  bash binary as /bin/bash -- real systems' /bin/sh is commonly bash or"
    echo "  another real shell, not a distinct binary; needed for real real,"
    echo "  unmodified Apple source that hardcodes _PATH_BSHELL (\"/bin/sh\"),"
    echo "  e.g. shell_cmds/apply's real default command interpreter -- found via"
    echo "  a real QEMU boot-test failure (apply's own honest \"/bin/sh: No such"
    echo "  file or directory\"), not added speculatively)"
else
    echo "= tools/userland_staging/bash.macho not built -- run build_bash.sh first if wanted, skipping"
fi

if [ -f tools/userland_staging/uname.macho ]; then
    mkdir -p "$MNT/usr/bin"
    cp tools/userland_staging/uname.macho "$MNT/usr/bin/uname"
    chmod 755 "$MNT/usr/bin/uname"
    echo "+ wrote $(wc -c < tools/userland_staging/uname.macho) bytes to $MNT/usr/bin/uname"
    echo "  (real, unmodified Apple uname -- Phase 4 step 3's first real command,"
    echo "  see docs/roadmap.md)"
else
    echo "= tools/userland_staging/uname.macho not built -- run build_uname.sh first if wanted, skipping"
fi

if [ -f tools/userland_staging/ps.macho ]; then
    mkdir -p "$MNT/bin"
    cp tools/userland_staging/ps.macho "$MNT/bin/ps"
    chmod 755 "$MNT/bin/ps"
    echo "+ wrote $(wc -c < tools/userland_staging/ps.macho) bytes to $MNT/bin/ps"
    echo "  (real, unmodified Apple ps -- Phase 4 step 3, see docs/roadmap.md)"
else
    echo "= tools/userland_staging/ps.macho not built -- run build_ps.sh first if wanted, skipping"
fi

if [ -f tools/pthread_kthread_test/pthread_kthread_test.macho ]; then
    mkdir -p "$MNT/bin"
    cp tools/pthread_kthread_test/pthread_kthread_test.macho "$MNT/bin/pthread_kthread_test"
    chmod 755 "$MNT/bin/pthread_kthread_test"
    echo "+ wrote $(wc -c < tools/pthread_kthread_test/pthread_kthread_test.macho) bytes to $MNT/bin/pthread_kthread_test"
    echo "  (Phase 7.5 real-bsdthread_create smoke test -- run it from the shell,"
    echo "  see tools/pthread_kthread_test/pthread_kthread_test.c)"
else
    echo "= tools/pthread_kthread_test/pthread_kthread_test.macho not built -- run tools/pthread_kthread_test/build.py first if wanted, skipping"
fi

# Real Phase 4 step 3 "remaining commands" tiers (docs/roadmap.md) --
# each build_*_cmds.sh script writes one <name>.macho per real command
# it built; every one of them lands in /bin, matching real BSD/macOS
# layout (all of shell_cmds' Tier 1/2 set below is genuinely /bin, not
# /usr/bin, on a real system). Generic loop instead of one if-block per
# command -- this list only grows as more tiers land.
mkdir -p "$MNT/bin"
for cmd in true false yes pwd basename dirname echo printf hostname logname cat wc head tail mkdir touch rm which env sleep printenv cut uniq ln id cp mv rev fold paste expand unexpand comm colrm lam rs rmdir mkfifo mknod chflags seq jot test kill tee mktemp hexdump stat cksum pathchk du chmod chown nice renice nohup chroot apply vis unvis grep sed nl xargs ed ls find date gzip compress diff tar less; do
    if [ -f "tools/userland_staging/${cmd}.macho" ]; then
        cp "tools/userland_staging/${cmd}.macho" "$MNT/bin/${cmd}"
        chmod 755 "$MNT/bin/${cmd}"
        echo "+ wrote $(wc -c < "tools/userland_staging/${cmd}.macho") bytes to $MNT/bin/${cmd}"
    else
        echo "= tools/userland_staging/${cmd}.macho not built -- run build_simple_cmds.sh first if wanted, skipping"
    fi
done

# whoami(1) (Tier B, docs/roadmap.md "Command priority reorganized"):
# no standalone real source exists anywhere in the macos-112 manifest
# (confirmed via a real `git ls-tree macos-112` lookup against
# apple-oss-distributions/distribution-macOS, not guessed) -- real
# Apple ships it as a real *mode* of shell_cmds/id/id.c instead
# (id.c's own real `myname = strrchr(argv[0], '/'); ...; else if
# (strcmp(myname, "whoami") == 0) iswhoami = 1;` argv[0]-basename
# dispatch, confirmed by reading the file directly), the exact same
# real "one binary, multiple installed names" pattern this script
# already uses for /bin/bash + /bin/sh above. id.macho is already
# built and QEMU-verified; this just installs a second copy of the
# same real binary under the name that makes it behave as whoami.
if [ -f tools/userland_staging/id.macho ]; then
    cp tools/userland_staging/id.macho "$MNT/bin/whoami"
    chmod 755 "$MNT/bin/whoami"
    echo "+ wrote $(wc -c < tools/userland_staging/id.macho) bytes to $MNT/bin/whoami (same real"
    echo "  id binary as /bin/id -- id.c's own real argv[0]-basename dispatch"
    echo "  ('myname == \"whoami\"') makes it behave as whoami(1) when installed"
    echo "  under this name, real Apple/BSD source, not a wrapper script)"
fi

# more(1) (Tier B): same real "one binary, multiple installed names"
# story as whoami above -- real less/main.c itself checks its own
# argv[0] basename (`if (strcmp(s, "more") == 0) less_is_more = 1;`,
# confirmed by reading the file directly) and switches to real
# more(1)-compatible default behavior (no real wrapper script).
if [ -f tools/userland_staging/less.macho ]; then
    cp tools/userland_staging/less.macho "$MNT/bin/more"
    chmod 755 "$MNT/bin/more"
    echo "+ wrote $(wc -c < tools/userland_staging/less.macho) bytes to $MNT/bin/more (same real"
    echo "  less binary as /bin/less -- less/main.c's own real argv[0]-basename"
    echo "  dispatch ('less_is_more') makes it behave as more(1) when installed"
    echo "  under this name)"
fi

mkdir -p "$MNT/tmp"
echo "+ ensured $MNT/tmp exists (real fopen()-family O_CREAT target dir -- same"
echo "  class of gap as /etc and /dev above; found via a real QEMU boot-test"
echo "  FAIL when tools/userland_staging/libc_stdio_smoketest.macho's real"
echo "  fopen(\"/tmp/...\", \"w\") returned NULL on a fresh image with no /tmp)"

# Real timezone database (Phase 4.6, docs/roadmap.md) -- real, unmodified
# Apple-shipped TZif2 binary zoneinfo files, copied directly from this
# host Mac's own /usr/share/zoneinfo/{UTC,GMT} (real Apple/IANA data,
# not fabricated -- `file` confirms real "timezone data (fat), version
# 2" format) rather than vendoring a whole IANA tzdata source tree +
# building a real `zic` compiler, since this project's own
# Libc/stdtime/FreeBSD/localtime.c only needs valid TZif files to exist
# at the real paths it looks up, not a complete zone database. Minimal,
# scoped, real -- not a fabricated stand-in. TZDIR/TZDEFAULT resolve to
# /var/db/timezone/{zoneinfo,localtime} for this project's real
# arm64-apple-ios14.4 (embedded, not TARGET_OS_OSX) build target -- see
# third_party/Libc/stdtime/FreeBSD/tzfile.h's own real conditional.
mkdir -p "$MNT/var/db/timezone/zoneinfo"
cp tools/userland_staging/zoneinfo_minimal/UTC "$MNT/var/db/timezone/zoneinfo/UTC"
cp tools/userland_staging/zoneinfo_minimal/GMT "$MNT/var/db/timezone/zoneinfo/GMT"
cp tools/userland_staging/zoneinfo_minimal/UTC "$MNT/var/db/timezone/localtime"
echo "+ wrote real Apple UTC/GMT zoneinfo to $MNT/var/db/timezone/zoneinfo/"
echo "  and $MNT/var/db/timezone/localtime (default TZ, real data, this host's"
echo "  own /usr/share/zoneinfo -- see docs/roadmap.md Phase 4.6)"

if [ ! -f "$MNT/etc/master.passwd" ]; then
    echo "+ no $MNT/etc/master.passwd -- provisioning real root account (password: darwin)"
    HASHTOOL="$(mktemp -t hash_password_host)"
    clang -Wall -Wextra -I tools/pwtool -o "$HASHTOOL" \
        tools/pwtool/hash_password_host.c tools/libinfo_custom/crypt_des.c
    ROOTHASH=$("$HASHTOOL" darwin)
    rm -f "$HASHTOOL"
    if [ -z "$ROOTHASH" ]; then
        echo "error: root password hash generation failed" >&2
        exit 1
    fi
    printf 'root:%s:0:0::0:0::/:/bin/sh\n' "$ROOTHASH" > "$MNT/etc/master.passwd"
    printf 'root:*:0:\n' > "$MNT/etc/group"
    chmod 600 "$MNT/etc/master.passwd"
    chmod 644 "$MNT/etc/group"
    echo "+ wrote $MNT/etc/master.passwd and $MNT/etc/group (root:darwin, uid/gid 0)"
else
    echo "+ $MNT/etc/master.passwd already exists -- leaving accounts as-is"
fi

if [ ! -f "$MNT/etc/hostname" ]; then
    echo "strudel" > "$MNT/etc/hostname"
    chmod 644 "$MNT/etc/hostname"
    echo "+ wrote $MNT/etc/hostname (default: strudel) -- init.c reads this at boot"
    echo "  (matches real FreeBSD/NetBSD/OpenBSD convention: edit this file and"
    echo "  reboot to rename, no rebuild needed; falls back to a compiled-in"
    echo "  default of the same name if this file is ever missing)"
else
    echo "+ $MNT/etc/hostname already exists -- leaving as-is"
fi

diskutil unmount "$MNT"
hdiutil detach "$DISK"
trap - EXIT

echo "done -- $SD_IMAGE's /sbin/launchd, /sbin/pwtool, and /etc are up to date"
