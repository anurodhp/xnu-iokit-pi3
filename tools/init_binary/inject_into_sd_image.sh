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
set -e
cd "$(dirname "$0")/../.."

SD_IMAGE="${1:-qemu/hfs_root_test.img}"
PART_IDX="${2:-2}"

if [ ! -f "$SD_IMAGE" ]; then
    echo "error: SD card image not found: $SD_IMAGE" >&2
    exit 1
fi

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

if [ -f tools/userland_staging/libc_build/launchd.macho ]; then
    cp tools/userland_staging/libc_build/launchd.macho "$MNT/sbin/launchd"
    chmod 700 "$MNT/sbin/launchd"
    echo "+ wrote $(wc -c < tools/userland_staging/libc_build/launchd.macho) bytes to $MNT/sbin/launchd"
    echo "  (real, dynamically-linked launchd -- Phase 6.8 Milestone 4, see"
    echo "  tools/userland_staging/build_launchd.sh; needs /usr/lib/dyld +"
    echo "  the full dylib stack already installed above)"

    # Real gap found wiring launchd up for its first actual boot: ICU's
    # LC_ID_DYLIB install names are RELATIVE ("libicuuc.66.dylib", not an
    # absolute /usr/lib/... path -- confirmed via otool -D), so dyld
    # resolves them against launchd's own cwd at exec time, which the
    # kernel leaves as "/" for the very first process (no parent to
    # inherit a cwd from). Installing at image root, matching that
    # relative name literally, rather than rebuilding ICU with real
    # absolute install names (a separate, larger follow-up -- this is a
    # known simplification, not the real Apple convention).
    ICU_LIB="tools/userland_staging/icu_build/target/lib"
    for d in libicuuc.66.dylib libicui18n.66.dylib libicudata.66.dylib; do
        if [ -f "$ICU_LIB/$d" ]; then
            cp "$ICU_LIB/$d" "$MNT/$d"
            chmod 755 "$MNT/$d"
            echo "+ wrote $(wc -c < "$ICU_LIB/$d") bytes to $MNT/$d (launchd's real ICU dep, relative install name)"
        else
            echo "error: $ICU_LIB/$d not found -- run build_icu_target.sh first" >&2
            exit 1
        fi
    done
else
    echo "error: tools/userland_staging/libc_build/launchd.macho not built -- run build_launchd.sh first" >&2
    exit 1
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

# cron.macho/crontab.macho intentionally NOT installed here (2026-08-22).
# Both build real (tools/userland_staging/build_cron.sh still produces
# working, fully-static Mach-O binaries, and can be run/inspected on its
# own) but cron.macho itself has a real, unresolved startup crash --
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

if [ -f tools/userland_staging/pthread_smoketest.macho ]; then
    mkdir -p "$MNT/bin"
    cp tools/userland_staging/pthread_smoketest.macho "$MNT/bin/pthread_smoketest"
    chmod 755 "$MNT/bin/pthread_smoketest"
    echo "+ wrote $(wc -c < tools/userland_staging/pthread_smoketest.macho) bytes to $MNT/bin/pthread_smoketest"
    echo "  (real libpthread.a smoketest -- now includes a real pthread_create()"
    echo "  second thread + contended pthread_mutex under ulock, 2026-08-24)"
else
    echo "= tools/userland_staging/pthread_smoketest.macho not built -- run tools/userland_staging/build_libpthread.sh first if wanted, skipping"
