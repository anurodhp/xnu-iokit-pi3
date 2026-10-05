#!/bin/bash
# Rebuilds pwtool.macho and writes it, plus real launchd.macho, into an SD
# card image's real HFS+ root (/sbin/launchd, /sbin/pwtool), replacing
# whatever's there. This is a manual macOS-only hdiutil/diskutil operation
# (attach the raw MBR image, mount just the HFS+ partition, copy, unmount,
# detach) that had previously been done by hand each time init.c changed --
# see project memory (userspace libc/dyld) for why this needed a script.
#
# PID 1 boot-path integration (2026-08-28, docs/daemon-roadmap.md Phase 6.8
# Milestone 4): /sbin/launchd is now real, dynamically-linked
# tools/userland_staging/libc_build/launchd.macho -- an explicit, user-
# picked "outright replace" of tools/init_binary/'s custom static PID-1
# (the alternative considered was init_binary exec()ing into launchd;
# outright replace was chosen instead). The kernel's own
# load_init_program() hardcodes the exact /sbin/launchd path
# (bsd/kern/kern_exec.c), unmodified stock XNU behavior, so no kernel
# change was needed for this swap. tools/init_binary/init.c itself (the
# retired custom PID-1) and its build.py/host_test.c/_pty_test*.py were
# removed 2026-08-31 -- see docs/static-linking-cleanup-plan.md and
# Linear DAR-166/DAR-106/DAR-137.
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
#
# SKIP_SMOKETESTS=1 (env var, not a positional arg): skip every
# *_smoketest.macho copy block. Smoketests are QEMU-only dev-iteration
# diagnostics (kqueue/pthread/tlv/dns/fork_alarm/etc -- one-shot programs
# written to isolate a single real bug in one boot, not real userland
# commands), so they have no reason to ship on a real-hardware image --
# they just add size and surface area for no real benefit there.
# tools/make_realhw_sd_image.sh sets this; the QEMU path
# (tools/make_sd_image.sh / runvm.sh's own default image) leaves it unset
# so QEMU images keep getting every smoketest, same as always.
set -e
cd "$(dirname "$0")/../.."

SD_IMAGE="${1:-qemu/hfs_root_test.img}"
PART_IDX="${2:-2}"

if [ ! -f "$SD_IMAGE" ]; then
    echo "error: SD card image not found: $SD_IMAGE" >&2
    exit 1
fi

python3 tools/pwtool/build.py

# install_smoketest SRC DEST MISSING_MSG [NOTE...] (DAR-348)
#
# The one way this script installs a QEMU-only *_smoketest binary. It
# honours SKIP_SMOKETESTS (real-hardware images; smoketests are dev
# diagnostics, not userland), copies SRC to $MNT/DEST mode 755, and echoes
# each NOTE. It always returns 0 (this script runs under set -e) and sets
# SMOKETEST_INSTALLED=1 only when it actually installed the file, for a
# caller with extra setup to test.
# Every smoketest used to repeat this guard by hand (18 copies), which made
# a forgotten guard easy; the self-check below now rejects any other copy
# of a *_smoketest binary in this file. It is defined and checked here,
# before the image is attached, so no install runs ahead of the check.
install_smoketest() {
    local src="$1" dest="$2" missing="$3"
    shift 3
    SMOKETEST_INSTALLED=0
    if [ -n "$SKIP_SMOKETESTS" ]; then
        echo "= $(basename "$src") skipped (SKIP_SMOKETESTS set -- real-hardware image; smoketests are QEMU-only dev diagnostics, not real userland)"
        return 0
    fi
    if [ ! -f "$src" ]; then
        echo "$missing"
        return 0
    fi
    mkdir -p "$(dirname "$MNT/$dest")"
    cp "$src" "$MNT/$dest"
    chmod 755 "$MNT/$dest"
    echo "+ wrote $(wc -c < "$src") bytes to /$dest"
    local note
    for note in "$@"; do
        echo "$note"
    done
    SMOKETEST_INSTALLED=1
}
if grep -nE '^[^#]*[[:space:]]cp[[:space:]].*_smoketest' tools/init_binary/inject_into_sd_image.sh | grep -v 'install_smoketest' >&2; then
    echo "error: the line(s) above copy a *_smoketest binary directly; use install_smoketest so SKIP_SMOKETESTS is honoured" >&2
    exit 1
fi

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

# A LEAKED QEMU still holding this image open makes hdiutil attach it
# read-only ("Media Read-Only: Yes"), and diskutil then reports the far
# less obvious "Volume on diskNs2 failed to mount" -- not the read-only
# mount the block below handles, an outright mount failure, which `set -e`
# turns into an abort before a single byte is copied. The boot test that
# follows then silently re-tests the STALE image, the exact wasted-cycle
# failure the read-only comment below describes, reached through a
# different door (hit for real 2026-09-03 during DAR-220: an earlier
# session's QEMU was still alive and `hdiutil attach -readwrite` failed
# with "Resource temporarily unavailable"; fsck_hfs reported the volume
# clean and changed nothing, because the journal was never the problem).
# Diagnose it explicitly rather than letting the mount failure speak,
# since the two causes need opposite fixes.
if diskutil info "$DISK" | grep -qE 'Media Read-Only: +Yes'; then
    echo "error: $DISK attached READ-ONLY -- something else still has $SD_IMAGE open" >&2
    echo "       (almost always a leaked qemu-system-aarch64 from an earlier boot test;" >&2
    echo "        confirm with: lsof $SD_IMAGE   then kill that pid and re-run)" >&2
    lsof "$SD_IMAGE" >&2 || true
    exit 1
fi
# A genuinely dirty journal (guest panic mid-write) can also refuse to
# mount outright; fsck_hfs replays it and the retry succeeds.
if ! diskutil mount "$SLICE"; then
    echo "= $SLICE would not mount (dirty journal from a killed/panicked guest) -- running fsck_hfs"
    fsck_hfs -fy "/dev/r${SLICE#/dev/}"
    diskutil mount "$SLICE"
fi
MNT=$(diskutil info "$SLICE" | awk -F': +' '/Mount Point/{print $2}')
if [ -z "$MNT" ]; then
    echo "error: could not determine mount point for $SLICE" >&2
    exit 1
fi

# A guest panic (or any hard QEMU kill) leaves this HFS+ volume's journal
# dirty, and macOS then mounts it READ-ONLY. Every copy below fails with
# "Read-only file system", which is loud on its own but easy to miss when
# this script runs inside a larger pipeline -- and the boot test that
# follows then silently exercises a STALE image, which looks exactly like
# "the fix didn't work" (hit for real 2026-09-02, DAR-200: a whole
# 4-minute QEMU run wasted re-testing the previous image). fsck_hfs
# replays/clears the journal and the remount comes back read-write; no
# sudo needed for an hdiutil-attached image owned by this user.
if mount | grep -qE "on ${MNT} .*read-only"; then
    echo "= $MNT mounted read-only (dirty journal, almost certainly a prior guest panic) -- running fsck_hfs"
    diskutil unmount "$SLICE" >/dev/null 2>&1 || true
    fsck_hfs -fy "/dev/r${SLICE#/dev/}"
    diskutil mount "$SLICE"
    MNT=$(diskutil info "$SLICE" | awk -F': +' '/Mount Point/{print $2}')
    if mount | grep -qE "on ${MNT} .*read-only"; then
        echo "error: $MNT is still read-only after fsck_hfs -- refusing to write a stale image" >&2
        exit 1
    fi
    echo "+ $MNT remounted read-write"
fi

if [ -f tools/userland_staging/libc_build/launchd.macho ]; then
    cp tools/userland_staging/libc_build/launchd.macho "$MNT/sbin/launchd"
    chmod 700 "$MNT/sbin/launchd"
    echo "+ wrote $(wc -c < tools/userland_staging/libc_build/launchd.macho) bytes to $MNT/sbin/launchd"
    echo "  (real, dynamically-linked launchd -- Phase 6.8 Milestone 4, see"
    echo "  tools/userland_staging/build_launchd.sh; needs /usr/lib/dyld +"
    echo "  the full dylib stack already installed above)"

    # DAR-315, real gap found wiring launchd up for its first actual boot,
    # revised 2026-09-15 to the real fix: ICU4C's own build (build_icu_
    # target.sh) produces dylibs with RELATIVE LC_ID_DYLIB/LC_LOAD_DYLIB
    # names ("libicuuc.66.dylib", not /usr/lib/...) -- confirmed via
    # otool -D/-L, not the real Apple convention every other dylib here
    # follows. This USED to be worked around by installing a copy at the
    # image root, matching the relative name literally so it resolved
    # against launchd's cwd (kernel leaves cwd="/" for the very first
    # process, since it has no parent to inherit one from) -- but that
    # only covered launchd. Every OTHER process (configd, ipconfig run
    # from an interactive login shell, etc.) resolves the SAME relative
    # name against ITS OWN cwd instead, which is not guaranteed to be
    # "/". Found for real 2026-09-15: QEMU's root shell happens to have
    # cwd=="/" (confirmed via `pwd`), masking this for months; real
    # hardware's root shell does not, and `ipconfig`/`configd` (both
    # link libCoreFoundation.dylib -> libicuuc.66.dylib) aborted at dyld
    # load with "Library not loaded: libicuuc.66.dylib" + EXC_CORPSE_
    # NOTIFY there -- which is also why configd's StandardErrorPath log
    # and chronyd both looked broken all along (configd was never
    # surviving past dyld load to do anything, including the pre-exec
    # file setup launchd does for it).
    #
    # Real fix, not a second workaround: the three already-built target
    # dylibs (tools/userland_staging/icu_build/target/lib/) have had
    # their LC_ID_DYLIB AND their cross-references to each other
    # (libicui18n -> libicuuc/libicudata, libicuuc -> libicudata)
    # rewritten to real absolute /usr/lib/... paths via install_name_tool
    # -id/-change, matching every other dylib in this system -- verified
    # via otool -L afterward, not just otool -D. That makes ONE install
    # location (/usr/lib, dyld's real standard path, not a fallback
    # trick) correct and sufficient for every consumer's cwd, including
    # PID 1's -- no image-root copy needed. (install_name_tool only
    # rewrites what's baked into ALREADY-linked consumers of a FUTURE
    # relink; launchd.macho/CoreFoundation.dylib/configd.macho/
    # ipconfig.macho were all linked against the pre-fix relative-name
    # dylibs, so their own LC_LOAD_DYLIB entries are still the bare
    # relative name -- those resolve via dyld's real, standard
    # DYLD_FALLBACK_LIBRARY_PATH-equivalent search, which tries /usr/lib
    # regardless of cwd, matching the search order dyld's own abort
    # message already showed. A full fix for THAT would mean relinking
    # every ICU consumer against the corrected dylibs -- real, but a
    # separate, larger follow-up, not bundled into this ticket.)
    ICU_LIB="tools/userland_staging/icu_build/target/lib"
    mkdir -p "$MNT/usr/lib"
    for d in libicuuc.66.dylib libicui18n.66.dylib libicudata.66.dylib; do
        if [ -f "$ICU_LIB/$d" ]; then
            cp "$ICU_LIB/$d" "$MNT/usr/lib/$d"
            chmod 755 "$MNT/usr/lib/$d"
            echo "+ wrote $(wc -c < "$ICU_LIB/$d") bytes to $MNT/usr/lib/$d (real absolute install name, DAR-315)"
            # Clean up the image-root copy the pre-fix version of this
            # script used to write. A stale one left over from re-running
            # this script against an already-staged image would otherwise
            # just sit there unused (nothing resolves against it once the
            # dylib has a real absolute ID) -- 34MB of dead weight per
            # ICU release, not a "harmless leftover".
            if [ -f "$MNT/$d" ]; then
                rm -f "$MNT/$d"
                echo "  (removed stale pre-DAR-315 image-root copy $MNT/$d)"
            fi
        else
            echo "error: $ICU_LIB/$d not found -- run build_icu_target.sh first" >&2
            exit 1
        fi
    done
else
    echo "error: tools/userland_staging/libc_build/launchd.macho not built -- run build_launchd.sh first" >&2
    exit 1
fi

# launchctl.macho is DELIBERATELY NOT installed onto the boot image.
# (2026-09-27 update, DAR-411/DAR-420: the boot hang below no longer
# reproduced on QEMU, but running the real `bootstrap -S System` here is
# still wrong. This port's launchd starts its daemons BEFORE launchctl
# would run, so launchctl's empty_dir(/var/run) deletes their live sockets
# (/var/run/syslog, chronyd.sock). DAR-420 tracks the Apple-order fix.
# /tmp and /var/run are emptied by pd_launchd_boot.c meanwhile.)
# Tried real 2026-09-02 (DAR-195's own consumer-migration verification)
# -- it builds clean and links with zero real gaps (gap_audit.sh clean,
# every phantom-libdispatch-bound symbol this session found real, closed
# fixes for), but a real QEMU boot found a genuine, PRE-EXISTING,
# unrelated-to-CF-dylib-conversion bug: real launchd core.c always calls
# `/bin/launchctl bootstrap -S System` during its own bootstrap
# (bootstrap_tool[], unconditional -- previously always a silent, no-op
# ENOENT since launchctl.macho never existed on disk before). Once
# launchctl.macho actually exists and runs, real launchctl.c's own
# system_specific_bootstrap() hits this project's real, already-
# documented BOOTSTRAP_NOT_PRIVILEGED stub limitation
# (job_mig_look_up2_forward()/job_mig_parent_forward(), see
# docs/launchd-pid1-postmortem.md) and hangs the ENTIRE boot instead of
# failing cleanly (confirmed reproducible across 2 independent real QEMU
# runs, 240s and 400s timeouts, identical hang point both times -- not a
# QEMU/TCG timing flake). Root cause not yet fully isolated (str2bsport()
# in launchctl.c returns a bogus mach_port_t value of literal `1` instead
# of MACH_PORT_NULL on its own early-return error paths, a real,
# separate upstream code smell that may or may not be the actual hang
# cause -- not confirmed). Tracked as its own real, separate issue (see
# DAR-195's Linear ticket comments); re-enable this install block once
# that's fixed and re-verified via a real QEMU boot. Leaving it out
# keeps this project's own boot path exactly as reliable as it was
# before this session (launchctl.macho is still real, still builds via
# tools/userland_staging/build_launchctl.sh, just not auto-installed).
#
# DAR-234 (2026-09-03, dynamicfix sweep): launchctl.macho now links
# clean (zero undefined symbols, zero fixup errors -- the collate/glob
# gaps above are real, closed fixes) and passes bind_target_audit.sh,
# but the real, separate BOOTSTRAP_NOT_PRIVILEGED hang documented just
# above is STILL unfixed and out of scope for this session. Installing
# it at the exact path core.c hardcodes (`/bin/launchctl`, see
# bootstrap_tool[] in launchd's core.c) would still reproduce that same
# boot hang. Installed here at `/usr/bin/launchctl` instead -- a
# genuinely different absolute path core.c's own hardcoded string never
# matches, so launchd's own unconditional bootstrap call still silently
# ENOENTs exactly as before (safe, unchanged boot behavior) -- while
# still putting a real, runnable launchctl.macho on the image for manual
# invocation from an interactive shell (e.g. `/usr/bin/launchctl list`),
# so this session's real link fixes get a real QEMU-boot verification
# without reintroducing the known hang. Re-home to `/bin/launchctl` once
# the BOOTSTRAP_NOT_PRIVILEGED gap is actually closed.
if [ -f tools/userland_staging/libc_build/launchctl.macho ]; then
    mkdir -p "$MNT/usr/bin"
    cp tools/userland_staging/libc_build/launchctl.macho "$MNT/usr/bin/launchctl"
    chmod 755 "$MNT/usr/bin/launchctl"
    echo "+ wrote $(wc -c < tools/userland_staging/libc_build/launchctl.macho) bytes to $MNT/usr/bin/launchctl"
    echo "  (real, dynamically-linked launchctl -- DAR-234; deliberately NOT"
    echo "  at /bin/launchctl, see this script's own comment above)"
fi

# DAR-165: which console-login LaunchDaemon plist gets installed as the
# live org.puredarwin.console-login.plist. Default "getty" is the new
# real getty->login->shell chain (pd_console_login.c's own default exec
# target, no argv). CONSOLE_LOGIN_MODE=direct installs the DAR-164
# fallback instead (pd_console_login.c invoked with -direct-login, skips
# getty and execs /usr/bin/login directly) -- per DAR-165's own explicit
# instruction to keep that path available (not deleted) until the real
# getty chain is QEMU-verified end to end. Only ONE of the two is ever
# installed under the live filename: this project's simplified launchd
# core.c job_import() does NOT actually honor the plist "Disabled" key
# (job_assumes(j, !value) just logs, never suppresses RunAtLoad/KeepAlive
# -- confirmed by reading core.c directly), so shipping both plists live
# in /Library/LaunchDaemons at once would race two jobs for /dev/console
# -- CONSOLE_LOGIN_MODE picks exactly one at image-build time instead,
# same "every built artifact needs a reproducing script" convention as
# everything else in this file, no manual on-image edits needed to flip
# back.
: "${CONSOLE_LOGIN_MODE:=getty}"
case "$CONSOLE_LOGIN_MODE" in
    getty)  CONSOLE_LOGIN_PLIST="third_party/PureDarwin/src/Libraries/XPC/launchd/org.puredarwin.console-login.plist" ;;
    direct) CONSOLE_LOGIN_PLIST="third_party/PureDarwin/src/Libraries/XPC/launchd/org.puredarwin.console-login-direct.plist" ;;
    *) echo "error: CONSOLE_LOGIN_MODE must be 'getty' or 'direct', got '$CONSOLE_LOGIN_MODE'" >&2; exit 1 ;;
esac

if [ -f tools/userland_staging/libc_build/pd_console_login.macho ]; then
    mkdir -p "$MNT/usr/libexec" "$MNT/Library/LaunchDaemons"
    cp tools/userland_staging/libc_build/pd_console_login.macho "$MNT/usr/libexec/pd-console-login"
    chmod 755 "$MNT/usr/libexec/pd-console-login"
    echo "+ wrote $(wc -c < tools/userland_staging/libc_build/pd_console_login.macho) bytes to $MNT/usr/libexec/pd-console-login"
    cp "$CONSOLE_LOGIN_PLIST" \
        "$MNT/Library/LaunchDaemons/org.puredarwin.console-login.plist"
    chmod 644 "$MNT/Library/LaunchDaemons/org.puredarwin.console-login.plist"
    echo "+ wrote $CONSOLE_LOGIN_PLIST to $MNT/Library/LaunchDaemons/org.puredarwin.console-login.plist (CONSOLE_LOGIN_MODE=$CONSOLE_LOGIN_MODE)"
    echo "  (launchd's real getty/login-equivalent -- see"
    echo "  tools/userland_staging/build_pd_console_login.sh; loaded at boot via"
    echo "  launchd.c's pd_launchd_load_daemons_dir() call, iokit project patch)"
else
    echo "= tools/userland_staging/libc_build/pd_console_login.macho not built -- run build_pd_console_login.sh first if wanted, skipping (no console login without it)"
