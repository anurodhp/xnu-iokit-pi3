#!/usr/bin/env python3
"""Boot the from-source kernel under QEMU and assert on its serial console.

This is the reusable form of the ad-hoc pty harnesses that kept getting
rewritten per-session (tools/init_binary/_pty_test.py,
_pty_test_phase5.py). It exists because two things about this project's
boot-verification path are easy to get wrong and both produce a *silent*
false negative -- a log that looks like "the kernel never booted" when
nothing is actually broken:

  1. QEMU's `-serial stdio` needs a real tty. Redirect runvm.sh's stdout
     to a pipe or a file and the guest's console output simply never
     arrives; the log contains only the QEMU loader's own "xnu" /
     "DEBUG mmap ok" lines and stops there. Every boot here therefore
     runs under a pty, never a pipe.

  2. Two or more concurrent QEMU/TCG instances on this host starve each
     other badly enough that a known-good boot never reaches kernel text
     inside a normal timeout (see _pty_test_phase5.py's note -- real
     resource contention, reproduced with an unmodified known-good
     image). So this script takes a project-wide mkdir lock and holds it
     for the whole run.

Usage:
    tools/boot_test.py --timeout 180 --log /tmp/boot.log \\
        --expect 'BCM2837DWC2' --expect-fail 'panic'
    tools/boot_test.py --cmd 'ls /' --cmd 'echo hi'   # drive the shell

Exit status is 0 only if every --expect matched and no --expect-fail did.
Environment variables are passed through to runvm.sh, so its own knobs
still work, e.g.:
    USB_DEVICES= tools/boot_test.py ...      # boot with no USB devices
    SD_IMAGE=qemu/fat32_test.dmg ROOT_DEV=disk0 tools/boot_test.py ...
"""
import argparse
import os
import pty
import re
import select
import signal
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOCK = os.path.join(REPO, "qemu", ".qemu_test_lock")

# The shell's own prompt. Used both to know when userland is up and to
# decide when it is safe to send the next --cmd. Real bash (docs/roadmap.md:
# "Interactive shell is real bash") prints "-bash-3.2# " for root, not the
# old custom init-shell's "% " this constant used to hold - a stale value
# here doesn't error, it just silently makes every --cmd never get sent
# (since_cmd never ends with PROMPT), which looks exactly like the guest
# hanging.
#
# DAR-164: root's real configured login shell (/etc/master.passwd, see
# inject_into_sd_image.sh) is /bin/sh, not /bin/bash -- a real, correct
# system convention this project already follows (/bin/sh is the SAME
# real bash binary, argv[0]-dispatched into sh-compat mode, matching
# real-world "/bin/sh is commonly bash" practice). Real bash's own
# sh-compat prompt is "-sh-3.2# ", not "-bash-3.2# " -- confirmed via a
# real QEMU --login run once DAR-164's login(1) crash fix let a real
# post-auth shell get reached for the first time; every prior --login
# run had silently never sent a single --cmd because of this, which
# looked identical to the guest hanging for the same reason the comment
# above already warns about.
PROMPT_CANDIDATES = ("-bash-3.2# ", "-sh-3.2# ")


def acquire_lock(timeout):
    """Project-wide single-QEMU-at-a-time lock. See module docstring."""
    os.makedirs(os.path.dirname(LOCK), exist_ok=True)
    deadline = time.time() + timeout
    while True:
        try:
            os.mkdir(LOCK)
            return
        except FileExistsError:
            if time.time() > deadline:
                sys.exit(f"error: another QEMU boot-test holds {LOCK}; "
                         f"waited {timeout}s. Remove it if stale.")
            time.sleep(2)