fi

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
        case "$install_name" in
            /usr/lib/*) : ;;
            *)
                echo "error: $d has no real LC_ID_DYLIB install_name (otool -D returned '$install_name') -- refusing to guess a destination path" >&2
                exit 1
                ;;
        esac
        dest="$MNT/usr/lib${install_name#/usr/lib}"
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
    mkdir -p "$MNT/usr/sbin" "$MNT/Library/LaunchDaemons"
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
else
    echo "= tools/userland_staging/syslogd.macho not built -- run tools/userland_staging/build_syslogd.sh first if wanted, skipping"
fi

if [ -f tools/userland_staging/libcxxabi_smoketest.macho ]; then
    mkdir -p "$MNT/bin"
    cp tools/userland_staging/libcxxabi_smoketest.macho "$MNT/bin/libcxxabi_smoketest"
    chmod 755 "$MNT/bin/libcxxabi_smoketest"
    echo "+ wrote $(wc -c < tools/userland_staging/libcxxabi_smoketest.macho) bytes to $MNT/bin/libcxxabi_smoketest"
    echo "  (real LLVM libc++abi smoketest -- dynamic_cast/RTTI/operator"
    echo "  new+delete OK; real throw/catch reported non-fatally, see"
    echo "  tools/userland_staging/llvm_libcxxabi_smoketest.cpp)"
else
    echo "= tools/userland_staging/libcxxabi_smoketest.macho not built -- run tools/userland_staging/build_llvm_libcxxabi.sh first if wanted, skipping"
fi

if [ -f tools/userland_staging/libcxx_smoketest.macho ]; then
    mkdir -p "$MNT/bin"
    cp tools/userland_staging/libcxx_smoketest.macho "$MNT/bin/libcxx_smoketest"
    chmod 755 "$MNT/bin/libcxx_smoketest"
    echo "+ wrote $(wc -c < tools/userland_staging/libcxx_smoketest.macho) bytes to $MNT/bin/libcxx_smoketest"
    echo "  (real LLVM libc++ smoketest -- std::vector/sort/string/"
    echo "  shared_ptr, see tools/userland_staging/llvm_libcxx_smoketest.cpp)"
else
    echo "= tools/userland_staging/libcxx_smoketest.macho not built -- run tools/userland_staging/build_llvm_libcxx.sh first if wanted, skipping"
fi

if [ -f tools/userland_staging/libc_build/kqueue_smoketest.macho ]; then
    mkdir -p "$MNT/bin"
    cp tools/userland_staging/libc_build/kqueue_smoketest.macho "$MNT/bin/kqueue_smoketest"
    chmod 755 "$MNT/bin/kqueue_smoketest"
    echo "+ wrote $(wc -c < tools/userland_staging/libc_build/kqueue_smoketest.macho) bytes to $MNT/bin/kqueue_smoketest"
    echo "  (real kqueue()/kevent() EVFILT_TIMER round-trip -- Phase 6.6"
    echo "  libdispatch prereq, see tools/userland_staging/kqueue_smoketest.c)"
else
    echo "= tools/userland_staging/libc_build/kqueue_smoketest.macho not built -- run tools/userland_staging/build_kqueue_smoketest.sh first if wanted, skipping"
fi

if [ -f tools/userland_staging/libinfo_smoketest.macho ]; then
    mkdir -p "$MNT/bin"
    cp tools/userland_staging/libinfo_smoketest.macho "$MNT/bin/libinfo_smoketest"
    chmod 755 "$MNT/bin/libinfo_smoketest"
    echo "+ wrote $(wc -c < tools/userland_staging/libinfo_smoketest.macho) bytes to $MNT/bin/libinfo_smoketest"
    echo "  (real Libinfo si_module files-only lookups -- DAR-181, see"
    echo "  tools/userland_staging/libinfo_smoketest.c; needs /usr/lib/libinfo.dylib)"
else
    echo "= tools/userland_staging/libinfo_smoketest.macho not built -- run tools/userland_staging/build_libinfo.sh first if wanted, skipping"
fi

# Real Phase 4 step 3 "remaining commands" tiers (docs/roadmap.md) --
# each build_*_cmds.sh script writes one <name>.macho per real command
# it built; every one of them lands in /bin, matching real BSD/macOS
# layout (all of shell_cmds' Tier 1/2 set below is genuinely /bin, not
# /usr/bin, on a real system). Generic loop instead of one if-block per
# command -- this list only grows as more tiers land.
mkdir -p "$MNT/bin"
for cmd in true false yes pwd basename dirname echo printf hostname logname cat wc head tail mkdir touch rm which env sleep printenv cut uniq ln id cp mv rev fold paste expand unexpand comm colrm lam rs rmdir mkfifo mknod chflags seq jot test kill tee mktemp hexdump stat cksum pathchk du chmod chown nice renice nohup chroot apply vis unvis grep sed nl xargs ed ls find date gzip compress diff tar less df killall getty md5; do
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

mkdir -p "$MNT/var/run"
echo "+ ensured $MNT/var/run exists (real bind(2) target dir for AF_UNIX"
echo "  Sockets -- DAR-153, launchd's real socketgroup_setup_one() needs the"
echo "  parent directory to exist before bind(\"/var/run/syslog\") can succeed)"

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