fi

# real login(1) (DAR-164): real, unmodified system_cmds/login.tproj/
# login.c, installed at the real Apple path (/usr/bin/login --
# third_party/system_cmds/getty.tproj/pathnames.h's own _PATH_LOGIN,
# what getty's real execle(LO, "login", ...) handoff expects too, even
# though getty itself isn't wired into this boot path yet -- see
# build_getty.sh's own header). pd_console_login.c's own updated real
# exec target (see that file directly) is this exact path, replacing
# its previous direct `/bin/bash -l` exec with zero auth prompt.
if [ -f tools/userland_staging/login.macho ]; then
    mkdir -p "$MNT/usr/bin"
    cp tools/userland_staging/login.macho "$MNT/usr/bin/login"
    chmod 755 "$MNT/usr/bin/login"
    echo "+ wrote $(wc -c < tools/userland_staging/login.macho) bytes to $MNT/usr/bin/login"
    echo "  (real Apple login(1), non-PAM crypt(3)/master.passwd auth branch --"
    echo "  see tools/userland_staging/build_login.sh)"
else
    echo "= tools/userland_staging/login.macho not built -- run build_login.sh first if wanted, skipping (pd-console-login will fail to exec real login without it)"
fi

# real getty.tproj (docs/roadmap.md Phase 5.5 item 1; wired into the real
# boot chain by DAR-165): pd_console_login.c's own default exec target is
# now real /bin/getty -- it does the tty setup (getty's own carrier-safe
# open, done here instead of inside getty itself, see pd_console_login.c's
# header for why) then execs getty in its "old style" (argv[2]=="-") mode,
# which prints the login: prompt, reads a username, and does getty's own
# real execle(LO, "login", ...) handoff to /usr/bin/login above. Must be
# installed at /bin/getty to match pd_console_login.c's execv("/bin/getty",
# ...) call. build_getty.sh had a real pre-existing link failure
# (_clock_get_time/_g_mach_ipc_clock_port/_g_mach_ipc_clock_sem/
# _mach_ipc_clock_init from simple_cmds_nanosleep_compat.c's _nanosleep,
# and _mmap from libpthread.a's pthread_main_thread_bootstrap.o) as of
# DAR-164 -- fixed by DAR-165 by linking libmach_ipc.a (already-built real
# Mach-IPC clock chain) and simple_cmds_mmap_compat.c + the real
# `-Wl,-alias,'_mmap$UNIX2003',_mmap` alias every other tier already uses;
# getty.macho now links fully static with zero undefined symbols.
if [ -f tools/userland_staging/getty.macho ]; then
    mkdir -p "$MNT/bin"
    cp tools/userland_staging/getty.macho "$MNT/bin/getty"
    chmod 755 "$MNT/bin/getty"
    echo "+ wrote $(wc -c < tools/userland_staging/getty.macho) bytes to $MNT/bin/getty (exec'd by pd-console-login when CONSOLE_LOGIN_MODE=getty)"
else
    echo "= tools/userland_staging/getty.macho not built -- run build_getty.sh first if wanted, skipping (required for CONSOLE_LOGIN_MODE=getty, the default)"
fi

cp tools/pwtool/pwtool.macho "$MNT/sbin/pwtool"
chmod 700 "$MNT/sbin/pwtool"
echo "+ wrote $(wc -c < tools/pwtool/pwtool.macho) bytes to $MNT/sbin/pwtool"

mkdir -p "$MNT/etc"
echo "+ ensured $MNT/etc exists (pwtool lock-file/master.passwd/group directory)"

# Core files. On this embedded (iPhoneOS) kernel, kern.corefile defaults
# to /private/var/cores/%N.core (third_party/xnu-7195/bsd/kern/
# kern_proc.c:185; /cores/core.%P is the XNU_TARGET_OS_OSX default). A
# DEVELOPMENT kernel also dumps initproc there before its "initproc
# exited" panic (bsd/kern/kern_exit.c:709). Without the directory that
# fails with ENOENT ("Failed to generate initproc core file: error 2"),
# and a launchd crash leaves no user register state at all. This image
# keeps /var as a real directory rather than Apple's /var -> private/var,
# so /private/var is a link back to it.
mkdir -p "$MNT/private" "$MNT/var/cores"
[ -e "$MNT/private/var" ] || ln -s ../var "$MNT/private/var"
chmod 1775 "$MNT/var/cores"
echo "+ ensured /private/var/cores resolves (kernel core files, incl. initproc's)"

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

if [ -f tools/userland_staging/vim.macho ]; then
    mkdir -p "$MNT/usr/bin"
    cp tools/userland_staging/vim.macho "$MNT/usr/bin/vim"
    chmod 755 "$MNT/usr/bin/vim"
    echo "+ wrote $(wc -c < tools/userland_staging/vim.macho) bytes to $MNT/usr/bin/vim"
    echo "  (real, unmodified Apple vim-91.80.1 -- DAR-185/Phase 7d, see"
    echo "  tools/userland_staging/build_vim.sh)"
    # /usr/bin/vi: real vim's own main.c parse_command_name() dispatches on
    # its argv[0] basename ("vi"/"ex"/"view"/"vimdiff"/...) to pick the real
    # compatibility mode, so a second copy under the name `vi` IS the real
    # vi -- the same "one binary, multiple installed names" pattern this
    # script already uses for /bin/bash + /bin/sh and id + whoami. Real
    # macOS ships /usr/bin/vi as a symlink to vim for exactly this reason;
    # this project copies rather than symlinks because nothing here creates
    # HFS+ symlinks on the injected image yet.
    cp tools/userland_staging/vim.macho "$MNT/usr/bin/vi"
    chmod 755 "$MNT/usr/bin/vi"
    echo "+ wrote $(wc -c < tools/userland_staging/vim.macho) bytes to $MNT/usr/bin/vi (same real"
    echo "  binary, argv[0]-dispatched into vi-compatibility mode)"
else
    echo "= tools/userland_staging/vim.macho not built -- run tools/userland_staging/build_vim.sh first if wanted, skipping"
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

# reboot(8)/halt(8)/shutdown(8) (DAR-334), all in /sbin as on a Mac. reboot
# and halt are one binary (reboot.c picks halt from argv[0]); both, and
# shutdown -r/-h, hand the request to launchd's reboot2().
if [ -f tools/userland_staging/reboot.macho ]; then
    mkdir -p "$MNT/sbin"
    for name in reboot halt; do
        cp tools/userland_staging/reboot.macho "$MNT/sbin/$name"
        chmod 755 "$MNT/sbin/$name"
    done
    echo "+ wrote reboot.macho to $MNT/sbin/reboot and $MNT/sbin/halt (DAR-334)"
else
    echo "= tools/userland_staging/reboot.macho not built -- run build_reboot.sh first if wanted, skipping"
fi
if [ -f tools/userland_staging/shutdown.macho ]; then
    cp tools/userland_staging/shutdown.macho "$MNT/sbin/shutdown"
    chmod 755 "$MNT/sbin/shutdown"
    echo "+ wrote shutdown.macho to $MNT/sbin/shutdown (DAR-334)"
else
    echo "= tools/userland_staging/shutdown.macho not built -- run build_reboot.sh first if wanted, skipping"
fi

if [ -f tools/userland_staging/ifconfig.macho ]; then
    mkdir -p "$MNT/sbin"
    cp tools/userland_staging/ifconfig.macho "$MNT/sbin/ifconfig"
    chmod 755 "$MNT/sbin/ifconfig"
    echo "+ wrote $(wc -c < tools/userland_staging/ifconfig.macho) bytes to $MNT/sbin/ifconfig"
    echo "  (real, unmodified Apple ifconfig -- Phase 6b, see"
    echo "  tools/userland_staging/build_ifconfig.sh)"
else
    echo "= tools/userland_staging/ifconfig.macho not built -- run build_ifconfig.sh first if wanted, skipping"
fi

# X11 client-library smoketest (DAR-383). The X dylibs themselves are
# installed by the generic libc_build/system/*.dylib loop below, at their
# /usr/X11/lib install names.
install_smoketest tools/userland_staging/x11_smoketest.macho "usr/X11/bin/x11_smoketest" \
    "= tools/userland_staging/x11_smoketest.macho not built -- run build_x11_smoketest.sh first if wanted, skipping" \
    "  (tools/userland_staging/build_x11_smoketest.sh)"
install_smoketest tools/userland_staging/x11_bench.macho "usr/X11/bin/x11_bench" \
    "= tools/userland_staging/x11_bench.macho not built -- run build_x11_bench.sh first if wanted, skipping" \
    "  (tools/userland_staging/build_x11_bench.sh: DAR-438 wrlib/Xlib micro-benchmark)"
# Software OpenGL smoketest (docs/gpu-opengl-plan.md Tier A): needs
# libGL.1.dylib from build_mesa.sh, installed by the generic loop below.
install_smoketest tools/userland_staging/gl_smoketest.macho "usr/X11/bin/gl_smoketest" \
    "= tools/userland_staging/gl_smoketest.macho not built -- run build_gl_smoketest.sh first if wanted, skipping" \
    "  (tools/userland_staging/build_gl_smoketest.sh: Mesa softpipe GLX/GLSL test)"
# V3D bring-up tool (DARW-295): needs libdrm.2.dylib (build_libdrm_v3d.sh),
# installed by the generic loop below.
install_smoketest tools/userland_staging/v3dprobe.macho "usr/X11/bin/v3dprobe" \
    "= tools/userland_staging/v3dprobe.macho not built -- run build_v3dprobe.sh first if wanted, skipping" \
    "  (tools/userland_staging/build_v3dprobe.sh: first-contact test for the V3D kext)"
# Window Maker Close/Kill driver (DAR-452): QEMU-only diagnostic, no xdotool/xkill here.
install_smoketest tools/userland_staging/wmclose_probe.macho "usr/X11/bin/wmclose_probe" \
    "= tools/userland_staging/wmclose_probe.macho not built -- run build_wmclose_probe.sh first if wanted, skipping" \
    "  (tools/userland_staging/build_wmclose_probe.sh)"
install_smoketest tools/userland_staging/xtest_type.macho "usr/X11/bin/xtest_type" \
    "= tools/userland_staging/xtest_type.macho not built -- run build_xtest_type.sh first if wanted, skipping" \
    "  (tools/userland_staging/build_xtest_type.sh: XTEST typing client for greeter tests)"
# DAR-431: Window Maker event-loop test aids (QEMU-only).
install_smoketest tools/userland_staging/wutil_kq_test.macho "usr/X11/bin/wutil_kq_test" \
    "= tools/userland_staging/wutil_kq_test.macho not built -- run build_wutil_kq_test.sh first if wanted, skipping" \
    "  (tools/userland_staging/build_wutil_kq_test.sh: libWUtil kqueue/select input-wait test)"
install_smoketest tools/userland_staging/wutil_timer_test.macho "usr/X11/bin/wutil_timer_test" \
    "= tools/userland_staging/wutil_timer_test.macho not built -- run build_wutil_timer_test.sh first if wanted, skipping" \
    "  (tools/userland_staging/build_wutil_timer_test.sh: DAR-432 timer clock-step and leeway test)"
install_smoketest tools/userland_staging/wutil_vnode_test.macho "usr/X11/bin/wutil_vnode_test" \
    "= tools/userland_staging/wutil_vnode_test.macho not built -- run build_wutil_vnode_test.sh first if wanted, skipping" \
    "  (tools/userland_staging/build_wutil_vnode_test.sh: DAR-433 EVFILT_VNODE delivery test)"
install_smoketest tools/userland_staging/qos_probe.macho "usr/X11/bin/qos_probe" \
    "= tools/userland_staging/qos_probe.macho not built -- run build_qos_probe.sh first if wanted, skipping" \
    "  (tools/userland_staging/build_qos_probe.sh: thread QoS / priority probe, DAR-439)"
install_smoketest tools/userland_staging/dispatch_stall_test.macho "usr/X11/bin/dispatch_stall_test" \
    "= tools/userland_staging/dispatch_stall_test.macho not built -- run build_dispatch_stall_test.sh first if wanted, skipping" \
    "  (tools/userland_staging/build_dispatch_stall_test.sh: DAR-409 libdispatch pool stall monitor test)"
install_smoketest tools/userland_staging/spawn_test.macho "usr/X11/bin/spawn_test" \
    "= tools/userland_staging/spawn_test.macho not built -- run build_spawn_test.sh first if wanted, skipping" \
    "  (tools/userland_staging/build_spawn_test.sh: DAR-434 posix_spawn(p) attributes + EVFILT_PROC test)"
install_smoketest tools/userland_staging/wmlat.macho "usr/X11/bin/wmlat" \
    "= tools/userland_staging/wmlat.macho not built -- run build_wmlat.sh first if wanted, skipping" \
    "  (tools/userland_staging/build_wmlat.sh: Window Maker event latency probe)"
install_smoketest tools/userland_staging/x11_probe.macho "usr/X11/bin/x11_probe" \
    "= tools/userland_staging/x11_probe.macho not built -- run build_x11_probe.sh first if wanted, skipping" \
    "  (tools/userland_staging/build_x11_probe.sh: window tree / XTest click client)"

# Font/2D stack (DAR-384). The dylibs (libz, libexpat, freetype, libpng,
# pixman, fontconfig, cairo, libXft) go in through the generic
# libc_build/system/*.dylib loop below. Here: the staged data tree
# (build_fontconfig.sh: /etc/fonts, /usr/X11/share/fontconfig,
# /usr/X11/share/xml/fontconfig, /var/cache/fontconfig; build_fonts.sh:
# /usr/share/fonts with the DejaVu TTFs + fonts.scale/fonts.dir), the
# fc-* tools, and cairo_smoketest. ditto keeps /etc/fonts/conf.d's
# relative symlinks as symlinks. The fontconfig cache is NOT prebuilt:
# the first client (or `fc-cache`) writes it on the target.
FS_STAGE=tools/userland_staging/libc_build/fontstack
if [ -d "$FS_STAGE/root" ]; then
    need_kb=$(du -sk "$FS_STAGE/root" | awk '{print $1}')
    free_kb=$(df -k "$MNT" | awk 'NR==2{print $4}')
    if [ "$free_kb" -lt $((need_kb + 51200)) ]; then
        echo "error: $MNT has ${free_kb} KB free; the font/2D data tree needs ${need_kb} KB (+50 MB headroom)" >&2
        exit 1
    fi
    ditto "$FS_STAGE/root" "$MNT/"
    chmod 755 "$MNT/var/cache/fontconfig"
    echo "+ wrote font/2D data tree ($need_kb KB: /etc/fonts, /usr/share/fonts, /usr/X11/share/{fontconfig,xml}, /var/cache/fontconfig; $free_kb KB was free)"
else
    echo "= $FS_STAGE/root not staged -- run build_fontconfig.sh and build_fonts.sh first if wanted, skipping"
fi
if [ -d "$FS_STAGE/bin" ]; then
    mkdir -p "$MNT/usr/X11/bin"
    for t in "$FS_STAGE"/bin/fc-*.macho; do
        [ -f "$t" ] || continue
        name="$(basename "$t" .macho)"
        cp "$t" "$MNT/usr/X11/bin/$name"
        chmod 755 "$MNT/usr/X11/bin/$name"
    done
    echo "+ wrote fontconfig tools to $MNT/usr/X11/bin ($(ls "$FS_STAGE"/bin/fc-*.macho | wc -l | tr -d ' ') of them)"
fi
# MIME database (shared-mime-info): GIO looks a file's content type up in /usr/share/mime/mime.cache. build_shared_mime_info.sh
# generates it on the host with update-mime-database; without it WebKit (and anything on GIO) sees every file as
# application/octet-stream. ditto keeps the directory layout.
MIME_STAGE=tools/userland_staging/libc_build/shared_mime_info/root/usr/share/mime
if [ -f "$MIME_STAGE/mime.cache" ]; then
    mkdir -p "$MNT/usr/share"
    ditto "$MIME_STAGE" "$MNT/usr/share/mime"
    echo "+ wrote MIME database ($(du -sk "$MIME_STAGE" | cut -f1) KB) to $MNT/usr/share/mime"
else
    echo "= $MIME_STAGE not built -- run tools/userland_staging/build_shared_mime_info.sh first if wanted, skipping (GIO then types every file application/octet-stream)"
fi
# terminfo database (DAR-293): libncurses here is built without one
# (build_ncurses.sh), so top, vim and libedit fail with "error opening terminal:
# linux" (Terminal.app exports TERM=linux). build_terminfo.sh compiles a small
# set of real ncurses terminfo.src entries with the host tic; ditto keeps the
# hex-named directories.
TI_STAGE=tools/userland_staging/libc_build/terminfo/root
if [ -d "$TI_STAGE/usr/share/terminfo" ]; then
    mkdir -p "$MNT/usr/share"
    ditto "$TI_STAGE/usr/share/terminfo" "$MNT/usr/share/terminfo"
    echo "+ wrote terminfo database ($(find "$TI_STAGE/usr/share/terminfo" -type f | wc -l | tr -d ' ') entries) to $MNT/usr/share/terminfo"
else
    echo "= $TI_STAGE not built -- run tools/userland_staging/build_terminfo.sh first if wanted, skipping (top/vim/libedit then fail with 'error opening terminal')"
fi
# GNUstep (DAR-388): gnustep-make, gnustep-base/gui/back, the themes and
# the apps as the build_gnustep_*.sh scripts install them (gnustep-make's
# "gnustep" layout under /usr/GNUstep, in libc_build/gnustep/root), placed
# on the image in the Mac layout (gnustep-make's FilesystemLayouts/mac):
# apps in /Applications, the system Library in /System/Library, tools in
# /usr/local/bin, /etc/GNUstep/GNUstep.conf pointing at them, and
# compatibility symlinks at the old /usr/GNUstep/System/{Applications,
# Library,Tools} so the libraries' unchanged /usr/GNUstep install names
# still resolve. stage_gnustep_mac_layout.sh builds that tree (see its
# header for why no library is rebuilt). ditto keeps the version symlinks.
GS_STAGE=tools/userland_staging/libc_build/gnustep/root
if [ -f "$GS_STAGE/usr/GNUstep/System/Library/Libraries/libgnustep-base.dylib" ]; then
    GS_MAC=$(mktemp -d "${TMPDIR:-/tmp}/gnustep-mac.XXXXXX")
    tools/userland_staging/stage_gnustep_mac_layout.sh "$GS_STAGE" "$GS_MAC/root"
    need_kb=$(du -sk "$GS_MAC/root" | awk '{print $1}')
    free_kb=$(df -k "$MNT" | awk 'NR==2{print $4}')
    if [ "$free_kb" -lt $((need_kb + 51200)) ]; then
        echo "error: $MNT has ${free_kb} KB free; the GNUstep tree needs ${need_kb} KB (+50 MB headroom)" >&2
        rm -rf "$GS_MAC"
        exit 1
    fi
    # An image injected before the Mac layout has real directories at the
    # old paths; they hold nothing but GNUstep's own files. Replace them
    # with the compatibility symlinks rather than ditto'ing over them.
    for d in Applications Library Tools; do
        if [ -d "$MNT/usr/GNUstep/System/$d" ] && [ ! -L "$MNT/usr/GNUstep/System/$d" ]; then
            rm -rf "${MNT:?}/usr/GNUstep/System/$d"
            echo "+ removed old-layout $MNT/usr/GNUstep/System/$d"
        fi
    done
    ditto "$GS_MAC/root" "$MNT/"
    rm -rf "$GS_MAC"
    for d in Applications Library Tools; do
        [ -L "$MNT/usr/GNUstep/System/$d" ] ||
            { echo "error: $MNT/usr/GNUstep/System/$d is not the compatibility symlink" >&2; exit 1; }
    done
    [ -x "$MNT/usr/GNUstep/System/Library/Libraries/libgnustep-base.dylib" ] ||
        { echo "error: libgnustep-base does not resolve at its install name on $MNT" >&2; exit 1; }
    echo "+ wrote GNUstep tree ($need_kb KB: /Applications, /System/Library, /Library/Documentation, /usr/local/bin, /etc/GNUstep/GNUstep.conf; /usr/GNUstep/System/{Applications,Library,Tools} symlinks)"