def release_lock():
    try:
        os.rmdir(LOCK)
    except OSError:
        pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--timeout", type=float, default=180,
                    help="seconds to run before giving up (default 180)")
    ap.add_argument("--log", default="qemu/boot_test.log",
                    help="where to write the full console log")
    ap.add_argument("--expect", action="append", default=[],
                    help="regex that MUST appear; repeatable. Once all have "
                         "matched (and no --cmd remains) the boot is cut "
                         "short rather than burning the whole timeout.")
    ap.add_argument("--expect-fail", action="append", default=[],
                    help="regex that must NOT appear; repeatable. Matching "
                         "one aborts the run immediately.")
    ap.add_argument("--cmd", action="append", default=[],
                    help="shell command to type once the prompt appears; "
                         "repeatable, sent in order")
    ap.add_argument("--login", action="store_true",
                    help="log in at the 'login:' prompt before sending any "
                         "--cmd (see tools/init_binary/inject_into_sd_image.sh "
                         "for where the root/darwin account gets provisioned)")
    ap.add_argument("--user", default="root")
    ap.add_argument("--password", default="darwin")
    ap.add_argument("--lock-timeout", type=float, default=600)
    ap.add_argument("--allow-panic", action="store_true",
                    help="do not fail on a kernel panic. By default any "
                         "'panic(' fails the run: since DAR-334 a panic "
                         "resets the machine (BCM2837PlatformExpert::"
                         "haltRestart), and the next boot's output could "
                         "otherwise satisfy --expect and hide it.")
    args = ap.parse_args()

    expect = [(p, re.compile(p)) for p in args.expect]
    if not args.allow_panic and not any(
            "panic" in p for p in args.expect_fail):
        args.expect_fail.append(r"panic\(")
    expect_fail = [(p, re.compile(p)) for p in args.expect_fail]
    pending_cmds = list(args.cmd)
    need_login = args.login
    need_password = False

    acquire_lock(args.lock_timeout)
    log_path = args.log if os.path.isabs(args.log) else os.path.join(REPO, args.log)
    os.makedirs(os.path.dirname(log_path), exist_ok=True)

    master, slave = pty.openpty()
    proc = subprocess.Popen(
        [os.path.join(REPO, "runvm.sh")],
        stdin=slave, stdout=slave, stderr=slave,
        cwd=REPO, close_fds=True, start_new_session=True,
    )
    os.close(slave)

    matched = set()
    failed = None
    # Only the text seen since the last command was sent is scanned for the
    # prompt, so a "% " echoed back inside earlier output cannot be
    # mistaken for a fresh prompt and race the next --cmd out too early.
    since_cmd = ""
    text = ""
    start = time.time()
    logf = open(log_path, "wb")
    try:
        while time.time() - start < args.timeout:
            r, _, _ = select.select([master], [], [], 0.5)
            if not r:
                if proc.poll() is not None:
                    break
                continue
            try:
                chunk = os.read(master, 65536)
            except OSError:
                break
            if not chunk:
                break
            logf.write(chunk)
            logf.flush()
            decoded = chunk.decode("utf-8", errors="replace")
            text += decoded
            since_cmd += decoded

            for pat, rx in expect_fail:
                if rx.search(text):
                    failed = pat
                    break
            if failed:
                # Keep draining before tearing the VM down. A panic is the
                # single most useful thing this harness can capture, and its
                # message plus backtrace arrive AFTER the "panic(" that
                # matched here - killing QEMU immediately truncated the log
                # to a bare "panic(cpu" and threw away the diagnosis, which
                # cost a whole extra boot cycle to rediscover. Serial is slow
                # (115200), so give it real time.
                drain_until = time.time() + 15
                while time.time() < drain_until:
                    r2, _, _ = select.select([master], [], [], 0.5)
                    if not r2:
                        continue
                    try:
                        chunk = os.read(master, 65536)
                    except OSError:
                        break
                    if not chunk:
                        break
                    logf.write(chunk)
                    logf.flush()
                break
            for pat, rx in expect:
                if pat not in matched and rx.search(text):
                    matched.add(pat)
                    print(f"[boot_test] matched --expect {pat!r} at "
                          f"t={time.time() - start:.1f}s")

            # DAR-164 fix: matching bare "login: " (no preceding \n) false-
            # triggered on org.puredarwin.console-login's own real ctrace
            # lines (pd_console_login.c's "pd-console-login: <msg>" prefix
            # literally ends in the substring "login: ") whenever a pty
            # read chunk happened to end right after that colon+space --
            # sent the username far too early, into pd-console-login's own
            # process (not getty's real prompt), so the real login:/
            # Password: exchange downstream never got answered. The real
            # getty prompt is always preceded by a newline (it prints a
            # banner + newline first); ctrace's mid-string "login: "
            # substring never is, since "-login: " is always preceded by
            # "-", not "\n" -- anchoring on "\n" is robust regardless of
            # where any given read() happens to chunk the stream.
            if need_login and since_cmd.endswith("\nlogin: "):
                print(f"[boot_test] logging in as {args.user!r}")
                os.write(master, (args.user + "\n").encode())
                since_cmd = ""
                need_login = False
                need_password = True
            # Real login(1)'s DEFAULT_PASSWD_PROMPT (system_cmds/login.tproj/
            # login.c) is literally "Password:" -- no trailing space, unlike
            # getty's "login: ". Matching "Password: " (with a space) never
            # fired, so the password was never sent and every login run
            # timed out waiting -- found once DAR-164's real login crash fix
            # let a real Password: prompt get reached for the first time.
            elif need_password and since_cmd.endswith("Password:"):
                os.write(master, (args.password + "\n").encode())
                since_cmd = ""
                need_password = False

            if pending_cmds and since_cmd.endswith(PROMPT_CANDIDATES):
                cmd = pending_cmds.pop(0)
                print(f"[boot_test] sending {cmd!r}")
                os.write(master, (cmd + "\n").encode())
                since_cmd = ""

            if not pending_cmds and expect and len(matched) == len(expect):
                # Nothing left to wait for. Give the guest a moment to
                # flush whatever it was mid-way through printing, THEN
                # check readability before reading - a bare os.read() here
                # is a BLOCKING call on this fd, and once the guest reaches
                # an idle prompt with no more output coming (e.g. "login: "
                # with nothing typed), that read never returns. That is a
                # real hang this harness caused, not the guest: the guest
                # had already booted successfully and finished 15 minutes
                # earlier while this call sat blocked, invisible to the
                # timeout loop above (which never gets a chance to
                # re-check once execution is inside this read).
                time.sleep(2)
                r2, _, _ = select.select([master], [], [], 0.5)
                if r2:
                    try:
                        chunk = os.read(master, 65536)
                        logf.write(chunk)
                    except OSError:
                        pass
                break
    finally:
        logf.close()
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
        except OSError:
            pass
        # Always follow up with SIGKILL to the group, even if the direct
        # child exited cleanly: a leaked QEMU is not merely untidy here, it
        # silently poisons the *next* boot test by competing for TCG CPU
        # (see module docstring), so the failure surfaces far from its
        # cause. runvm.sh execs QEMU for the same reason.
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            pass
        time.sleep(0.5)
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
        except OSError:
            pass
        os.close(master)
        release_lock()

    elapsed = time.time() - start
    print(f"[boot_test] ran {elapsed:.1f}s, log: {log_path} "
          f"({os.path.getsize(log_path)} bytes)")

    if failed:
        print(f"[boot_test] FAIL: --expect-fail {failed!r} matched")
        return 1
    missing = [p for p, _ in expect if p not in matched]
    if missing:
        print(f"[boot_test] FAIL: never matched: {missing}")
        return 1
    if pending_cmds:
        print(f"[boot_test] FAIL: never reached a prompt to send: {pending_cmds}")
        return 1
    print("[boot_test] PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
