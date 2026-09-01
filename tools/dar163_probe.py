#!/usr/bin/env python3
"""DAR-163 standalone probe: boots the kernel with 'pdtrace' in boot-args
(to enable core.c's pd_core_phase() tracing), captures console output to
qemu/dar163_probe.log until QUIET_SECONDS of silence or MAX_SECONDS total,
then tears QEMU down and releases the lock -- unlike tools/boot_test.py,
this has no --expect/--expect-fail gating, so it's useful for open-ended
"just show me what happens" boots.

Set DAR163_PROBE_KEEP_ALIVE=1 to leave QEMU running after the quiet
period instead (for live `qemu/mon_probe.sock` monitor queries) -- the
lock is deliberately NOT released in that mode, since QEMU is still
running; kill the process and rmdir qemu/.qemu_test_lock yourself when
done.

Mirrors runvm.sh's real args (qemu-zhuowei fork, cpu-factor=10, serial=3)
plus the project's mkdir-based qemu/.qemu_test_lock (NOT boot_test.py's
internal lock -- this is a separate standalone script, so it must take
the lock itself, per this project's standing single-QEMU-at-a-time rule).

Usage: tools/dar163_probe.py [quiet_seconds]
"""
import os
import pty
import select
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOCK = os.path.join(REPO, "qemu", ".qemu_test_lock")
LOG = os.path.join(REPO, "qemu", "dar163_probe.log")
PIDFILE = os.path.join(REPO, "qemu", "dar163_probe.pid")

QUIET_SECONDS = float(sys.argv[1]) if len(sys.argv) > 1 else 20.0
MAX_SECONDS = 240.0


def acquire_lock(timeout=600):
    os.makedirs(os.path.dirname(LOCK), exist_ok=True)
    deadline = time.time() + timeout
    while True:
        try:
            os.mkdir(LOCK)
            return
        except FileExistsError:
            if time.time() > deadline:
                sys.exit(f"error: {LOCK} held; waited {timeout}s")
            time.sleep(2)


def main():
    acquire_lock()
    print(f"[probe] lock acquired: {LOCK}")

    sd_image = os.environ.get("SD_IMAGE", "qemu/hfs_root_test.img")
    root_dev = os.environ.get("ROOT_DEV", "disk0s2")
    usb_devices = os.environ.get(
        "USB_DEVICES",
        "-device usb-hub,id=lan9514hub,bus=usb-bus.0,port=1 "
        "-netdev user,id=usbnet0 -device usb-net,netdev=usbnet0,bus=usb-bus.0,port=1.1",
    )

    cmd = [
        os.path.join(REPO, "third_party/qemu-zhuowei/aarch64-softmmu/qemu-system-aarch64"),
        "-M", "raspi3", "-m", "1024", "-display", "none",
        "-serial", "null", "-serial", "stdio",
        "-kernel", os.path.join(REPO, "build/xnu-latest-build/out/kernel.development.bcm2837-7195.unstripped"),
        "-dtb", os.path.join(REPO, "devicetrees/rpi3.dtb"),
        "-drive", f"file={os.path.join(REPO, sd_image)},if=sd,format=raw",
    ] + usb_devices.split() + [
        "-append", f"debug=0x8 kextlog=0xfff -noprogress -v -enable_kprintf_spam rd={root_dev} serial=3 cpu-factor=10 pdtrace",
        "-monitor", "unix:" + os.path.join(REPO, "qemu/mon_probe.sock") + ",server,nowait",
    ]

    master, slave = pty.openpty()
    proc = subprocess.Popen(cmd, stdin=slave, stdout=slave, stderr=slave,
                             cwd=REPO, close_fds=True, start_new_session=True)
    os.close(slave)
    with open(PIDFILE, "w") as f:
        f.write(str(proc.pid))
    print(f"[probe] qemu pid={proc.pid}, logging to {LOG}, monitor at qemu/mon_probe.sock")

    start = time.time()
    last_data = start
    with open(LOG, "wb") as logf:
        while True:
            now = time.time()
            if now - start > MAX_SECONDS:
                print(f"[probe] hit MAX_SECONDS={MAX_SECONDS}, stopping read (qemu still running)")
                break
            if now - last_data > QUIET_SECONDS:
                print(f"[probe] quiet for {QUIET_SECONDS}s, stopping read (qemu still running, pid={proc.pid})")
                break
            r, _, _ = select.select([master], [], [], 0.5)
            if not r:
                if proc.poll() is not None:
                    print("[probe] qemu exited on its own")
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
            last_data = now

    if os.environ.get("DAR163_PROBE_KEEP_ALIVE") == "1":
        print(f"[probe] leaving qemu pid={proc.pid} running (DAR163_PROBE_KEEP_ALIVE=1). "
              f"Query qemu/mon_probe.sock, then kill it and rmdir {LOCK} when done.")
        # NOTE: lock intentionally NOT released here -- qemu is still running.
        return
    print(f"[probe] killing qemu pid={proc.pid} and releasing lock")
    try:
        proc.kill()
        proc.wait(timeout=5)
    except Exception:
        pass
    try:
        os.rmdir(LOCK)
    except OSError:
        pass


if __name__ == "__main__":
    main()