else
    echo "= $GS_STAGE not built -- run tools/userland_staging/build_gnustep_base.sh first if wanted, skipping"
fi
# X servers (DAR-387/389, X11GS 4/7): Xvfb, Xorg, xkbcomp and the test
# clients in /usr/X11/bin, the puredarwingop video driver and (DAR-391) the
# iohid input driver under
# /usr/X11/lib/xorg/modules/{drivers,input}, /etc/X11/xorg.conf, and the XKB keymap
# database in /usr/X11/share/xkeyboard-config-2 (with its
# /usr/X11/share/X11/xkb symlink). Staged by build_xserver.sh,
# build_xf86_input_iohid.sh,
# build_xf86_video_puredarwingop.sh, build_x11_clients.sh, build_xkbcomp.sh
# and build_xkeyboard_config.sh. The servers' own dylibs (libXfont2,
# libfontenc, libxkbfile, libxcvt, ...) are installed by the generic
# libc_build/system/*.dylib loop below. ditto keeps the xkb symlink.
XS_STAGE=tools/userland_staging/libc_build/x11/server/root
if [ -x "$XS_STAGE/usr/X11/bin/Xvfb" ]; then
    need_kb=$(du -sk "$XS_STAGE" | awk '{print $1}')
    free_kb=$(df -k "$MNT" | awk 'NR==2{print $4}')
    if [ "$free_kb" -lt $((need_kb + 51200)) ]; then
        echo "error: $MNT has ${free_kb} KB free; the X server tree needs ${need_kb} KB (+50 MB headroom)" >&2
        exit 1
    fi
    ditto "$XS_STAGE" "$MNT/"
    # xkb/ddxLoad.c falls back to /tmp when this is not writable; created
    # here as well because build_xkeyboard_config.sh rebuilds that tree.
    mkdir -p "$MNT/usr/X11/share/xkeyboard-config-2/compiled"
    chmod 755 "$MNT/usr/X11/share/xkeyboard-config-2/compiled"
    echo "+ wrote X server tree ($need_kb KB: /usr/X11/bin/{Xvfb,Xorg,xkbcomp,...}, /usr/X11/lib/xorg/modules, /etc/X11/xorg.conf, /usr/X11/share/xkeyboard-config-2)"
else
    echo "= $XS_STAGE/usr/X11/bin/Xvfb not built -- run build_xserver.sh (and build_xkbcomp.sh, build_xkeyboard_config.sh) first if wanted, skipping"
fi
install_smoketest tools/userland_staging/cairo_smoketest.macho "usr/X11/bin/cairo_smoketest" \
    "= tools/userland_staging/cairo_smoketest.macho not built -- run build_cairo_smoketest.sh first if wanted, skipping" \
    "  (tools/userland_staging/build_cairo_smoketest.sh)"

if [ -f tools/userland_staging/netstat.macho ]; then
    mkdir -p "$MNT/usr/sbin"
    cp tools/userland_staging/netstat.macho "$MNT/usr/sbin/netstat"
    chmod 755 "$MNT/usr/sbin/netstat"
    echo "+ wrote $(wc -c < tools/userland_staging/netstat.macho) bytes to $MNT/usr/sbin/netstat"
    echo "  (real, unmodified Apple netstat -- see"
    echo "  tools/userland_staging/build_netstat.sh)"
else
    echo "= tools/userland_staging/netstat.macho not built -- run build_netstat.sh first if wanted, skipping"
fi

if [ -f tools/userland_staging/ping.macho ]; then
    mkdir -p "$MNT/sbin"
    cp tools/userland_staging/ping.macho "$MNT/sbin/ping"
    chmod 755 "$MNT/sbin/ping"
    echo "+ wrote $(wc -c < tools/userland_staging/ping.macho) bytes to $MNT/sbin/ping"
    echo "  (real, unmodified Apple network_cmds ping -- Phase 6c, see"
    echo "  tools/userland_staging/build_ping.sh; numeric-address-only in"
    echo "  practice -- gethostbyname2()/gethostbyaddr() honestly degrade,"
    echo "  no resolver daemon on this target yet)"
else
    echo "= tools/userland_staging/ping.macho not built -- run build_ping.sh first if wanted, skipping"
fi

# real vipw.tproj (DAR-174, Phase 5 account-provisioning tools):
# real, unmodified system_cmds/vipw.tproj/vipw.c + pw_util.c, installed
# at the real Apple path (/usr/sbin/vipw, confirmed against
# system_cmds.xcodeproj's own INSTALL_PATH, not guessed). Not wired into
# any boot-path exec chain -- an admin invokes it manually from a
# logged-in root shell. pw_util.c's own pw_mkdb() execs a separate
# pwd_mkdb binary to rebuild /etc/pwd.db + /etc/spwd.db -- real,
# unmodified pwd_mkdb.macho is now also built and installed below
# (DAR-174's Berkeley DB port, see build_libc_db.sh/build_pwd_mkdb.sh),
# so this real exec path now actually resolves instead of degrading.
# Not yet QEMU-boot-tested end to end (DAR-174's own build pass
# deliberately stayed off QEMU to avoid clashing with concurrent QEMU
# use elsewhere) -- real interactive verification is a recommended
# follow-up.
if [ -f tools/userland_staging/vipw.macho ]; then
    mkdir -p "$MNT/usr/sbin"
    cp tools/userland_staging/vipw.macho "$MNT/usr/sbin/vipw"
    chmod 755 "$MNT/usr/sbin/vipw"
    echo "+ wrote $(wc -c < tools/userland_staging/vipw.macho) bytes to $MNT/usr/sbin/vipw"
    echo "  (real, unmodified Apple vipw -- DAR-174, see"
    echo "  tools/userland_staging/build_vipw.sh; pw_mkdb() now has a real"
    echo "  pwd_mkdb to exec, see below)"
else
    echo "= tools/userland_staging/vipw.macho not built -- run build_vipw.sh first if wanted, skipping"
fi

# real pwd_mkdb.tproj (DAR-174, Phase 5 account-provisioning tools):
# real, unmodified system_cmds/pwd_mkdb.tproj/pwd_mkdb.c + pw_scan.c,
# installed at the real Apple path (/usr/sbin/pwd_mkdb, confirmed
# against the real Apple iPhoneOS SDK's own pwd.h `_PATH_PWD_MKDB`
# constant, not guessed). Needed real Berkeley DB (dbopen()/DB_HASH,
# this project's first-ever port of it -- see
# tools/userland_staging/build_libc_db.sh's own header for the full
# scope/citation: db/+hash/ only, btree/recno/mpool deliberately not
# ported since nothing here calls dbopen() with those types). Not wired
# into any boot-path exec chain itself -- vipw/chpass/passwd's own
# pw_mkdb() helper execs it by absolute path. Not yet QEMU-boot-tested
# end to end -- real interactive verification (actually editing
# /etc/master.passwd and confirming /etc/pwd.db + /etc/spwd.db get
# regenerated) is a recommended follow-up.
if [ -f tools/userland_staging/pwd_mkdb.macho ]; then
    mkdir -p "$MNT/usr/sbin"
    cp tools/userland_staging/pwd_mkdb.macho "$MNT/usr/sbin/pwd_mkdb"
    chmod 755 "$MNT/usr/sbin/pwd_mkdb"
    echo "+ wrote $(wc -c < tools/userland_staging/pwd_mkdb.macho) bytes to $MNT/usr/sbin/pwd_mkdb"
    echo "  (real, unmodified Apple pwd_mkdb -- DAR-174, see"
    echo "  tools/userland_staging/build_pwd_mkdb.sh; first real consumer of"
    echo "  this project's own new Berkeley DB port, build_libc_db.sh)"
    # DAR-341: one-shot first-boot job that builds pwd.db/spwd.db, which
    # pw(8)'s incremental `pwd_mkdb -u` needs; see the plist's header for
    # why the image builder cannot do it on the Mac.
    mkdir -p "$MNT/Library/LaunchDaemons"
    cp tools/userland_staging/org.puredarwin.pwd_mkdb.plist \
        "$MNT/Library/LaunchDaemons/org.puredarwin.pwd_mkdb.plist"
    chmod 644 "$MNT/Library/LaunchDaemons/org.puredarwin.pwd_mkdb.plist"
    echo "+ wrote org.puredarwin.pwd_mkdb.plist (first-boot pwd.db/spwd.db build)"
else
    echo "= tools/userland_staging/pwd_mkdb.macho not built -- run build_pwd_mkdb.sh first if wanted, skipping"
fi

# real passwd.tproj (DAR-174, Phase 5 account-provisioning tools):
# real, unmodified system_cmds/passwd.tproj/passwd.c + file_passwd.c,
# installed at the real Apple path (/usr/bin/passwd, confirmed against
# system_cmds.xcodeproj's own INSTALL_PATH). Built with the real
# iPhoneOS SDK device target, which mechanically excludes passwd.h's
# own NIS/OpenDirectory/PAM branches (TARGET_OS_IPHONE &&
# !TARGET_OS_SIMULATOR is false only on a real device build -- see
# build_passwd.sh's own header for the exact real-source citation), so
# this binary takes the same real, designed-in INFO_FILE branch as
# Apple's own device build -- not a project-side workaround. Real,
# named runtime gap this leaves (already true of Apple's own
# file_passwd.c, not introduced here): it rewrites /etc/master.passwd
# in place but does not regenerate /etc/pwd.db / /etc/spwd.db (no
# pwd_mkdb call at all in this code path); harmless as long as
# consumers read /etc/master.passwd directly (this project's current
# Libinfo resolvers do), stale otherwise. Not yet QEMU-boot-tested end
# to end -- real interactive verification is a recommended follow-up.
if [ -f tools/userland_staging/passwd.macho ]; then
    mkdir -p "$MNT/usr/bin"
    cp tools/userland_staging/passwd.macho "$MNT/usr/bin/passwd"
    chmod 755 "$MNT/usr/bin/passwd"
    echo "+ wrote $(wc -c < tools/userland_staging/passwd.macho) bytes to $MNT/usr/bin/passwd"
    echo "  (real, unmodified Apple passwd -- DAR-174, see"
    echo "  tools/userland_staging/build_passwd.sh; FILE infosystem only, no"
    echo "  NIS/OpenDirectory/PAM on a real device target)"
else
    echo "= tools/userland_staging/passwd.macho not built -- run build_passwd.sh first if wanted, skipping"
fi

# real chpass.tproj (DAR-174, Phase 5 account-provisioning tools --
# 4th and final tool of this ticket): real, unmodified system_cmds/
# chpass.tproj/{chpass,edit,field,table,util,pw_copy}.c, installed at
# the real Apple path (/usr/bin/chpass, confirmed against
# system_cmds.xcodeproj's own INSTALL_PATH). Built non-OPEN_DIRECTORY
# (same real device-target reasoning as passwd.tproj above -- Apple's
# own build always defines OPEN_DIRECTORY, so this branch, and this
# exact real gap, was apparently never build-tested by Apple either;
# see build_chpass.sh's own header for the full citation). Needed
# real FreeBSD lib/libutil/pw_util.c + lib/libc/gen/pw_scan.c +
# lib/libutil/flopen.c (chpass_pw_util_freebsd.c/chpass_pw_scan_
# freebsd.c/chpass_flopen_freebsd.c) since neither Apple's public SDK
# nor this project's own vendored Libc ever defined pw_init(dir,master)/
# pw_fini()/pw_tempname()/pw_equal()/__pw_scan() etc.
#
# REAL, CONFIRMED UPSTREAM APPLE BUG this build surfaces but does not
# fix (fixing it would mean patching real Apple source, out of scope
# here): chpass.c's own real, unmodified non-OPEN_DIRECTORY tail has
# `exit(0);` as its literal first statement (chpass.c, right after the
# password re-entry check) -- BEFORE ever calling pw_copy()/pw_lock()/
# pw_mkdb() to actually persist an edited entry. So on a real device
# build, real Apple's own chpass(1) validates the edit and exits
# without saving; this port reproduces that behavior exactly (byte-for-
# byte real source), it does not paper over it. See this ticket's own
# Linear comment for the full writeup.
if [ -f tools/userland_staging/chpass.macho ]; then
    mkdir -p "$MNT/usr/bin"
    cp tools/userland_staging/chpass.macho "$MNT/usr/bin/chpass"
    chmod 755 "$MNT/usr/bin/chpass"
    echo "+ wrote $(wc -c < tools/userland_staging/chpass.macho) bytes to $MNT/usr/bin/chpass"
    echo "  (real, unmodified Apple chpass -- DAR-174, see"
    echo "  tools/userland_staging/build_chpass.sh; real confirmed upstream"
    echo "  exit(0)-before-save bug in this exact build config, not fixed here)"
else
    echo "= tools/userland_staging/chpass.macho not built -- run build_chpass.sh first if wanted, skipping"
fi

# real FreeBSD usr.sbin/pw (DAR-341, user-management commands --
# useradd/userdel/usermod/groupadd/... with no separate "adduser"
# wrapper, matching real FreeBSD's own single-binary design): installed
# at /usr/sbin/pw, the real FreeBSD BINDIR for usr.sbin/pw (there is no
# Apple-sourced pw(8) to cite an INSTALL_PATH from - this tool is
# FreeBSD-only, unlike vipw/passwd/chpass/pwd_mkdb above, which are all
# real Apple system_cmds). build_pw.sh itself was real and complete
# (produces pw.macho, real compile+link+smoketest) but this install
# step was never added - found 2026-09-21 when the user looked for `pw`
# on a real device and it was missing, despite DAR-341 tracking it as
# in progress rather than blocked on anything.
if [ -f tools/userland_staging/pw.macho ]; then
    mkdir -p "$MNT/usr/sbin"
    cp tools/userland_staging/pw.macho "$MNT/usr/sbin/pw"
    chmod 755 "$MNT/usr/sbin/pw"
    echo "+ wrote $(wc -c < tools/userland_staging/pw.macho) bytes to $MNT/usr/sbin/pw"
    echo "  (real, unmodified FreeBSD pw(8) -- DAR-341, see"
    echo "  tools/userland_staging/build_pw.sh)"
    # pw(8)'s compiled-in defaults are FreeBSD's (/home, /bin/sh); this
    # file points new accounts at /Users/<name> with /bin/bash instead.
    mkdir -p "$MNT/etc"
    cp tools/userland_staging/pw.conf "$MNT/etc/pw.conf"
    chmod 644 "$MNT/etc/pw.conf"
    echo "+ wrote $MNT/etc/pw.conf (home=/Users, defaultshell=bash)"
else
    echo "= tools/userland_staging/pw.macho not built -- run build_pw.sh first if wanted, skipping"
fi

# cron.macho/crontab.macho intentionally NOT installed here (2026-08-22).
# Both build real (tools/userland_staging/build_cron.sh still produces
# working Mach-O binaries -- real DYNAMIC ones as of 2026-09-15, DAR-314;
# they were fully static when this comment was first written -- and can
# be run/inspected on their own) but cron.macho itself has a real,
# unresolved startup crash --
# SIGSEGV in open_pidfile()'s snprintf() call, immediately after this
# same process's own signal(SIGCHLD/SIGCLD, ...) + signal(SIGHUP, ...)
# registrations, bisected with temporary write(2) checkpoints down to
# that exact line but not root-caused further. Shipping a daemon that
# reliably crashes on every boot is worse than not shipping it (spams
# the console right at the login prompt, leaves an unreaped zombie --
# the full writeup used to be cited via the now-retired
# tools/init_binary/init.c's spawn_cron() comment, content not
# preserved elsewhere -- see docs/static-linking-cleanup-plan.md).
# crontab(1) itself has two separate real bugs already fixed
# (a broken pthread_once()-based COMPAT_MODE, a broken setlinebuf()) but
# was never fully confirmed end-to-end (a host-side QEMU test image
# journal issue blocked the last verification pass, unrelated to the
# code itself) -- left uninstalled alongside cron rather than shipped
# half-verified. See docs/daemon-roadmap.md's Phase 5.5 item 3 for the
# complete writeup and what a future pass needs to finish this for real.

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

if [ -f tools/ulock_kthread_test/ulock_kthread_test.macho ]; then
    mkdir -p "$MNT/bin"
    cp tools/ulock_kthread_test/ulock_kthread_test.macho "$MNT/bin/ulock_kthread_test"
    chmod 755 "$MNT/bin/ulock_kthread_test"
    echo "+ wrote $(wc -c < tools/ulock_kthread_test/ulock_kthread_test.macho) bytes to $MNT/bin/ulock_kthread_test"
    echo "  (Phase 7.5 real ulock_wait/ulock_wake blocking-handoff smoke test --"
    echo "  run it from the shell, see tools/ulock_kthread_test/ulock_kthread_test.c)"
else
    echo "= tools/ulock_kthread_test/ulock_kthread_test.macho not built -- run tools/ulock_kthread_test/build.py first if wanted, skipping"
fi

install_smoketest tools/userland_staging/libc_build/pthread_smoketest.macho "bin/pthread_smoketest" \
    "= tools/userland_staging/libc_build/pthread_smoketest.macho not built -- run tools/userland_staging/build_pthread_smoketest.sh first if wanted, skipping" \
    "  (libpthread smoketest, dynamic against libsystem_pthread.dylib (DAR-127) -- real pthread_create()" \
    "  second thread + contended pthread_mutex under ulock, 2026-08-24)"

