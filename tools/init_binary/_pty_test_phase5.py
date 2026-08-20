#!/usr/bin/env python3
"""Ad-hoc pty-driven QEMU interactive test harness for Phase 5 (login/su/
pwtool) verification. Adapted from _pty_test.py (same file, same
disposable-scratch-tool status -- delete when done) with one real change:
prompt detection also recognizes pwtool's interactive password prompts
("New password: ", "Retype new password: ") and init.c's real login
prompts ("login: ", "Password: "), not just the shell's own "% " prompt --
needed because this test drives pwtool's interactive `passwd` subcommand
and a real login session, neither of which end in "% "."""
import pty, os, time, select, subprocess, socket, sys

cmds_file, log_file, mon_sock = sys.argv[1], sys.argv[2], sys.argv[3]
img = sys.argv[4] if len(sys.argv) > 4 else "qemu/phase5_test.img"
with open(cmds_file) as f:
    commands = [l.rstrip("\n") for l in f if l.strip() != "" or True]
    # keep truly-empty lines too (e.g. blank "login:" retry) -- only skip
    # comment lines
    commands = [l.rstrip("\n") for l in open(cmds_file) if not l.startswith("#")]
    commands = [l.rstrip("\n") for l in commands]

# NOTE (2026-08-15): two or more concurrent QEMU/TCG instances on this
# host (e.g. multiple parallel forks each boot-testing their own scratch
# image at once) starve each other for CPU badly enough that a boot which
# normally reaches kernel text in a few seconds instead never gets there
# at all inside this harness's own 240s loop -- reproduced with a
# genuinely unmodified, previously-known-good image/kernel/dtb, purely by
# having 2 other unrelated QEMU processes running at 100% CPU each at the
# same time; the hang vanished immediately once those exited. Real
# resource contention, not a boot regression. The fix is a CALLER-SIDE
# convention, not something this script enforces itself: wrap whatever
# invokes this script with a shared `mkdir qemu/.qemu_test_lock` /
# poll-retry / `rmdir` (trap-protected) around the whole run, so only one
# QEMU instance boots at a time project-wide. Deliberately NOT locking
# inside this script itself -- an earlier version of this file did that
# and self-deadlocked the moment a caller ALSO wrapped it in the same
# outer lock (mkdir already held by the caller's own shell, this
# script's own mkdir attempt then waits forever for a lock its own
# parent process is holding). One lock, acquired once, at the outermost
# caller -- see CLAUDE.md's "Boot / test commands" section.

master, slave = pty.openpty()
cmd = [
    "./third_party/qemu-zhuowei/aarch64-softmmu/qemu-system-aarch64",
    "-M", "raspi3", "-m", "1024", "-display", "none",
    "-serial", "null", "-serial", "stdio",
    "-kernel", "build/xnu-latest-build/out/kernel.development.bcm2837-7195.unstripped",
    "-dtb", "devicetrees/rpi3.dtb",
    "-drive", f"file={img},if=sd,format=raw",
    "-monitor", f"unix:{mon_sock},server,nowait",
    "-append", "debug=0x8 kextlog=0xfff -noprogress -v rd=disk0s2 serial=3 cpu-factor=10",
]
proc = subprocess.Popen(cmd, stdin=slave, stdout=slave, stderr=slave, close_fds=True)
os.close(slave)

logf = open(log_file, "wb")
buf = b""
start = time.time()
sent_cont = False
cmd_idx = 0
last_growth = time.time()
last_len = 0

PROMPT_SUFFIXES = [b"% ", b"login: ", b"Password: ", b"New password: ", b"Retype new password: ", b"-bash-3.2# ", b"(y/n [n]) "]

def buf_tail_has_prompt():
    tail = buf[-40:]
    return any(tail.endswith(p) for p in PROMPT_SUFFIXES)

while time.time() - start < 240:
    r, _, _ = select.select([master], [], [], 1.0)
    if master in r:
        try:
            data = os.read(master, 65536)
        except OSError:
            break
        if not data:
            break
        buf += data
        logf.write(data)
        logf.flush()
    if len(buf) != last_len:
        last_len = len(buf)
        last_growth = time.time()

    if not sent_cont and b"DIAG: first abort" in buf and (time.time() - last_growth) > 2:
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(mon_sock)
            time.sleep(0.2)
            s.recv(4096)
            s.send(b"cont\n")
            time.sleep(0.3)
            s.recv(4096)
            s.close()
        except Exception as e:
            logf.write(("PYERR cont: %s\n" % e).encode())
        sent_cont = True

    if cmd_idx < len(commands) and buf_tail_has_prompt() and (time.time() - last_growth) > 0.3:
        time.sleep(0.3)
        os.write(master, (commands[cmd_idx] + "\n").encode())
        cmd_idx += 1
        last_growth = time.time()
        time.sleep(1.5)

    if cmd_idx >= len(commands) and buf_tail_has_prompt():
        break

time.sleep(1.5)
try:
    while True:
        r, _, _ = select.select([master], [], [], 0.5)
        if master not in r:
            break
        data = os.read(master, 65536)
        if not data:
            break
        buf += data
        logf.write(data)
        logf.flush()
except OSError:
    pass

proc.kill()
logf.close()
print("DONE cmd_idx=%d/%d" % (cmd_idx, len(commands)))
