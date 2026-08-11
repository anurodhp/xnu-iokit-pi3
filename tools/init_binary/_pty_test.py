#!/usr/bin/env python3
"""Ad-hoc pty-driven QEMU interactive test harness. Scratch tool, not part
of the build - reads a newline-separated command script from argv[1] (each
line sent after the prompt appears, with a delay after each), logs raw
output to argv[2]. Not committed as a permanent script; delete when done."""
import pty, os, time, select, subprocess, socket, sys

cmds_file, log_file, mon_sock = sys.argv[1], sys.argv[2], sys.argv[3]
with open(cmds_file) as f:
    commands = [l.rstrip("\n") for l in f if l.strip() != ""]

master, slave = pty.openpty()
cmd = [
    "./third_party/qemu-zhuowei/aarch64-softmmu/qemu-system-aarch64",
    "-M", "raspi3", "-m", "1024", "-display", "none",
    "-serial", "null", "-serial", "stdio",
    "-kernel", "build/xnu-latest-build/out/kernel.development.bcm2837-7195.unstripped",
    "-dtb", "devicetrees/rpi3.dtb",
    "-drive", "file=qemu/hfs_root_test.img,if=sd,format=raw",
    "-monitor", f"unix:{mon_sock},server,nowait",
    "-append", "debug=0x8 kextlog=0xfff -noprogress -v rd=disk0s2 serial=3",
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
last_prompt_at = 0

def buf_tail_has_prompt():
    # crude: prompt ends in "% " right at current end of buffer
    tail = buf[-6:]
    return tail.endswith(b"% ")

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
        time.sleep(1.2)

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