if [ -f tools/userland_staging/dyld.macho ] && [ -f tools/userland_staging/dyld_test_exec.macho ]; then
    mkdir -p "$MNT/usr/lib" "$MNT/bin"
    cp tools/userland_staging/dyld.macho "$MNT/usr/lib/dyld"
    chmod 755 "$MNT/usr/lib/dyld"
    echo "+ wrote $(wc -c < tools/userland_staging/dyld.macho) bytes to $MNT/usr/lib/dyld"
    if [ -f tools/userland_staging/libc_build/libtest.dylib ]; then
        cp tools/userland_staging/libc_build/libtest.dylib "$MNT/usr/lib/libtest.dylib"
        chmod 755 "$MNT/usr/lib/libtest.dylib"
        echo "+ wrote $(wc -c < tools/userland_staging/libc_build/libtest.dylib) bytes to $MNT/usr/lib/libtest.dylib"
    fi
    # Real, unconditional dyldMain.cpp requirement (found+closed
    # 2026-08-25, see build_libdyld.sh/build_libsystem.sh's own header
    # comments): every launched process must load both of these at their
    # exact real install_names.
    if [ -f tools/userland_staging/libc_build/system/libdyld.dylib ]; then
        mkdir -p "$MNT/usr/lib/system"
        cp tools/userland_staging/libc_build/system/libdyld.dylib "$MNT/usr/lib/system/libdyld.dylib"
        chmod 755 "$MNT/usr/lib/system/libdyld.dylib"
        echo "+ wrote $(wc -c < tools/userland_staging/libc_build/system/libdyld.dylib) bytes to $MNT/usr/lib/system/libdyld.dylib"
    fi
    if [ -f tools/userland_staging/libc_build/system/libSystem.B.dylib ]; then
        cp tools/userland_staging/libc_build/system/libSystem.B.dylib "$MNT/usr/lib/libSystem.B.dylib"
        chmod 755 "$MNT/usr/lib/libSystem.B.dylib"
        echo "+ wrote $(wc -c < tools/userland_staging/libc_build/system/libSystem.B.dylib) bytes to $MNT/usr/lib/libSystem.B.dylib"
    fi
    # Static-to-dylib migration (docs/daemon-roadmap.md's "Static-vs-dylib
    # architecture debt" section): every other real per-library dylib
    # under libc_build/system/ (libsystem_kernel.dylib,
    # libsystem_platform.dylib, libsystem_blocks.dylib, and whatever
    # gets added in later migration batches) -- libdyld.dylib/
    # libSystem.B.dylib above have real LC_LOAD_DYLIB references to
    # these at their exact real install_names, resolved by dyld
    # recursively even though dyld_test_exec.macho itself never
    # references them directly, so each must be present at
    # /usr/lib/system/<name>.dylib for either of the two dylibs above to
    # load at all. Looped (not hand-listed one at a time) so a new
    # migration batch's dylib is picked up automatically the next time
    # this script runs, without needing its own edit here.
    # REAL BUG FOUND+FIXED 2026-08-28 (static-linking cleanup pass, Stage
    # B): this loop used to hardcode every dylib's destination to
    # /usr/lib/system/<name>, which only matches real Apple's own actual
    # install path for the libsystem_*/libdispatch/libunwind-class
    # dylibs -- confirmed via a real `otool -D` sweep of every dylib
    # under libc_build/system/ that several of this pass's own new
    # standalone dylibs (libresolv.dylib, libbsm.dylib, libncurses.dylib,
    # libiconv.dylib, libopenpam.dylib, libiokituser.dylib) carry a real
    # LC_ID_DYLIB install_name OUTSIDE /usr/lib/system/ (matching real
    # macOS's own actual paths, e.g. /usr/lib/libresolv.9.dylib) --
    # installing them at the old hardcoded path would leave dyld unable
    # to find them at their real referenced location, a real, silent
    # boot-time failure this generic loop's own "just works for whatever
    # gets added later" comment explicitly promised and didn't actually
    # deliver. Same real bug already existed for libxml2.dylib (real
    # install_name /usr/lib/libxml2.2.dylib) even before this pass,
    # latent only because nothing yet loads it at runtime -- confirmed
    # via the same otool sweep, not assumed. Real fix: read each dylib's
    # own actual LC_ID_DYLIB via `otool -D` and install it there,
    # instead of guessing a path from the filename. The old libc++.dylib
    # special case is subsumed by this (no longer needed) -- kept only
    # the libdyld.dylib/libSystem.B.dylib skip, since those two already
    # have their own dedicated, working install blocks directly above.
    mkdir -p "$MNT/usr/lib/system"
    for d in tools/userland_staging/libc_build/system/*.dylib; do
        [ -f "$d" ] || continue
        name="$(basename "$d")"
        case "$name" in
            libdyld.dylib|libSystem.B.dylib) continue ;;
        esac
        install_name="$(otool -D "$d" 2>/dev/null | tail -1)"
        # /usr/X11/lib/*: the X11 client libraries (DAR-383), installed
        # under Apple's own 10.5-10.7 X11 prefix -- see
        # tools/userland_staging/x11_common.sh's header.
        case "$install_name" in
            /usr/lib/*|/usr/X11/lib/*) : ;;
            *)
                echo "error: $d has no real LC_ID_DYLIB install_name (otool -D returned '$install_name') -- refusing to guess a destination path" >&2
                exit 1
                ;;
        esac
        dest="$MNT$install_name"
        mkdir -p "$(dirname "$dest")"
        cp "$d" "$dest"
        chmod 755 "$dest"
        echo "+ wrote $(wc -c < "$d") bytes to $dest (real install_name $install_name)"
    done
    cp tools/userland_staging/dyld_test_exec.macho "$MNT/bin/dyld_test_exec"
    chmod 755 "$MNT/bin/dyld_test_exec"
    echo "+ wrote $(wc -c < tools/userland_staging/dyld_test_exec.macho) bytes to $MNT/bin/dyld_test_exec"
    echo "  (Phase 6.5 Phase C -- REAL dyld-940, see"
    echo "  tools/userland_staging/build_dyld_core.sh + build_dyld_link.sh;"
    echo "  run /bin/dyld_test_exec from the shell -- this is the first real"
    echo "  boot test of an actual dyld binary loading a real dylib)"
elif [ -f tools/userland_staging/dyld_stub.macho ] && [ -f tools/userland_staging/dyld_test_exec.macho ]; then
    mkdir -p "$MNT/usr/lib" "$MNT/bin"
    cp tools/userland_staging/dyld_stub.macho "$MNT/usr/lib/dyld"
    chmod 755 "$MNT/usr/lib/dyld"
    echo "+ wrote $(wc -c < tools/userland_staging/dyld_stub.macho) bytes to $MNT/usr/lib/dyld"
    cp tools/userland_staging/dyld_test_exec.macho "$MNT/bin/dyld_test_exec"
    chmod 755 "$MNT/bin/dyld_test_exec"
    echo "+ wrote $(wc -c < tools/userland_staging/dyld_test_exec.macho) bytes to $MNT/bin/dyld_test_exec"
    echo "  (Phase 6.5 Phase B kernel-plumbing proof -- NOT real dyld, see"
    echo "  tools/userland_staging/dyld_stub_main.c; run /bin/dyld_test_exec from"
    echo "  the shell and check \$? == 42 -- confirms the kernel's real"
    echo "  load_dylinker()/LC_LOAD_DYLINKER handoff actually reaches"
    echo "  /usr/lib/dyld's entry point instead of the exec'd binary's own)"
else
    echo "= neither tools/userland_staging/dyld.macho nor dyld_stub.macho built -- run build_dyld_link.sh (real dyld) or build_dyld_stub.sh (kernel-plumbing-only proof) + build_dyld_test_exec.sh first if wanted, skipping"
fi

if [ -f tools/userland_staging/syslogd.macho ]; then
    mkdir -p "$MNT/usr/sbin" "$MNT/Library/LaunchDaemons" \
        "$MNT/etc" "$MNT/usr/local/etc/asl"
    cp tools/userland_staging/syslogd.macho "$MNT/usr/sbin/syslogd"
    chmod 755 "$MNT/usr/sbin/syslogd"
    echo "+ wrote $(wc -c < tools/userland_staging/syslogd.macho) bytes to $MNT/usr/sbin/syslogd"
    echo "  (Phase 6.7 legacy-scope syslogd, tools/userland_staging/build_syslogd.sh --"
    echo "  real dynamic Mach-O, spawned by real launchd.macho via a LaunchDaemons"
    echo "  plist -- init_binary's own custom spawn_syslogd() PID-1 no longer exists,"
    echo "  see docs/static-linking-cleanup-plan.md's static-linking-cleanup entry;"
    echo "  docs/daemon-roadmap.md for the real, current scope/gap list)"
    cp "third_party/syslog/syslogd.tproj/com.apple.syslogd.plist" \
        "$MNT/Library/LaunchDaemons/com.apple.syslogd.plist"
    chmod 644 "$MNT/Library/LaunchDaemons/com.apple.syslogd.plist"
    echo "+ wrote com.apple.syslogd.plist to $MNT/Library/LaunchDaemons/"
    echo "  (real, unmodified Apple plist, DAR-153; Sockets/BSDSystemLogger and"
    echo "  MachServices/com.apple.system.logger checkin both require the real"
    echo "  socketgroup_setup()/job_export()/launch_data_get_machport() fixes)"
    # DAR-257: separate from syslogd's own real _PATH_SYSLOGD_LOG
    # (/var/log/syslogd.log, ASL-formatted, its own application output) --
    # this captures raw stdio/stderr writes launchd's job would otherwise
    # send to /dev/console, the same real gap notifyd's plist fix (DAR-167)
    # and configd's (above) close, in case syslogd's own dispatch manager
    # thread is the one that hits the DISPATCH_CLIENT_CRASH("Unexpected
    # error from kevent") crash in a given non-deterministic boot.
    /usr/libexec/PlistBuddy -c "Add :StandardOutPath string /var/log/syslogd_stderr.log" \
        "$MNT/Library/LaunchDaemons/com.apple.syslogd.plist" 2>/dev/null
    /usr/libexec/PlistBuddy -c "Add :StandardErrorPath string /var/log/syslogd_stderr.log" \
        "$MNT/Library/LaunchDaemons/com.apple.syslogd.plist" 2>/dev/null
    echo "+ added StandardOutPath/StandardErrorPath -> /var/log/syslogd_stderr.log"
    echo "  to com.apple.syslogd.plist (DAR-257)"

    # DAR-250: syslogd's real asl_out_module_init() loads its base rules
    # from /etc/asl.conf and local modules from /usr/local/etc/asl/.  The
    # image previously created /var/log but staged neither input, leaving
    # a healthy daemon with no rule that writes system.log.  These are the
    # matching upstream iOS-target base configuration and the upstream
    # system.log module; the latter's own destination is /var/log/system.log.
    cp "third_party/syslog/syslogd.tproj/asl.conf.ios" "$MNT/etc/asl.conf"
    cp "third_party/syslog/syslogd.tproj/com.apple.system.log" \
        "$MNT/usr/local/etc/asl/com.apple.system.log"
    chmod 644 "$MNT/etc/asl.conf" "$MNT/usr/local/etc/asl/com.apple.system.log"
    echo "+ wrote real syslogd ASL rules to /etc/asl.conf and /usr/local/etc/asl/"
    echo "  (DAR-250; enables the real /var/log/system.log output rule)"
else
    echo "= tools/userland_staging/syslogd.macho not built -- run tools/userland_staging/build_syslogd.sh first if wanted, skipping"
fi

if [ -f tools/userland_staging/notifyd.macho ]; then
    mkdir -p "$MNT/usr/sbin" "$MNT/Library/LaunchDaemons" "$MNT/var/log"
    cp tools/userland_staging/notifyd.macho "$MNT/usr/sbin/notifyd"
    chmod 755 "$MNT/usr/sbin/notifyd"
    echo "+ wrote $(wc -c < tools/userland_staging/notifyd.macho) bytes to $MNT/usr/sbin/notifyd"
    echo "  (DAR-167 -- real notifyd.tproj source (apple-oss-distributions/Libnotify,"
    echo "  via third_party/PureDarwin's own copy), tools/userland_staging/build_notifyd.sh --"
    echo "  real dynamic Mach-O, checks in against real launchd's bootstrap namespace"
    echo "  exactly like com.apple.SystemConfiguration.configd already does, see that"
    echo "  script's own header comment for the full real citation)"
    cp "third_party/PureDarwin/src/Libraries/XPC/notify/com.apple.notifyd.plist" \
        "$MNT/Library/LaunchDaemons/com.apple.notifyd.plist"
    chmod 644 "$MNT/Library/LaunchDaemons/com.apple.notifyd.plist"
    # DAR-167 (2026-09-06): real launchd never wires an unconfigured job's
    # stdio to /dev/console -- confirmed via a real QEMU boot showing
    # zero output from notifyd's own dyld halt()/console() messages (which
    # do real write(2, ..., fd=2) calls) even with -user_fault_debug
    # active. StandardOutPath/StandardErrorPath are real, standard launchd
    # plist keys (not a project invention); pointing them at a real file
    # is the only way to see a launchd-spawned notifyd's own stderr at
    # all, matching DAR-250's own real /var/log precedent for syslogd.
    # This is a real, deliberate divergence from PureDarwin's own
    # unmodified plist content (which is otherwise still installed as-is
    # above) -- not a change to the vendored file itself.
    /usr/libexec/PlistBuddy -c "Add :StandardOutPath string /var/log/notifyd.log" \
        "$MNT/Library/LaunchDaemons/com.apple.notifyd.plist" 2>/dev/null
    /usr/libexec/PlistBuddy -c "Add :StandardErrorPath string /var/log/notifyd.log" \
        "$MNT/Library/LaunchDaemons/com.apple.notifyd.plist" 2>/dev/null
    echo "+ wrote com.apple.notifyd.plist to $MNT/Library/LaunchDaemons/"
    echo "  (real, unmodified PureDarwin plist plus this script's own added"
    echo "  StandardOutPath/StandardErrorPath -> /var/log/notifyd.log (DAR-167,"
    echo "  see comment above) -- MachServices/com.apple.system.notification_center"
    echo "  checkin, RunAtLoad, ProgramArguments /usr/sbin/notifyd, matches this"
    echo "  script's real install path above; deliberately NOT real Apple's own"
    echo "  fuller plist -- PublishesEvents/EnablePressuredExit/POSIXSpawnType are"
    echo "  left out on purpose, since this project has no real XPC-events"
    echo "  registrar for PublishesEvents to mean anything to launchd, see"
    echo "  build_notifyd.sh's own header comment/DAR-268)"
else
    echo "= tools/userland_staging/notifyd.macho not built -- run tools/userland_staging/build_notifyd.sh first if wanted, skipping"
fi

if [ -f tools/userland_staging/chronyd.macho ]; then
    # Real gap, found by code review: chrony's own real CNF_CreateDirs()
    # (third_party/chrony/conf.c) only creates the directories for
    # bind_cmd_path/logdir/dumpdir/nts_dump_dir -- NOT the driftfile's
    # directory. chrony.conf sets `driftfile /var/lib/chrony/drift`, and
    # util.c's UTI_OpenFile() opens that path with a plain open(...,
    # O_CREAT, ...), no mkdir -- so without this, every
    # update_drift_file() write silently fails ENOENT (logged LOGS_ERR,
    # non-fatal) and drift persistence across reboots never works.
    # /var/run/chrony IS still real-created by CNF_CreateDirs() itself
    # (it owns the cmdmon socket's directory), so only /var/lib/chrony
    # needs a manual mkdir here.
    mkdir -p "$MNT/usr/sbin" "$MNT/Library/LaunchDaemons" "$MNT/etc" "$MNT/var/log" "$MNT/var/lib/chrony"
    cp tools/userland_staging/chronyd.macho "$MNT/usr/sbin/chronyd"
    chmod 755 "$MNT/usr/sbin/chronyd"
    echo "+ wrote $(wc -c < tools/userland_staging/chronyd.macho) bytes to $MNT/usr/sbin/chronyd"
    echo "  (DAR-169 -- real upstream chrony (this project's own fork,"
    echo "  git@github.com:anurodhp/chrony.git, branch iokit-port, tag 4.9),"
    echo "  tools/userland_staging/build_chrony.sh -- real dynamic Mach-O, see"
    echo "  that script's own header comment for the full real build story)"
    cp tools/userland_staging/org.chrony.chronyd.plist \
        "$MNT/Library/LaunchDaemons/org.chrony.chronyd.plist"
    chmod 644 "$MNT/Library/LaunchDaemons/org.chrony.chronyd.plist"
    echo "+ wrote org.chrony.chronyd.plist to $MNT/Library/LaunchDaemons/"
    cp tools/userland_staging/chrony.conf "$MNT/etc/chrony.conf"
    chmod 644 "$MNT/etc/chrony.conf"
    echo "+ wrote chrony.conf to $MNT/etc/"
    echo "+ ensured $MNT/var/lib/chrony exists (driftfile's own directory --"
    echo "  real chrony's CNF_CreateDirs() does NOT create this one, see"
    echo "  this block's own comment above)"
    echo "  (/var/run/chrony IS created by chronyd itself at startup,"
    echo "  CNF_CreateDirs() -- real chrony behavior, no manual mkdir needed"
    echo "  for that one)"
else
    echo "= tools/userland_staging/chronyd.macho not built -- run tools/userland_staging/build_chrony.sh first if wanted, skipping"
fi

# DAR-330: fake-hwclock. The Pi 3 has no RTC, so every boot starts at
# 1970 until chronyd syncs; the resulting multi-decade step under running
# daemons broke sshd sessions (DAR-377). Restores the last saved time at
# boot (forward only) and saves it every 15 min and on launchd's SIGTERM.
mkdir -p "$MNT/usr/libexec" "$MNT/Library/LaunchDaemons" "$MNT/var/db" "$MNT/var/log"
cp tools/userland_staging/fake-hwclock.sh "$MNT/usr/libexec/fake-hwclock"
chmod 755 "$MNT/usr/libexec/fake-hwclock"
cp tools/userland_staging/org.puredarwin.fake-hwclock.plist \
    "$MNT/Library/LaunchDaemons/org.puredarwin.fake-hwclock.plist"
chmod 644 "$MNT/Library/LaunchDaemons/org.puredarwin.fake-hwclock.plist"
# Seed with the image's build time so even a first boot starts in the
# right decade.
[ -f "$MNT/var/db/fake-hwclock" ] || date +%s > "$MNT/var/db/fake-hwclock"
echo "+ wrote fake-hwclock + org.puredarwin.fake-hwclock.plist (DAR-330)"

install_smoketest tools/userland_staging/sc_client_smoketest.macho "usr/local/bin/sc_client_smoketest" \
    "= sc_client_smoketest.macho not built -- run tools/userland_staging/build_libsystemconfiguration_client.sh first if wanted, skipping" \
    "  (real libSystemConfiguration.dylib client -- SCDynamicStoreCreate()" \
    "  against the real configd; tools/userland_staging/" \
    "  build_libsystemconfiguration_client.sh. Installed as of DAR-300, which" \
    "  is what made it loadable: it used to need a CFURL flat-namespace" \
    "  definition it did not carry, and linked against a dyld_stub_binder" \
    "  this project's libSystem does not export.)"

install_smoketest tools/userland_staging/libc_build/notifyd_roundtrip_smoketest.macho "usr/local/bin/notifyd_roundtrip_smoketest" \
    "= notifyd_roundtrip_smoketest.macho not built -- run tools/userland_staging/build_notifyd_smoketest.sh first if wanted, skipping" \
    "  (DAR-167 real end-to-end verification client, run by hand from the root" \
    "  shell post-boot -- tools/userland_staging/build_notifyd_smoketest.sh)"

install_smoketest tools/userland_staging/libc_build/res_init_dnsinfo_smoketest.macho "usr/local/bin/res_init_dnsinfo_smoketest" \
    "= res_init_dnsinfo_smoketest.macho not built -- run tools/userland_staging/build_res_init_dnsinfo_smoketest.sh first if wanted, skipping" \
    "  (DAR-285: proves the STOCK libresolv path -- dns_configuration_copy()" \
    "  and res_init() -- really sees the DNS server configd publishes after a" \
    "  real DHCP lease. Run by hand from the root shell post-boot, AFTER" \
    "  'ipconfig set en0 DHCP' has completed; before a lease exists it" \
    "  correctly reports no configuration. Built by" \
    "  tools/userland_staging/build_res_init_dnsinfo_smoketest.sh)"

install_smoketest tools/userland_staging/libc_build/dns_getaddrinfo_smoketest.macho "usr/local/bin/dns_getaddrinfo_smoketest" \
    "= dns_getaddrinfo_smoketest.macho not built -- run tools/userland_staging/build_dns_getaddrinfo_smoketest.sh first if wanted, skipping" \
    "  (DAR-182: real end-to-end DNS query over a real socket via real" \
    "  getaddrinfo()/gethostbyname2() from libinfo.dylib, resolving through" \
    "  the static /etc/resolv.conf this script writes (nameserver 10.0.2.3," \
    "  QEMU's usermode DNS proxy) since no real DHCP lease exists. Run by" \
    "  hand from the root shell post-boot. Built by" \
    "  tools/userland_staging/build_dns_getaddrinfo_smoketest.sh)"

install_smoketest tools/userland_staging/libedit_smoketest.macho "usr/local/bin/libedit_smoketest" \
    "= libedit_smoketest.macho not built -- run tools/userland_staging/build_libedit.sh first if wanted, skipping" \
    "  (DAR-286: replays scutil.c's own real libedit setup sequence and" \
    "  reports which branch scutil would take -- keep the editor, or tear it" \
    "  down and fall back to fgets(). Also does a real el_gets() read from a" \
    "  real file. Run by hand from the root shell post-boot. Built by" \
    "  tools/userland_staging/build_libedit.sh)"

install_smoketest tools/userland_staging/libc_build/tsd_vm_smoketest.macho "usr/local/bin/tsd_vm_smoketest" \
    "= tsd_vm_smoketest.macho not built -- run tools/userland_staging/build_tsd_vm_smoketest.sh first if wanted, skipping" \
    "  (DAR-258 fresh-thread TSD hygiene + mmap(MAP_ANON) zero-fill check," \
    "  run by hand from the root shell post-boot --" \
    "  tools/userland_staging/build_tsd_vm_smoketest.sh)"

install_smoketest tools/userland_staging/libc_build/tlv_smoketest.macho "usr/local/bin/tlv_smoketest" \
    "= tlv_smoketest.macho not built -- run tools/userland_staging/build_tlv_smoketest.sh first if wanted, skipping" \
    "  (DAR-279 real arm64 __thread / Thread-Local Variable check --" \
    "  initial-content copy, per-thread separation, persistence across" \
    "  another thread's use. Run by hand from the root shell post-boot;" \
    "  prints a single PASS/FAIL line. tools/userland_staging/" \
    "  build_tlv_smoketest.sh)"

install_smoketest tools/userland_staging/libc_build/pthread_cond_smoketest.macho "usr/local/bin/pthread_cond_smoketest" \
    "= pthread_cond_smoketest.macho not built -- run tools/userland_staging/build_pthread_cond_smoketest.sh first if wanted, skipping" \
    "  (DAR-280 real mutex + condition-variable handoff check -- the" \
    "  exact textbook pattern that used to SIGSEGV inside" \
    "  pthread_cond_wait on far 0x26. Run by hand from the root shell" \
    "  post-boot; prints a single PASS/FAIL line. tools/userland_staging/" \
    "  build_pthread_cond_smoketest.sh)"

install_smoketest tools/userland_staging/libc_build/ffi_smoketest.macho "usr/local/bin/ffi_smoketest" \
    "= ffi_smoketest.macho not built -- run tools/userland_staging/build_ffi_smoketest.sh first if wanted, skipping" \
    "  (DAR-388 libffi: ffi_call plus a trampoline-table closure. Run by hand;" \
    "  prints PASS/FAIL. tools/userland_staging/build_ffi_smoketest.sh)"

install_smoketest tools/userland_staging/libc_build/libm_accuracy_smoketest.macho "usr/local/bin/libm_accuracy_smoketest" \
    "= libm_accuracy_smoketest.macho not built -- run tools/userland_staging/build_libm_accuracy_smoketest.sh first if wanted, skipping" \
    "  (DAR-447 libsystem_m's FreeBSD msun sin/cos/tan/atan/atan2/log vs correctly" \
    "  rounded values, <= 1 ulp. Run by hand; prints PASS/FAIL." \
    "  tools/userland_staging/build_libm_accuracy_smoketest.sh)"

install_smoketest tools/userland_staging/libc_build/foundation_smoketest.macho "usr/local/bin/foundation_smoketest" \
    "= foundation_smoketest.macho not built -- run tools/userland_staging/build_foundation_smoketest.sh first if wanted, skipping" \
    "  (DAR-388 gnustep-base on objc4: strings, collections, exceptions," \
    "  NSInvocation and forwarding, threads. Run by hand; prints PASS/FAIL." \
    "  tools/userland_staging/build_foundation_smoketest.sh)"

install_smoketest tools/userland_staging/libc_build/weak_proxy_smoketest.macho "usr/local/bin/weak_proxy_smoketest" \
    "= weak_proxy_smoketest.macho not built -- run tools/userland_staging/build_weak_proxy_smoketest.sh first if wanted, skipping" \
    "  (DAR-450 __weak references to GNUstep NSProxy instances and NSProxy" \
    "  subclass class objects on objc4; a failure leaks a SideTable lock." \
    "  Run by hand; prints PASS/FAIL. tools/userland_staging/build_weak_proxy_smoketest.sh)"

install_smoketest tools/userland_staging/libc_build/pthread_lifecycle_smoketest.macho "usr/local/bin/pthread_lifecycle_smoketest" \
    "= pthread_lifecycle_smoketest.macho not built -- run tools/userland_staging/build_pthread_lifecycle_smoketest.sh first if wanted, skipping" \
    "  (DAR-416 real libpthread: pthread_join/exit/detach/cancel/kill and" \
    "  posix_spawn. Run by hand from the root shell post-boot; prints a" \
    "  single PASS/FAIL line. tools/userland_staging/" \
    "  build_pthread_lifecycle_smoketest.sh)"

install_smoketest tools/userland_staging/libc_build/fork_atfork_smoketest.macho "usr/local/bin/fork_atfork_smoketest" \
    "= fork_atfork_smoketest.macho not built -- run tools/userland_staging/build_fork_atfork_smoketest.sh first if wanted, skipping" \
    "  (DAR-130 fork() child-side _libc_fork_child() check. Run by hand" \
    "  from the root shell; prints a single PASS/FAIL line." \
    "  tools/userland_staging/build_fork_atfork_smoketest.sh)"

install_smoketest tools/userland_staging/libc_build/pthread_main_stack_smoketest.macho "usr/local/bin/pthread_main_stack_smoketest" \
    "= pthread_main_stack_smoketest.macho not built -- run tools/userland_staging/build_pthread_main_stack_smoketest.sh first if wanted, skipping" \
    "  (DAR-225 main-thread pthread_get_stackaddr_np() check. Run by hand" \
    "  from the root shell; prints a single PASS/FAIL line." \
    "  tools/userland_staging/build_pthread_main_stack_smoketest.sh)"

install_smoketest tools/userland_staging/libc_build/iokitlib_smoketest.macho "usr/local/bin/iokitlib_smoketest" \
    "= iokitlib_smoketest.macho not built -- run tools/userland_staging/build_iokitlib_smoketest.sh first if wanted, skipping" \
    "  (DAR-251 IOKitLib API check, prints PASS/FAIL)"
install_smoketest tools/userland_staging/libc_build/pdgop_smoketest.macho "usr/local/bin/pdgop_smoketest" \
    "= pdgop_smoketest.macho not built -- run tools/userland_staging/build_pdgop_smoketest.sh first if wanted, skipping" \
    "  (DAR-358 PDGOP framebuffer mapping + colour bars; needs hdmi_console=1)"
install_smoketest tools/userland_staging/libc_build/fb_cache_test.macho "usr/local/bin/fb_cache_test" \
    "= fb_cache_test.macho not built -- run tools/userland_staging/build_fb_cache_test.sh first if wanted, skipping" \
    "  (DAR-460 VRAM map cache modes + throughput; needs hdmi_console=1)"
install_smoketest tools/userland_staging/libc_build/hid_event_smoketest.macho "usr/local/bin/hid_event_smoketest" \
    "= hid_event_smoketest.macho not built -- run tools/userland_staging/build_hid_event_smoketest.sh first if wanted, skipping" \
    "  (DAR-391 keyboard/pointer events from IOHIDEventServiceUserClient)"
install_smoketest tools/userland_staging/libc_build/kqueue_poll_smoketest.macho "usr/local/bin/kqueue_poll_smoketest" \
    "= kqueue_poll_smoketest.macho not built -- run tools/userland_staging/build_kqueue_poll_smoketest.sh first if wanted, skipping" \
    "  (DAR-417 poll() readiness of a kqueue fd)"

for v in "" "_broken"; do
    install_smoketest "tools/userland_staging/libc_build/sigaction_tramp_smoketest${v}.macho" \
        "usr/local/bin/sigaction_tramp_smoketest${v}" \
        "= sigaction_tramp_smoketest${v}.macho not built -- run tools/userland_staging/build_sigaction_tramp_smoketest.sh first if wanted, skipping"
done
# Both variants on purpose (DAR-262): the "_broken" one is linked against
# sigaction_broken_shim_compat.c, a checked-in copy of the pre-fix
# _sigaction body, so the real failing case is reproducible on the target
# instead of only being argued from struct layouts. Nothing else on the
# image links that shim -- see sigaction_broken_shim_compat.c's header.
#
# WARNING, found the hard way 2026-09-07: run the "_broken" one LAST, or
# by hand. It does not fail cleanly -- the whole point of the bug is that
# the kernel enters userspace at a garbage sa_tramp, so the process can
# wedge instead of exiting, and it never returns to a shell prompt. Under
# tools/boot_test.py that silently eats every remaining --cmd in the run:
# a real 610s boot spent its whole window here and never got to the uname
# or notifyd checks queued behind it. The FIXED variant
# (sigaction_tramp_smoketest, no suffix) is the one to put in an
# automated sequence; it exits 0 and is safe to chain.

install_smoketest tools/userland_staging/libc_build/mach_port_construct_smoketest.macho "usr/local/bin/mach_port_construct_smoketest" \
    "= mach_port_construct_smoketest.macho not built -- run tools/userland_staging/build_mach_port_construct_smoketest.sh first if wanted, skipping" \
    "  (DAR-261 mach_port_construct/insert_right/MPO_QLIMIT probe plus the" \
    "  argv-driven abort() sub-modes abort-main/abort-assert/abort-thread/" \
    "  raw-kill, run by hand from the root shell post-boot --" \
    "  tools/userland_staging/build_mach_port_construct_smoketest.sh)"

install_smoketest tools/userland_staging/libc_build/dispatch_machport_smoketest.macho "usr/local/bin/dispatch_machport_smoketest" \
    "= dispatch_machport_smoketest.macho not built -- run tools/userland_staging/build_dispatch_machport_smoketest.sh first if wanted, skipping" \
    "  (DAR-266/DAR-268 EVFILT_MACHPORT dispatch-source probe, argv modes" \
    "  mt/proc/recv-create/recv-resume/recv-block/recv-deliver/notify/all," \
    "  run by hand from the root shell post-boot --" \
    "  tools/userland_staging/build_dispatch_machport_smoketest.sh)"

if [ -f tools/userland_staging/configd.macho ]; then
    mkdir -p "$MNT/usr/libexec" "$MNT/Library/LaunchDaemons"
    cp tools/userland_staging/configd.macho "$MNT/usr/libexec/configd"
    chmod 755 "$MNT/usr/libexec/configd"
    echo "+ wrote $(wc -c < tools/userland_staging/configd.macho) bytes to $MNT/usr/libexec/configd"
    echo "  (DAR-170 Tier 1 step 3 -- real configd.tproj SCDynamicStore SERVER"
    echo "  daemon, tools/userland_staging/build_configd.sh; real dynamic Mach-O,"
    echo "  spawned by real launchd.macho via a LaunchDaemons plist, real Mach"
    echo "  service com.apple.SystemConfiguration.configd. The six real"
    echo "  builtin-linked plugin subsystems (IPMonitor/InterfaceNamer/"
    echo "  KernelEventMonitor/LinkConfiguration/PreferencesMonitor/QoSMarking)"
    echo "  are honest 'not yet ported' stand-ins this pass -- see"
    echo "  configd_builtin_plugins_stub.c; real DHCP/IPConfiguration and real"
    echo "  InterfaceNamer are the next real step, not done yet)"
    cp "third_party/configd/configd.tproj/com.apple.configd.plist" \
        "$MNT/Library/LaunchDaemons/com.apple.configd.plist"
    chmod 644 "$MNT/Library/LaunchDaemons/com.apple.configd.plist"
    # DAR-257: same real, standard launchd StandardOutPath/StandardErrorPath
    # technique already applied to notifyd's plist above -- configd's own
    # dispatch manager thread is suspected of hitting the same
    # DISPATCH_CLIENT_CRASH("Unexpected error from kevent") crash class
    # notifyd hits, non-deterministically depending on which daemon's
    # manager thread gets there first in a given boot. Without this,
    # configd's crash is invisible the same way notifyd's was before DAR-167
    # added this for it.
    mkdir -p "$MNT/var/log"
    /usr/libexec/PlistBuddy -c "Add :StandardOutPath string /var/log/configd.log" \
        "$MNT/Library/LaunchDaemons/com.apple.configd.plist" 2>/dev/null
    /usr/libexec/PlistBuddy -c "Add :StandardErrorPath string /var/log/configd.log" \
        "$MNT/Library/LaunchDaemons/com.apple.configd.plist" 2>/dev/null
    # DAR-263/DAR-265: add configd's own real `-d` flag to ProgramArguments.
    # Without it, configd is completely undiagnosable on this target and
    # /var/log/configd.log stays EMPTY no matter what goes wrong -- every
    # SC_log() in configd routes through os_log(3), and this project's
    # os_log_compat.c is an honest no-op (_os_log_impl/_os_log_error_impl
    # all `(void)` their arguments and return). So the StandardErrorPath
    # above, added for DAR-257, was capturing a stream nothing ever wrote
    # to. Both DAR-261 and DAR-265 had to be diagnosed from ps/exit codes
    # alone because of this.
    #
    # `-d` is configd's own real, unmodified option (configd.m:314), and
    # under launchd it changes ONLY the log destination: its `else` branch
    # sets `_sc_log = kSCLogDestinationFile`, which __SC_log_enabled()/
    # __SC_log_send() (SystemConfiguration.fproj/SCD.c:544/583) then honour
    # by printing every level < LOG_INFO to stdout/stderr. It does NOT
    # change the daemonize path -- that is `if (!forceForeground &&
    # !is_launchd_job)` (configd.m:384), and is_launchd_job is already TRUE
    # for a launchd-started configd (DAR-242), so the fork is skipped either
    # way -- and init_fds() is likewise already skipped for a launchd job.
    # Verified by reading configd.m directly, not assumed.
    /usr/libexec/PlistBuddy -c "Add :ProgramArguments:1 string -d" \
        "$MNT/Library/LaunchDaemons/com.apple.configd.plist" 2>/dev/null
    echo "+ wrote com.apple.configd.plist to $MNT/Library/LaunchDaemons/"
    echo "  (real, unmodified Apple plist plus this script's own added"
    echo "  StandardOutPath/StandardErrorPath -> /var/log/configd.log (DAR-257) --"
    echo "  MachServices/com.apple.SystemConfiguration.configd checkin,"
    echo "  ProgramArguments /usr/libexec/configd, matches this script's real"
    echo "  install path above)"

    # DAR-170 (2026-09-04 session 4): real, previously-missing prerequisite
    # for EVERY configd plugin (not just IPConfiguration) to ever actually
    # run. Root cause found this session by reading plugin_support.c's real,
    # unmodified plugin_exec() directly: even "builtin" (statically-linked)
    # plugins are only invoked (load()/start()/prime()) if a real
    # CFBundleRef for their exact CFBundleIdentifier is first discovered by
    # a real directory scan of $(sysdir LIBRARY,SYSTEM)/SystemConfiguration
    # (= "/System/Library/SystemConfiguration" -- see sysdir_compat.c's own
    # updated header comment). With zero .bundle directories ever staged
    # here, `allBundles` was always empty and DAR-240/241's already-linked
    # InterfaceNamer/LinkConfiguration/KernelEventMonitor/
    # PreferencesMonitor/IPMonitor/QoSMarking code was silently NEVER
    # called at runtime, despite linking clean -- confirmed via a real
    # QEMU direct-invocation probe this session showing configd exit
    # immediately instead of blocking in CFRunLoopRun().
    #
    # Fix: stage a real, flat (no Contents/ -- confirmed by reading
    # swift-corelibs-foundation's own CFBundle_Internal.h search-path list,
    # which tries a bare top-level Info.plist for exactly this iOS/embedded
    # bundle shape) .bundle directory per plugin, containing that plugin's
    # own REAL, unmodified Info.plist from third_party/configd/Plugins/ --
    # only textual substitution applied is Xcode's own
    # "$(PRODUCT_BUNDLE_IDENTIFIER)" build-variable placeholder (never
    # expanded outside a real Xcode build), replaced with the same real
    # bundle identifier value plugin_support.c's own pluginWhitelist[]/
    # builtin_plugins[] tables already hard-code for that exact plugin --
    # not fabricated, cross-checked against both real tables.
    stage_sc_bundle() {
        local bundle_name="$1" bundle_id="$2" src_plist="$3"
        local dir="$MNT/System/Library/SystemConfiguration/${bundle_name}.bundle"
        if [ ! -f "$src_plist" ]; then
            echo "  ! $src_plist not found, skipping ${bundle_name}.bundle"
            return
        fi
        mkdir -p "$dir"
        sed 's#$(PRODUCT_BUNDLE_IDENTIFIER)#'"$bundle_id"'#' "$src_plist" \
            > "$dir/Info.plist"
        chmod 644 "$dir/Info.plist"
        echo "+ staged $dir/Info.plist (real Apple Info.plist, id=$bundle_id)"
    }
    stage_sc_bundle "InterfaceNamer" "com.apple.SystemConfiguration.InterfaceNamer" \
        "third_party/configd/Plugins/InterfaceNamer/Info.plist"
    stage_sc_bundle "LinkConfiguration" "com.apple.SystemConfiguration.LinkConfiguration" \
        "third_party/configd/Plugins/LinkConfiguration/Info.plist"
    stage_sc_bundle "KernelEventMonitor" "com.apple.SystemConfiguration.KernelEventMonitor" \
        "third_party/configd/Plugins/KernelEventMonitor/Info.plist"
    stage_sc_bundle "PreferencesMonitor" "com.apple.SystemConfiguration.PreferencesMonitor" \
        "third_party/configd/Plugins/PreferencesMonitor/Info.plist"
    stage_sc_bundle "IPMonitor" "com.apple.SystemConfiguration.IPMonitor" \
        "third_party/configd/Plugins/IPMonitor/Info.plist"
    stage_sc_bundle "QoSMarking" "com.apple.SystemConfiguration.QoSMarking" \
        "third_party/configd/Plugins/QoSMarking/Info.plist"
    # IPConfiguration: real Apple loads this one via CFBundle dlopen (a
    # separate .bproj target from configd's own Plugins/), NOT the
    # builtin_plugins[] table -- confirmed by reading plugin_support.c and
    # bootp.xcodeproj directly (DAR-170 2026-09-04 session 3 finding). This
    # project has no proven CFBundleLoadExecutableAndReturnError-on-a-
    # dlopened-bundle path, so IPConfiguration is deliberately treated as a
    # statically-linked builtin here too (real, honestly-documented
    # architecture deviation -- see third_party-patches/configd.patch and
    # ipconfiguration_builtin_glue.c). Its real Embedded-Info.plist has no
    # "Builtin" key at all (real Apple never marks it builtin, since real
    # Apple never treats it as one) -- this stage_sc_bundle call adds one
    # via the same sed pass, which is a deliberate, documented content
    # addition beyond verbatim real Apple source, not an oversight.
    if [ -f tools/userland_staging/libc_build/libconfigd_ipconfiguration.a ]; then
        # The awk pass anchors the Builtin insertion to the FIRST <dict> in
        # the file -- the bundle's own root dict -- and nothing else. (awk,
        # not `sed '0,/<dict>/'`: address 0 is a GNU extension that BSD/macOS
        # sed silently ignores, producing zero substitutions -- verified.)
        # Corrected 2026-09-09 (DAR-170): the previous unanchored
        # `sed 's#<dict>#...#'` substitutes the first match on EVERY line, so
        # it injected <key>Builtin</key> into all three <dict>s in this
        # plist (3 hits, counted). Two of those are wrong: the plugin's own
        # "IPConfiguration" settings dict (harmless but bogus) and its
        # "MachServices" dict -- which, once the real update-mach-services
        # merge below started running, published a nonexistent Mach service
        # literally named "Builtin" into com.apple.configd.plist. Found by
        # reading the merged plist back off the image, not from a failure.
        sed 's#$(PRODUCT_BUNDLE_IDENTIFIER)#com.apple.SystemConfiguration.IPConfiguration#' \
            "third_party/bootp/IPConfiguration.bproj/Embedded-Info.plist" \
            | awk '!done && /<dict>/ { sub(/<dict>/, "<dict>\n\t<key>Builtin</key>\n\t<true/>"); done=1 } { print }' \
            > "$MNT/System/Library/SystemConfiguration/IPConfiguration.bundle.tmp" 2>/dev/null
        mkdir -p "$MNT/System/Library/SystemConfiguration/IPConfiguration.bundle"
        mv "$MNT/System/Library/SystemConfiguration/IPConfiguration.bundle.tmp" \
            "$MNT/System/Library/SystemConfiguration/IPConfiguration.bundle/Info.plist"
        chmod 644 "$MNT/System/Library/SystemConfiguration/IPConfiguration.bundle/Info.plist"
        echo "+ staged $MNT/System/Library/SystemConfiguration/IPConfiguration.bundle/Info.plist"
        echo "  (real Embedded-Info.plist + added Builtin=true, DAR-170)"
    else
        echo "= tools/userland_staging/libc_build/libconfigd_ipconfiguration.a not built -- run"
        echo "  tools/userland_staging/build_ipconfiguration_plugin.sh first if wanted, skipping"
        echo "  IPConfiguration.bundle staging (other 6 plugin bundles above still staged)"
    fi

    # DAR-170: real Apple's own "Update MachServices" build phase, replayed
    # here. This is NOT an invention -- it is a faithful replay of
    # third_party/configd/configd.tproj/update-mach-services, the real,
    # unmodified Apple shell script configd.xcodeproj runs as a build phase
    # ("Update MachServices", 3 targets reference it). Read that script; the
    # loop below is the same two PlistBuddy calls it makes, against the same
    # two paths, with the Xcode-only DSTROOT/SDKROOT/RC_EMBEDDEDPROJECT_DIR
    # search roots collapsed to the one root that exists here.
    #
    # Why it matters, found by a real QEMU boot (2026-09-09): a configd
    # plugin that needs its own Mach service declares it in its OWN bundle
    # Info.plist, and this build phase is what merges those into the
    # installed /System/Library/LaunchDaemons/com.apple.configd.plist. The
    # checked-in source plist (configd.tproj/com.apple.configd.plist) lists
    # only com.apple.SystemConfiguration.configd because the shipped one is
    # GENERATED. IPConfiguration.bproj/Embedded-Info.plist really does carry
    #
    #     <key>MachServices</key>
    #     <dict><key>com.apple.network.IPConfiguration</key><true/></dict>
    #
    # and ipconfigd.c's real server_init() (IPConfiguration.bproj/server.c:535)
    # does bootstrap_check_in(bootstrap_port, IPCONFIG_SERVER) for exactly
    # that name. Without the merge, launchd has never heard of the service,
    # bootstrap_check_in fails, no MIG port is ever created, and the real
    # ipconfig(8) client reports "ipconfig server not active" -- which is
    # precisely what a real boot showed before this ran. Nothing was wrong
    # with the plugin, the daemon, or the client; the generated half of the
    # plist was simply never generated.
    CONFIGD_LAUNCHD_PLIST="$MNT/Library/LaunchDaemons/com.apple.configd.plist"
    if [ -f "$CONFIGD_LAUNCHD_PLIST" ]; then
        merged_any=0
        for plugin_dir in "$MNT"/System/Library/SystemConfiguration/*.bundle; do
            [ -d "$plugin_dir" ] || continue
            # Real script's own search order: Contents/Info.plist first (the
            # macOS bundle shape), then a flat top-level Info.plist (the
            # iOS/embedded shape this project stages).
            plugin_plist=""
            if [ -f "$plugin_dir/Contents/Info.plist" ]; then
                plugin_plist="$plugin_dir/Contents/Info.plist"
            elif [ -f "$plugin_dir/Info.plist" ]; then
                plugin_plist="$plugin_dir/Info.plist"
            fi
            [ -n "$plugin_plist" ] || continue
            tmp_ms="$(mktemp -t plugin_machservices)"
            if /usr/libexec/PlistBuddy -c "Print :MachServices" -x "$plugin_plist" \
                    > "$tmp_ms" 2>/dev/null; then
                echo "+ merging \"MachServices\" from: $plugin_plist"
                merge_err="$(mktemp -t plugin_machservices_err)"
                if /usr/libexec/PlistBuddy -c "Merge $tmp_ms :MachServices" \
                        "$CONFIGD_LAUNCHD_PLIST" 2>"$merge_err"; then
                    merged_any=1
                else
                    # DAR-291: don't let set -e's silent, contextless abort be
                    # the only signal here -- name the plugin and show
                    # PlistBuddy's own reason (almost always a MachServices
                    # key collision with a plugin already merged above).
                    echo "error: PlistBuddy Merge of MachServices from $plugin_plist failed" >&2
                    echo "  into $CONFIGD_LAUNCHD_PLIST -- likely a MachServices key collision" >&2
                    echo "  with a plugin already merged. PlistBuddy said:" >&2
                    sed 's/^/    /' "$merge_err" >&2
                    rm -f "$tmp_ms" "$merge_err"
                    exit 1
                fi
                rm -f "$merge_err"
            fi
            rm -f "$tmp_ms"
        done
        if [ "$merged_any" -eq 1 ]; then
            # Real script converts at the end too (xml1 for macOS deployment
            # targets, binary1 otherwise). xml1 here so the staged plist stays
            # readable/diffable from the guest, and because this project's own
            # launchd reads it either way.
            /usr/bin/plutil -convert xml1 "$CONFIGD_LAUNCHD_PLIST" 2>/dev/null || true
            echo "  (real Apple configd.tproj/update-mach-services build phase, replayed --"
            echo "  without it bootstrap_check_in(com.apple.network.IPConfiguration) in"
            echo "  IPConfiguration.bproj/server.c:535 fails and ipconfig(8) reports"
            echo "  \"ipconfig server not active\")"
            echo "+ com.apple.configd.plist MachServices now:"
            /usr/libexec/PlistBuddy -c "Print :MachServices" "$CONFIGD_LAUNCHD_PLIST" 2>/dev/null \
                | sed 's/^/    /'
        fi
    fi
else
    echo "= tools/userland_staging/configd.macho not built -- run tools/userland_staging/build_configd.sh first if wanted, skipping"
fi

# DAR-170: real Apple ipconfig(8) -- the client half of IPConfiguration
# (third_party/bootp/ipconfig.tproj/client.c, real unmodified source,
# tools/userland_staging/build_ipconfig.sh). Real Apple installs it at
# /usr/sbin/ipconfig; same path here. This is the only way to talk to the
# real IPConfiguration plugin running inside configd -- `ipconfig set en0
# DHCP` drives the real DHCP state machine directly through the real
# ipconfig MIG subsystem (bootplib/ipconfig.defs -> server.c's
# _ipconfig_set() -> ipconfigd.c's set_if()), with no SCPreferences /
# SCDynamicStore configuration needed, and `ipconfig getpacket en0` prints
# the real DHCP ACK back.
if [ -f tools/userland_staging/ipconfig.macho ]; then
    mkdir -p "$MNT/usr/sbin"
    cp tools/userland_staging/ipconfig.macho "$MNT/usr/sbin/ipconfig"
    chmod 755 "$MNT/usr/sbin/ipconfig"
    echo "+ wrote $(wc -c < tools/userland_staging/ipconfig.macho) bytes to $MNT/usr/sbin/ipconfig"
    echo "  (DAR-170 -- real Apple ipconfig(8), tools/userland_staging/build_ipconfig.sh;"
    echo "  real dynamic Mach-O talking the real ipconfig MIG subsystem to the"
    echo "  IPConfiguration plugin inside configd)"
else
    echo "= tools/userland_staging/ipconfig.macho not built -- run tools/userland_staging/build_ipconfig.sh first if wanted, skipping"
fi

# DAR-283: real Apple scutil(8) -- the SCDynamicStore/SCPreferences
# read-back tool (third_party/configd/scutil.tproj, real unmodified
# source, tools/userland_staging/build_scutil.sh). Real Apple installs it
# at /usr/sbin/scutil; same path here.
#
# This is the counterpart to ipconfig(8) above: ipconfig proves a real
# DHCP lease arrived, scutil proves what configd's IPMonitor plugin then
# PUBLISHED from it. /etc/resolv.conf is legitimately absent on this
# target -- ip_plugin.c's flat-file write is `#if !TARGET_OS_IPHONE` and
# this port is TARGET_OS_IPHONE=1 -- so the dynamic store is the only
# real place the DNS configuration lives:
#
#   scutil --dns
#   scutil <<< "show State:/Network/Global/DNS"
#   scutil <<< "list State:/Network/.*"
#
# All three are non-interactive. scutil's interactive mode now has REAL
# line editing and history too, as of DAR-286 (2026-09-11): real libedit
# is built by tools/userland_staging/build_libedit.sh and installed above
# at /usr/lib/libedit.3.dylib. The one thing still missing is a
# terminfo/termcap database on this image, without which libedit falls
# back to dumb-terminal settings and arrow keys do not move the cursor --
# DAR-293, observable by running /usr/local/bin/libedit_smoketest.
if [ -f tools/userland_staging/scutil.macho ]; then
    mkdir -p "$MNT/usr/sbin"
    cp tools/userland_staging/scutil.macho "$MNT/usr/sbin/scutil"
    chmod 755 "$MNT/usr/sbin/scutil"
    echo "+ wrote $(wc -c < tools/userland_staging/scutil.macho) bytes to $MNT/usr/sbin/scutil"
    echo "  (DAR-283 -- real Apple scutil(8), tools/userland_staging/build_scutil.sh;"
    echo "  real dynamic Mach-O talking the real config MIG subsystem to configd)"
else
    echo "= tools/userland_staging/scutil.macho not built -- run tools/userland_staging/build_scutil.sh first if wanted, skipping"
fi

if [ -f tools/userland_staging/libc_build/libobjc.A.dylib ]; then
    mkdir -p "$MNT/usr/lib" "$MNT/bin"
    cp tools/userland_staging/libc_build/libobjc.A.dylib "$MNT/usr/lib/libobjc.A.dylib"
    chmod 755 "$MNT/usr/lib/libobjc.A.dylib"
    echo "+ wrote $(wc -c < tools/userland_staging/libc_build/libobjc.A.dylib) bytes to $MNT/usr/lib/libobjc.A.dylib"
    echo "  (DAR-236 -- full real objc4 runtime, real Apple product name/path,"
    echo "  tools/userland_staging/build_objc4.sh; not yet wired into"
    echo "  configd.macho/IPMonitor.m/QoSMarking.m, that's DAR-170's job)"
    if [ -f tools/userland_staging/libc_build/libobjc-trampolines.dylib ]; then
        cp tools/userland_staging/libc_build/libobjc-trampolines.dylib "$MNT/usr/lib/libobjc-trampolines.dylib"
        chmod 755 "$MNT/usr/lib/libobjc-trampolines.dylib"
        echo "+ wrote $MNT/usr/lib/libobjc-trampolines.dylib (DAR-236 -- imp_implementationWithBlock's"
        echo "  trampoline pages, dlopen()ed by libobjc; tools/userland_staging/build_objc4.sh)"
    fi
    install_smoketest tools/userland_staging/objc4_smoketest.macho "bin/objc4_smoketest" \
        "= tools/userland_staging/objc4_smoketest.macho not built -- run tools/userland_staging/build_objc4.sh first if wanted, skipping" \
        "  (real minimal Objective-C smoketest -- one @interface/@implementation," \
        "  real objc_msgSend dispatch, see tools/userland_staging/objc4_smoketest.m)"
    install_smoketest tools/userland_staging/gnustep_objc4_smoketest.macho "bin/gnustep_objc4_smoketest" \
        "= tools/userland_staging/gnustep_objc4_smoketest.macho not built -- run tools/userland_staging/build_gnustep_objc4_smoketest.sh first if wanted, skipping" \
        "  (DAR-382 -- the runtime features GNUstep's Foundation needs from objc4," \
        "  see tools/userland_staging/gnustep_objc4_smoketest.m)"
else
    echo "= tools/userland_staging/libc_build/libobjc.A.dylib not built -- run tools/userland_staging/build_objc4.sh first if wanted, skipping"
fi

install_smoketest tools/userland_staging/libcxxabi_smoketest.macho "bin/libcxxabi_smoketest" \
    "= tools/userland_staging/libcxxabi_smoketest.macho not built -- run tools/userland_staging/build_llvm_libcxxabi.sh first if wanted, skipping" \
    "  (real LLVM libc++abi smoketest -- dynamic_cast/RTTI/operator" \
    "  new+delete OK; real throw/catch reported non-fatally, see" \
    "  tools/userland_staging/llvm_libcxxabi_smoketest.cpp)"

install_smoketest tools/userland_staging/libcxx_smoketest.macho "bin/libcxx_smoketest" \
    "= tools/userland_staging/libcxx_smoketest.macho not built -- run tools/userland_staging/build_llvm_libcxx.sh first if wanted, skipping" \
    "  (real LLVM libc++ smoketest -- std::vector/sort/string/" \
    "  shared_ptr, see tools/userland_staging/llvm_libcxx_smoketest.cpp)"

install_smoketest tools/userland_staging/libc_build/kqueue_smoketest.macho "bin/kqueue_smoketest" \
    "= tools/userland_staging/libc_build/kqueue_smoketest.macho not built -- run tools/userland_staging/build_kqueue_smoketest.sh first if wanted, skipping" \
    "  (real kqueue()/kevent() EVFILT_TIMER round-trip -- Phase 6.6" \
    "  libdispatch prereq, see tools/userland_staging/kqueue_smoketest.c)"

install_smoketest tools/userland_staging/libc_build/sysv_ipc_smoketest.macho "bin/sysv_ipc_smoketest" \
    "= tools/userland_staging/libc_build/sysv_ipc_smoketest.macho not built -- run tools/userland_staging/build_sysv_ipc_smoketest.sh first if wanted, skipping" \
    "  (SysV shm/sem/msg round-trip incl. cairo's MIT-SHM probe order --" \
    "  DAR-407, see tools/userland_staging/sysv_ipc_smoketest.c)"

# DAR-200: the real EVFILT_READ / EVFILT_PROC / select()-on-a-kqueue-fd
# test (tools/userland_staging/kqueue_socket_smoketest.c, built by
# build_kqueue_socket_smoketest.sh). Always installed as /bin/ if built;
# its LaunchDaemon plist only when KQUEUE_SOCKET_SMOKETEST=1, since
# there is currently no login prompt to run it from by hand (that being
# DAR-200's own second symptom) but it is still a diagnostic, not part
# of the real boot.
install_smoketest tools/userland_staging/libc_build/kqueue_socket_smoketest.macho "bin/kqueue_socket_smoketest" \
    "= tools/userland_staging/libc_build/kqueue_socket_smoketest.macho not built -- run tools/userland_staging/build_kqueue_socket_smoketest.sh first if wanted, skipping" \
    "  (real select()-on-a-kqueue-fd + EVFILT_READ/AF_UNIX + EVFILT_PROC" \
    "  test, DAR-200 -- see tools/userland_staging/kqueue_socket_smoketest.c)"
if [ "$SMOKETEST_INSTALLED" = 1 ]; then
    if [ "${KQUEUE_SOCKET_SMOKETEST:-0}" = "1" ]; then
        mkdir -p "$MNT/Library/LaunchDaemons"
        cp tools/userland_staging/org.puredarwin.kqueue-socket-smoketest.plist \
            "$MNT/Library/LaunchDaemons/org.puredarwin.kqueue-socket-smoketest.plist"
        chmod 644 "$MNT/Library/LaunchDaemons/org.puredarwin.kqueue-socket-smoketest.plist"
    else
        rm -f "$MNT/Library/LaunchDaemons/org.puredarwin.kqueue-socket-smoketest.plist"
        echo "= org.puredarwin.kqueue-socket-smoketest.plist NOT installed (set KQUEUE_SOCKET_SMOKETEST=1 to run it at boot)"
    fi
fi

# KQUEUE_SOCKET_SMOKETEST_AS_PID1=1: install the smoketest as the process
# the kernel itself execs as PID 1, completely bypassing launchd.
# bsd/kern/kern_exec.c's load_init_program() tries
# /usr/appleinternal/sbin/launchd.development BEFORE /sbin/launchd
# (visible on every boot log as two "load_init_program: attempting to
# load ..." lines), so dropping a binary there replaces PID 1 without
# touching /sbin/launchd at all -- fully reversible, and the real launchd
# comes back the moment this is re-run without the variable. That is what
# made the test decisive for DAR-200: it answers "does the KERNEL deliver
# these events" with zero launchd machinery in the picture.
#
# Deliberately OUTSIDE the `if [ -f ...kqueue_socket_smoketest.macho ]`
# block above: the removal half must run even when that binary has since
# been deleted from libc_build/, otherwise a stale hijack file would sit
# on the image forever, silently making every later boot run a smoketest
# instead of real launchd -- exactly the "stale artifact hides the real
# state" failure this repo's own postmortems keep hitting.
if [ "${KQUEUE_SOCKET_SMOKETEST_AS_PID1:-0}" = "1" ]; then
    if [ -n "$SKIP_SMOKETESTS" ]; then
        echo "error: KQUEUE_SOCKET_SMOKETEST_AS_PID1=1 and SKIP_SMOKETESTS are contradictory (a real-hardware image with a smoketest as PID 1)" >&2
        exit 1
    fi
    if [ ! -f tools/userland_staging/libc_build/kqueue_socket_smoketest.macho ]; then
        echo "error: KQUEUE_SOCKET_SMOKETEST_AS_PID1=1 but kqueue_socket_smoketest.macho is not built -- run tools/userland_staging/build_kqueue_socket_smoketest.sh first" >&2
        exit 1
    fi
    mkdir -p "$MNT/usr/appleinternal/sbin"
    # Explicit opt-in dev flag, rejected with SKIP_SMOKETESTS above.
    cp tools/userland_staging/libc_build/kqueue_socket_smoketest.macho "$MNT/usr/appleinternal/sbin/launchd.development" # install_smoketest-exempt
    chmod 755 "$MNT/usr/appleinternal/sbin/launchd.development"
    echo "+ installed kqueue_socket_smoketest as PID 1 at /usr/appleinternal/sbin/launchd.development (KQUEUE_SOCKET_SMOKETEST_AS_PID1=1)"
    echo "  !! real launchd is NOT PID 1 in this image until this script is re-run without that variable"
elif [ -f "$MNT/usr/appleinternal/sbin/launchd.development" ]; then
    rm -f "$MNT/usr/appleinternal/sbin/launchd.development"
    echo "= removed stale /usr/appleinternal/sbin/launchd.development -- real /sbin/launchd is PID 1 again"
fi

install_smoketest tools/userland_staging/asl_smoketest.macho "bin/asl_smoketest" \
    "= tools/userland_staging/asl_smoketest.macho not built -- run tools/userland_staging/build_libsystem_asl_dylib.sh first if wanted, skipping" \
    "  (real syslog-385 ASL client -> syslogd dbserver.c -- DAR-121, see" \
    "  tools/userland_staging/asl_smoketest.c; needs /usr/lib/system/libsystem_asl.dylib)"

install_smoketest tools/userland_staging/libinfo_smoketest.macho "bin/libinfo_smoketest" \
    "= tools/userland_staging/libinfo_smoketest.macho not built -- run tools/userland_staging/build_libinfo.sh first if wanted, skipping" \
    "  (real Libinfo si_module files-only lookups -- DAR-181, see" \
    "  tools/userland_staging/libinfo_smoketest.c; needs /usr/lib/libinfo.dylib)"

install_smoketest tools/userland_staging/libc_build/cf_dylib_smoketest.macho "bin/cf_dylib_smoketest" \
    "= tools/userland_staging/libc_build/cf_dylib_smoketest.macho not built -- run tools/userland_staging/build_corefoundation_dylib_test.sh first if wanted, skipping" \
    "  (real libCoreFoundation.dylib constructor/init-order proof -- DAR-195," \
    "  see tools/userland_staging/cf_dylib_smoketest.c; needs /usr/lib/libCoreFoundation.dylib)"

# Real Phase 4 step 3 "remaining commands" tiers (docs/roadmap.md) --
# each build_*_cmds.sh script writes one <name>.macho per real command
# it built; every one of them lands in /bin, matching real BSD/macOS
# layout (all of shell_cmds' Tier 1/2 set below is genuinely /bin, not
# /usr/bin, on a real system). Generic loop instead of one if-block per
# command -- this list only grows as more tiers land.
mkdir -p "$MNT/bin"
for cmd in true false yes pwd basename dirname echo printf hostname logname cat wc head tail mkdir touch rm which env sleep printenv cut uniq ln id cp mv rev fold paste expand unexpand comm colrm lam rs rmdir mkfifo mknod chflags seq jot test kill tee mktemp hexdump stat cksum pathchk du chmod chown nice renice nohup chroot apply vis unvis grep sed nl xargs ed ls find date gzip compress diff tar less df killall getty md5 sync; do
    if [ -f "tools/userland_staging/${cmd}.macho" ]; then
        cp "tools/userland_staging/${cmd}.macho" "$MNT/bin/${cmd}"
        chmod 755 "$MNT/bin/${cmd}"
        echo "+ wrote $(wc -c < "tools/userland_staging/${cmd}.macho") bytes to $MNT/bin/${cmd}"
    else
        echo "= tools/userland_staging/${cmd}.macho not built -- run build_simple_cmds.sh first if wanted, skipping"
    fi
done

# /bin/sort: a stale leftover in older base images (qemu/hfs_root_test.img).
# No script here builds sort (it is not in the loop above; the real source,
# third_party/text_cmds/sort, is not ported yet), and the leftover copy
# aborts at dyld load (`Symbol not found: _pthread_cond_wait$UNIX2003`).
# Remove it unless a real build of it is being installed.
if [ ! -f tools/userland_staging/sort.macho ] && [ -e "$MNT/bin/sort" ]; then
    rm -f "$MNT/bin/sort"
    echo "= removed stale /bin/sort (not built by any script here; aborted at dyld load)"
fi

# base64(1) and awk(1): built by build_simple_cmds.sh like the loop above,
# but a Mac ships them in /usr/bin, not /bin.
mkdir -p "$MNT/usr/bin"
for cmd in base64 awk pkill top; do
    if [ -f "tools/userland_staging/$cmd.macho" ]; then
        cp "tools/userland_staging/$cmd.macho" "$MNT/usr/bin/$cmd"
        chmod 755 "$MNT/usr/bin/$cmd"
        echo "+ wrote $(wc -c < "tools/userland_staging/$cmd.macho") bytes to $MNT/usr/bin/$cmd"
    else
        echo "= tools/userland_staging/$cmd.macho not built -- run build_simple_cmds.sh first if wanted, skipping"
    fi
done
# pgrep(1) is the same binary as pkill(1) (pkill.c picks its mode from
# getprogname(), DAR-343); HFS+ symlinks aren't injected yet, so copy.
if [ -f tools/userland_staging/pkill.macho ]; then
    cp tools/userland_staging/pkill.macho "$MNT/usr/bin/pgrep"
    chmod 755 "$MNT/usr/bin/pgrep"
fi

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

# DAR-336 / DAR-337 reproducer (found by the OpenSSH port): alarm(3)
# killing its caller, and a fork() child that forks again crashing. Kept
# on the image as a permanent regression test -- both bugs are invisible
# to a link-time or nm-level check and only show up at runtime.
install_smoketest tools/userland_staging/libc_build/fork_alarm_smoketest.macho "usr/local/bin/fork_alarm_smoketest" \
    "= fork_alarm_smoketest.macho not built -- run build_fork_alarm_smoketest.sh first if wanted, skipping"

# --- OpenSSH (DAR-183, Phase 7b) --------------------------------------
# Real OpenSSH 9.3p1 out of the FreeBSD pin (third_party/freebsd-src/
# crypto/openssh @ release/13.2.0), built by
# tools/userland_staging/build_sshd.sh. DAR-183's own original
# instruction was to leave this started by hand rather than by a
# plist; DAR-350 (real, observed on real hardware: "sshd doesnt seem
# to auto start on rpi3") supersedes that -- com.openssh.sshd.plist
# (this same directory) now gets real launchd (launchd.macho, this
# project's own real PID 1) to start it, the same real
# RunAtLoad+KeepAlive pattern already proven for syslogd/notifyd/
# configd/chronyd (see that plist's own header for the full citation).
if [ -f tools/userland_staging/sshd.macho ]; then
    mkdir -p "$MNT/usr/sbin" "$MNT/Library/LaunchDaemons" "$MNT/var/log"
    cp tools/userland_staging/sshd.macho "$MNT/usr/sbin/sshd"
    chmod 755 "$MNT/usr/sbin/sshd"
    echo "+ wrote $(wc -c < tools/userland_staging/sshd.macho) bytes to $MNT/usr/sbin/sshd"
    cp tools/userland_staging/com.openssh.sshd.plist \
        "$MNT/Library/LaunchDaemons/com.openssh.sshd.plist"
    chmod 644 "$MNT/Library/LaunchDaemons/com.openssh.sshd.plist"
    echo "+ wrote com.openssh.sshd.plist to $MNT/Library/LaunchDaemons/ (DAR-350)"
else
    echo "= tools/userland_staging/sshd.macho not built -- run build_sshd.sh first if wanted, skipping"
fi
for pair in "ssh:bin/ssh" "ssh-keygen:bin/ssh-keygen"; do
    src="tools/userland_staging/${pair%%:*}.macho"
    dst="usr/${pair##*:}"
    if [ -f "$src" ]; then
        mkdir -p "$MNT/$(dirname "$dst")"
        cp "$src" "$MNT/$dst"
        chmod 755 "$MNT/$dst"
        echo "+ wrote $(wc -c < "$src") bytes to $MNT/$dst"
    else
        echo "= $src not built -- run build_sshd.sh first if wanted, skipping"
    fi
done

# --- su (DAR-339) -----------------------------------------------------
# Real Apple shell_cmds/su/su.c, built by tools/userland_staging/
# build_su.sh against this project's now-real OpenPAM static-module
# support (build_openpam.sh) + su_pam_unix_static.c. su.c's own real,
# unmodified main() hard-requires `geteuid() == 0` ("not running
# setuid" fatal error otherwise, checked directly at su.c's own line
# ~243) -- this project's first shipped setuid-root binary, so mode and
# ownership both matter, not just presence. chown to root:wheel is
# attempted and then VERIFIED (stat'd back), not assumed to have
# worked: chown(2) to an arbitrary uid is refused for a non-root caller
# on most real Unix permission models, including a real HFS+ volume
# mounted by this script's own non-root hdiutil attach (no `-owners
# on` passed) -- if it silently no-ops here, su would need a real
# sudo-elevated provisioning step instead, which this script does not
# attempt to add on its own.
# DAR-340/H1: a setuid bit is only ever installed when the file really ends
# up root-owned (0:0). This script runs as the host user (uid 502) against an
# hdiutil attach without `-owners on`; chown(2) silently no-ops there and
# `sudo` on the host needs a password, so 0:0 is normally NOT reachable here.
# Shipping "4755, owned by uid 502" is worse than useless (a setuid-as-502
# binary that su/sudo then refuse to trust), so the fallback is mode 0755
# (NOT setuid) plus a documented first-boot provisioning step from a root
# shell: /usr/sbin/provision-setuid (tools/userland_staging/provision-setuid.sh)
# does chown root:wheel + chmod 4755 (su, sudo) / 0440 (sudoers) and prints
# `ls -ln`. Set REQUIRE_SETUID_ROOT=1 to make "did not become 0:0" a hard
# build failure instead.
install_setuid_root() { # <src> <dest> <label>
    local src="$1" dest="$2" label="$3" owner
    cp "$src" "$dest"
    chown root:wheel "$dest" 2>/dev/null || true
    owner="$(stat -f '%u:%g' "$dest")"
    if [ "$owner" = "0:0" ]; then
        chmod 4755 "$dest"
        echo "+ wrote $(wc -c < "$src") bytes to $dest (mode 4755, root:wheel confirmed)"
    else
        chmod 0755 "$dest"
        if [ "${REQUIRE_SETUID_ROOT:-0}" = "1" ]; then
            echo "error: $label: chown root:wheel did not take (owner uid:gid $owner) and REQUIRE_SETUID_ROOT=1" >&2
            exit 1
        fi
        echo "! $label installed NON-setuid (mode 0755, owner $owner): chown root:wheel did not take on this host mount. It will refuse to elevate until provisioned: run /usr/sbin/provision-setuid from a ROOT shell on the booted target (DAR-340 H1)"
    fi
}

if [ -f tools/userland_staging/su.macho ]; then
    install_setuid_root tools/userland_staging/su.macho "$MNT/usr/bin/su" su
else
    echo "= tools/userland_staging/su.macho not built -- run build_su.sh first if wanted, skipping"
fi

# First-boot provisioning helper for the setuid/root-owned files above.
if [ -f tools/userland_staging/provision-setuid.sh ]; then
    mkdir -p "$MNT/usr/sbin"
    cp tools/userland_staging/provision-setuid.sh "$MNT/usr/sbin/provision-setuid"
    chmod 755 "$MNT/usr/sbin/provision-setuid"
    echo "+ wrote $MNT/usr/sbin/provision-setuid"
fi

if [ -f tools/userland_staging/dyld-inject-regression.sh ]; then
    cp tools/userland_staging/dyld-inject-regression.sh "$MNT/usr/sbin/dyld-inject-regression"
    chmod 755 "$MNT/usr/sbin/dyld-inject-regression"
    echo "+ wrote $MNT/usr/sbin/dyld-inject-regression"
fi

# DAR-340/C1 regression probe (see docs/sudo-su-dyld-regression.md).
if [ -f tools/userland_staging/libc_build/dyld_inject_probe.dylib ]; then
    cp tools/userland_staging/libc_build/dyld_inject_probe.dylib "$MNT/usr/lib/dyld_inject_probe.dylib"
    chmod 755 "$MNT/usr/lib/dyld_inject_probe.dylib"
    echo "+ wrote $MNT/usr/lib/dyld_inject_probe.dylib"
fi

# su's own real openpam_configure.c defaults every uncovered PAM
# facility to "required pam_deny.so" (checked directly) -- without a
# real /etc/pam.d/su naming the real pam_unix.so this project's own
# su_pam_unix_static.c registers (build_su.sh), su would fail closed
# on every facility, not just look insecure.
mkdir -p "$MNT/etc/pam.d"
if [ ! -f "$MNT/etc/pam.d/su" ]; then
    cat > "$MNT/etc/pam.d/su" <<'PAM_SU_EOF'
# Written by tools/init_binary/inject_into_sd_image.sh (DAR-339).
# pam_unix.so here is this project's own real static module
# (su_pam_unix_static.c) -- real crypt(3)/getpwnam() against
# /etc/master.passwd, the same real hash format every other
# account-auth tool in this project already reads/writes.
auth		required	pam_unix.so
account		required	pam_unix.so
session		required	pam_unix.so
# DAR-340: the password facility MUST be present. openpam_configure.c:337-344
# defaults every EMPTY facility (incl. password) to `required pam_deny.so`, and
# no pam_deny.so module exists here, so an absent line makes pam_start() itself
# fail ("no pam_deny.so found") and su never runs at all. su never calls
# pam_chauthtok; pam_unix's pam_sm_chauthtok (su_pam_unix_static.c:141) is a
# no-op that returns PAM_SUCCESS.
password	required	pam_unix.so
PAM_SU_EOF
    chmod 644 "$MNT/etc/pam.d/su"
    echo "+ wrote $MNT/etc/pam.d/su"
else
    echo "+ $MNT/etc/pam.d/su already exists -- leaving as-is"
fi

# --- sudo (DAR-340) ---------------------------------------------------
# Real sudo-project/sudo v1.9.17p2 built by tools/userland_staging/
# build_sudo.sh (UNVERIFIED on a booted target as of this entry -- first
# drafted on a Linux host with no SDK). Like su, sudo refuses to run
# unless it is setuid root (third_party/sudo/src/sudo.c:994, "%s must be
# owned by uid %d and have the setuid bit set"), and it refuses a sudoers
# file that fails sudo_secure_file() (plugins/sudoers/sudoers.c:1297 sudo_secure_fd;
# the build passes -DSUDOERS_MODE=0440 -DSUDOERS_UID=0 -DSUDOERS_GID=0). Auth is plugins/sudoers/auth/passwd.c
# (getpwnam + crypt against /etc/master.passwd) -- no PAM, so no
# /etc/pam.d/sudo is written.
if [ -f tools/userland_staging/sudo.macho ]; then
    install_setuid_root tools/userland_staging/sudo.macho "$MNT/usr/bin/sudo" sudo
    if [ ! -f "$MNT/etc/sudoers" ]; then
        cat > "$MNT/etc/sudoers" <<'SUDOERS_EOF'
# Written by tools/init_binary/inject_into_sd_image.sh (DAR-340).
# Edit with visudo-equivalent care: a syntax error locks everyone out.
# secure_path (DAR-340/M1): the invoking user's PATH is never trusted.
Defaults	secure_path="/usr/bin:/bin:/usr/sbin:/sbin"
# NOT SUPPORTED, do not add: `Defaults noexec` / NOEXEC: sudo_noexec.so is not
# built in this port (build_sudo.sh), so the setting would be silently
# ineffective -- it must not be relied on as a restriction (DAR-340/M2).
root	ALL=(ALL) ALL
%wheel	ALL=(ALL) ALL
SUDOERS_EOF
        chown root:wheel "$MNT/etc/sudoers" 2>/dev/null || true
        chmod 440 "$MNT/etc/sudoers"
        sudoers_owner="$(stat -f '%u:%g' "$MNT/etc/sudoers")"
        if [ "$sudoers_owner" = "0:0" ]; then
            echo "+ wrote $MNT/etc/sudoers (mode 440, root:wheel confirmed)"
        elif [ "${REQUIRE_SETUID_ROOT:-0}" = "1" ]; then
            echo "error: /etc/sudoers owner is $sudoers_owner, not 0:0, and REQUIRE_SETUID_ROOT=1" >&2
            exit 1
        else
            echo "! wrote $MNT/etc/sudoers (mode 440) but owner is $sudoers_owner, not 0:0 -- sudo will refuse it (sudo_secure_file) until /usr/sbin/provision-setuid is run from a root shell"
        fi
    else
        echo "+ $MNT/etc/sudoers already exists -- leaving as-is"
    fi
else
    echo "= tools/userland_staging/sudo.macho not built -- run build_sudo.sh first if wanted, skipping"
fi

# /var/empty: sshd's real privilege-separation chroot directory
# (_PATH_PRIVSEP_CHROOT_DIR). sshd.c's own privsep_preauth() fatals with
# "Missing privilege separation directory" unless it exists, is owned by
# root and is not group/world-writable -- that check is real, unmodified
# upstream code (misc.c's safe_path()), not a convention this project
# invented, so the directory has to be here before sshd will start at all.
mkdir -p "$MNT/var/empty"
chmod 755 "$MNT/var/empty"
echo "+ ensured $MNT/var/empty exists, mode 755 (sshd's real privsep chroot dir)"

mkdir -p "$MNT/etc/ssh"
chmod 755 "$MNT/etc/ssh"
if [ ! -f "$MNT/etc/ssh/sshd_config" ]; then
    # Real upstream sshd_config (crypto/openssh/sshd_config, shipped
    # commented-out-defaults file) is NOT copied verbatim: four of this
    # system's answers genuinely differ from the upstream defaults, and a
    # config full of commented-out defaults would hide that. Each active
    # line below says why it is here.
    cat > "$MNT/etc/ssh/sshd_config" <<'SSHD_CONF_EOF'
# Written by tools/init_binary/inject_into_sd_image.sh (DAR-183).
# Every line here is a deliberate deviation from OpenSSH's own defaults;
# anything not listed keeps the real upstream default.

# This build has no OpenSSL (see build_sshd.sh), so ssh-ed25519 is the
# only host key type it can make or load. Naming it explicitly keeps
# sshd from logging a failure for each of the RSA/ECDSA keys it would
# otherwise look for.
HostKey /etc/ssh/ssh_host_ed25519_key

# root is currently the only account this image provisions
# (/etc/master.passwd, written further down this same script), so the
# upstream default of "prohibit-password" would lock the system out of
# its own sshd entirely.
PermitRootLogin yes

# No PAM in this build (OpenPAM here has no service modules), so the
# password path is the real crypt(3)/master.passwd one -- the same hashes
# tools/pwtool writes and login(1) verifies.
PasswordAuthentication yes

# sftp-server.c is compiled INTO sshd in this port; there is no separate
# /usr/libexec/sftp-server binary on the image, so the subsystem has to
# be the in-process one.
Subsystem	sftp	internal-sftp
SSHD_CONF_EOF
    chmod 644 "$MNT/etc/ssh/sshd_config"
    echo "+ wrote $MNT/etc/ssh/sshd_config"
else
    echo "+ $MNT/etc/ssh/sshd_config already exists -- leaving as-is"
fi

# sshd_config's own HostKey line above names /etc/ssh/ssh_host_ed25519_key,
# but nothing else in this script ever created that file -- a real,
# missing provisioning step found 2026-09-21 (a booted image's sshd.c own
# real, unmodified load_hostkeys() call would find zero usable host keys
# and refuse to start; real upstream sshd requires at least one). The
# target's own ssh-keygen.macho (copied above) can't do this itself: it's
# a target-arch Mach-O built against this project's own freestanding
# libc, not runnable on this host, same real reason
# tools/pwtool/hash_password_host.c exists as a host-side stand-in for
# provisioning /etc/master.passwd before real target userland exists to
# do it. ed25519 is a standard OpenSSH key format either side can read,
# so the host's own real ssh-keygen (already installed everywhere this
# script runs) generates the identical real key type sshd_config asks
# for -- not a stand-in format, the real one.
if [ ! -f "$MNT/etc/ssh/ssh_host_ed25519_key" ]; then
    ssh-keygen -q -t ed25519 -f "$MNT/etc/ssh/ssh_host_ed25519_key" -N "" -C "root@$(basename "$MNT")"
    chmod 600 "$MNT/etc/ssh/ssh_host_ed25519_key"
    chmod 644 "$MNT/etc/ssh/ssh_host_ed25519_key.pub"
    echo "+ generated $MNT/etc/ssh/ssh_host_ed25519_key (+ .pub)"
else
    echo "+ $MNT/etc/ssh/ssh_host_ed25519_key already exists -- leaving as-is"
fi

mkdir -p "$MNT/tmp"
echo "+ ensured $MNT/tmp exists (real fopen()-family O_CREAT target dir -- same"
echo "  class of gap as /etc and /dev above; found via a real QEMU boot-test"
echo "  FAIL when tools/userland_staging/libc_stdio_smoketest.macho's real"
echo "  fopen(\"/tmp/...\", \"w\") returned NULL on a fresh image with no /tmp)"

mkdir -p "$MNT/var/run"
echo "+ ensured $MNT/var/run exists (real bind(2) target dir for AF_UNIX"
echo "  Sockets -- DAR-153, launchd's real socketgroup_setup_one() needs the"
echo "  parent directory to exist before bind(\"/var/run/syslog\") can succeed)"

# Real macOS convention (not this project's guess): regular-user home
# directories live at /Users/<name>, confirmed against this host Mac's
# own /Users. Real macOS itself gives root a separate home, /var/root -
# this project's own root entry (further below, "root:...::0:0::/:
# /bin/sh") instead uses "/" for root's home, a real, existing
# divergence from that convention, not something this change touches
# either way. Only the bare directory: this project provisions no
# non-root account yet (only root + the sshd:75 privsep account,
# neither of which would live under /Users even on real macOS), so
# there is no specific user's home directory to create here until a
# real account exists - adding one is a separate step (a master.passwd
# entry + a real home directory + a real login shell), not implied by
# this bare directory alone.
mkdir -p "$MNT/Users"
chmod 755 "$MNT/Users"
echo "+ ensured $MNT/Users exists, mode 755 (real macOS convention for"
echo "  regular-user home directories; no user account provisioned under"
echo "  it yet)"

# DAR-222: real syslogd (third_party/syslog/syslogd.tproj/daemon.h)
# hardcodes _PATH_SYSLOGD_LOG "/var/log/syslogd.log" and bsd_out's real
# ASL store path is also under /var/log -- this is not the cause of
# syslogd's dispatch_once() crash (that's memory corruption, unrelated),
# but it's a real, separate gap the ticket also flags: even
# once syslogd stays up, bsd_out has nowhere to write without this.
mkdir -p "$MNT/var/log/asl/Logs"
echo "+ ensured $MNT/var/log/asl/Logs exists (real syslogd.tproj/daemon.h"
echo "  _PATH_SYSLOGD_LOG target + real asl.conf.osx's aslmanager Logs dir)"

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
    : > "$MNT/etc/group"
    chmod 600 "$MNT/etc/master.passwd"
    chmod 644 "$MNT/etc/group"
    echo "+ wrote $MNT/etc/master.passwd (root:darwin, uid/gid 0); base groups follow"
else
    echo "+ $MNT/etc/master.passwd already exists -- leaving accounts as-is"
fi

# Apple's base BSD groups, exactly as a real Mac's /etc/group has them
# (macOS 11 /etc/group; the underscore-prefixed service groups are left
# out, as are member names for accounts this image does not have, e.g.
# mail's _teamsserver). gid 0 is "wheel" on a Mac, not "root": Apple's
# pwd_mkdb refuses to run without it (SHADOW_GROUP "wheel",
# third_party/system_cmds/pwd_mkdb.tproj/pwd_mkdb.c:77, :154-155), which
# made every pw(8) useradd/usermod/userdel fail (DAR-341), and
# libinfo_smoketest.c:74 already expects getgrnam("wheel") == 0.
#
# Also migrates images provisioned before this: an old "root:*:0:" gid-0
# entry is renamed to wheel, and any base group missing by name and gid
# is appended. Idempotent.
if [ -f "$MNT/etc/group" ]; then
    if grep -q '^root:\*:0:' "$MNT/etc/group" && ! grep -q '^wheel:' "$MNT/etc/group"; then
        sed -i '' 's/^root:\*:0:.*$/wheel:*:0:root/' "$MNT/etc/group"
        echo "+ renamed gid 0 in $MNT/etc/group from root to wheel (Apple's name)"
    fi
    while IFS=: read -r gname gpw ggid gmem; do
        if ! awk -F: -v n="$gname" -v g="$ggid" '$1==n || $3==g {f=1} END {exit !f}' "$MNT/etc/group"; then
            printf '%s:%s:%s:%s\n' "$gname" "$gpw" "$ggid" "$gmem" >> "$MNT/etc/group"
            echo "+ added group $gname ($ggid) to $MNT/etc/group"
        fi
    done <<'GROUPS'
nobody:*:-2:
nogroup:*:-1:
wheel:*:0:root
daemon:*:1:root
kmem:*:2:root
sys:*:3:root
tty:*:4:root
operator:*:5:root
mail:*:6:
bin:*:7:
procview:*:8:root
procmod:*:9:root
owner:*:10:
everyone:*:12:
group:*:16:
staff:*:20:root
utmp:*:45:
admin:*:80:root
GROUPS
fi

# The "sshd" privilege-separation account (DAR-183). sshd.c:1877 does a
# real getpwnam(SSH_PRIVSEP_USER) and exits with "Privilege separation
# user sshd does not exist" if it is missing -- unmodified upstream code.
# Added separately from the root-account block above so it also lands on
# images provisioned before this existed, and never twice.
if [ -f "$MNT/etc/master.passwd" ] && ! grep -q '^sshd:' "$MNT/etc/master.passwd"; then
    # '*' as the hash: no password can ever hash to it, which is the real
    # BSD convention for an account that must never be logged into. uid/gid
    # 75 is Apple's own real _sshd uid on macOS; /var/empty + /usr/bin/false
    # match what real OpenSSH deployments use (this image has no
    # /usr/bin/false yet, which is harmless -- the account exists to be
    # setuid()'d to, never to run a login shell).
    printf 'sshd:*:75:75::0:0:sshd privsep:/var/empty:/usr/bin/false\n' >> "$MNT/etc/master.passwd"
    echo "+ added the sshd privsep account to $MNT/etc/master.passwd (uid/gid 75, no password)"
fi
if [ -f "$MNT/etc/group" ] && ! grep -q '^sshd:' "$MNT/etc/group"; then
    printf 'sshd:*:75:\n' >> "$MNT/etc/group"
    echo "+ added the sshd group to $MNT/etc/group (gid 75)"
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

# DAR-182: a real, static /etc/resolv.conf for QEMU testing. Real
# __res_vinit() (third_party/libresolv/res_init.c:1163) always tries the
# real dns_configuration_copy() XPC path to configd FIRST, and only falls
# back to parsing this file when that returns NULL (no reachable configd
# dnsinfo service) -- exactly this image's real state today with no DHCP
# lease (DAR-170, blocked on DAR-316). 10.0.2.3 is QEMU usermode
# networking's own built-in DNS proxy, reachable with zero extra QEMU
# flags (see runvm.sh's `-netdev user` line) -- not this project's guess,
# real SLIRP behavior. Never overwrites a real resolv.conf a later real
# DHCP/configd flow may have written.
if [ ! -f "$MNT/etc/resolv.conf" ]; then
    echo "nameserver 10.0.2.3" > "$MNT/etc/resolv.conf"
    chmod 644 "$MNT/etc/resolv.conf"
    echo "+ wrote $MNT/etc/resolv.conf (nameserver 10.0.2.3, QEMU usermode DNS proxy)"
else
    echo "+ $MNT/etc/resolv.conf already exists -- leaving as-is"
fi

# /Library/Preferences/SystemConfiguration/preferences.plist (DAR-304,
# 2026-09-14): real gap found via live plugin_support.c/ipconfigd.c
# instrumentation -- IPConfiguration's real prime() -> start_initialization()
# -> configure_from_cache() reads Setup:/Network/Service/*/IPv4 from
# SCDynamicStore, which PreferencesMonitor's real (unmodified) prefsmon.c
# populates FROM this exact file at boot. With no file at all,
# entity_all() returns NULL, configure_from_cache() takes the
# unblock_startup() no-op path, and en0 never gets DHCP -- confirmed via
# a real fprintf(stderr,...) instrumented boot showing
# "all_ipv4=0x0 ... S_interfaces count=2" and "calling unblock_startup
# (no configured services)".
#
# Real Apple's prefsmon.c DOES have automatic default-config creation
# (establishNewPreferences(), storeCallback()) for exactly this
# "no preferences.plist yet" case -- but it's gated entirely on a real
# "quiet"/"timeout" key that InterfaceNamer publishes once its own real
# IOKit driver-matching settles (State:/.../InterfaceNamer, read via
# kInterfaceNamerKey_Quiet/kInterfaceNamerKey_Timeout). This project's
# InterfaceNamer is a deliberate stand-in (configd_builtin_plugins_stub.c,
# blocked on the real IOKitLib/CF/dispatch/lockdown wall, tracked
# separately) that never publishes that key, so the real auto-create path
# can never fire here -- confirmed by reading storeCallback()'s own
# source, not guessed.
#
# Staging this file directly is the real, faithful equivalent of what a
# real Mac's first-boot/imaging process already does before configd ever
# runs for the first time (this project's own SD-image provisioning IS
# that first-boot imaging step) -- not a bypass of real config logic,
# just supplying the one input real InterfaceNamer would otherwise have
# triggered PreferencesMonitor to synthesize. Schema is real Apple's
# well-documented SCPreferences format (CurrentSet -> Sets/<UUID> ->
# Network/Service/<UUID>/__LINK__ -> NetworkServices/<UUID>), a single
# DHCP-configured Ethernet service bound to en0.
mkdir -p "$MNT/Library/Preferences/SystemConfiguration"
PREFS_PLIST="$MNT/Library/Preferences/SystemConfiguration/preferences.plist"
# Overwrite not just when missing but also when it's a real, on-disk but
# EMPTY skeleton (just "__VERSION__", no "NetworkServices" key) -- real
# SCPreferencesCreate() writes exactly that skeleton the first time any
# code opens a nonexistent preferences.plist, which happens during a
# plain QEMU boot before this script's own real config below is staged.
# Without this check, a stale skeleton from an earlier boot/test run
# would make this script think real config already exists and silently
# skip writing it -- found the hard way validating DAR-304's own fix.
PREFS_NEEDS_WRITE=1
if [ -f "$PREFS_PLIST" ]; then
    if /usr/bin/plutil -extract NetworkServices raw -o - "$PREFS_PLIST" >/dev/null 2>&1; then
        PREFS_NEEDS_WRITE=0
    fi
fi
if [ "$PREFS_NEEDS_WRITE" = "1" ]; then
    cat > "$PREFS_PLIST" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CurrentSet</key>
	<string>/Sets/8DCD07A2-0000-0000-0000-000000000000</string>
	<key>NetworkServices</key>
	<dict>
		<key>8DCD07A2-0000-0000-0000-000000000001</key>
		<dict>
			<key>DNS</key>
			<dict/>
			<key>IPv4</key>
			<dict>
				<key>ConfigMethod</key>
				<string>DHCP</string>
			</dict>
			<key>Interface</key>
			<dict>
				<key>DeviceName</key>
				<string>en0</string>
				<key>Hardware</key>
				<string>Ethernet</string>
				<key>Type</key>
				<string>Ethernet</string>
				<key>UserDefinedName</key>
				<string>Ethernet</string>
			</dict>
			<key>Proxies</key>
			<dict>
				<key>ExceptionsList</key>
				<array>
					<string>*.local</string>
					<string>169.254/16</string>
				</array>
			</dict>
			<key>UserDefinedName</key>
			<string>Ethernet</string>
		</dict>
	</dict>
	<key>Sets</key>
	<dict>
		<key>8DCD07A2-0000-0000-0000-000000000000</key>
		<dict>
			<key>Network</key>
			<dict>
				<key>Global</key>
				<dict>
					<key>IPv4</key>
					<dict>
						<key>ServiceOrder</key>
						<array>
							<string>8DCD07A2-0000-0000-0000-000000000001</string>
						</array>
					</dict>
				</dict>
				<key>Service</key>
				<dict>
					<key>8DCD07A2-0000-0000-0000-000000000001</key>
					<dict>
						<key>__LINK__</key>
						<string>/NetworkServices/8DCD07A2-0000-0000-0000-000000000001</string>
					</dict>
				</dict>
			</dict>
			<key>UserDefinedName</key>
			<string>Automatic</string>
		</dict>
	</dict>
</dict>
</plist>
EOF
    chmod 644 "$PREFS_PLIST"
    echo "+ wrote $PREFS_PLIST"
    echo "  (real SCPreferences schema, one DHCP-configured Ethernet service on en0 -- DAR-304)"
else
    echo "+ $PREFS_PLIST already has real NetworkServices config -- leaving as-is"
fi

# /etc/login.conf: pw(8) wants a login class (else "setting crypt(3) format:
# No such file or directory"); see tools/userland_staging/login.conf.
cp tools/userland_staging/login.conf "$MNT/etc/login.conf"
chmod 644 "$MNT/etc/login.conf"
echo "+ wrote login.conf to $MNT/etc/"

# /etc/profile: CLICOLOR=1 and a colour-capable TERM default for login shells
# (serial console and ssh); see tools/userland_staging/profile.
cp tools/userland_staging/profile "$MNT/etc/profile"
chmod 644 "$MNT/etc/profile"
echo "+ wrote profile to $MNT/etc/"

# /etc/ttys (DAR-164, real gap found completing the real login(1) E2E
# flow): real login.c's own rootterm() (system_cmds/login.tproj/login.c,
# non-PAM branch) refuses ANY root login unless getttynam(ttyn) finds a
# matching /etc/ttys entry with the TTY_SECURE flag set -- a real,
# intentional Apple/BSD safety check, not a bug. This project's own
# getttyent.c port (build_login.sh) already degrades gracefully when
# /etc/ttys is entirely missing (same "no real TZ database"-class honest
# gap as getty's own /etc/gettytab handling) -- but "gracefully" here
# means rootterm() returns false, so root got a real, correct, but
# permanent "root login refused on this terminal" rejection every single
# time (confirmed via a real QEMU boot: login prompts for a password,
# accepts input, then unconditionally loops back to a fresh login:
# prompt) until this file exists. `console` is the real basename
# ttyname(STDIN_FILENO) resolves to for this project's serial/console tty
# (login.c strips the "/dev/" prefix itself before calling getttynam()) --
# confirmed via the same real QEMU boot. Standard /etc/ttys field order
# (name, getty program, terminal type, flags) -- the getty-program/type
# fields are unused by this project (nothing spawns getty from this file,
# see build_getty.sh's own header on the real getty->login handoff this
# project actually uses instead), included only because getttyent.c's
# real parser expects the field positions to be present.
if [ ! -f "$MNT/etc/ttys" ]; then
    cat > "$MNT/etc/ttys" <<'EOF'
console		"/usr/libexec/getty std.9600"	vt100	on secure
EOF
    chmod 644 "$MNT/etc/ttys"
    echo "+ wrote $MNT/etc/ttys (console marked secure) -- real login(1)'s own"
    echo "  rootterm() check needs this before it will ever allow a root login"
else
    echo "+ $MNT/etc/ttys already exists -- leaving as-is"
fi

diskutil unmount "$MNT"
hdiutil detach "$DISK"
trap - EXIT

echo "done -- $SD_IMAGE's /sbin/launchd, /sbin/pwtool, and /etc are up to date"
