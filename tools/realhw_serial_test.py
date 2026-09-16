#!/usr/bin/env python3
"""Drive the real Pi 3's serial console non-interactively, like boot_test.py does for QEMU.

WHY THIS EXISTS
===============
Real-hardware verification in this project had no scripted path at all: every
check meant a human sitting in `minicom -b 115200 -D /dev/tty.usbserial-0001`
typing commands and pasting output back. That is the slowest loop in the
project (far slower than a QEMU boot, which tools/boot_test.py already
automates), and it silently discourages the "run one more real check to
confirm" step that this codebase's own history keeps proving necessary.

This is the real-hardware counterpart to tools/boot_test.py: same --cmd
interface, same "send a command, wait for the prompt, capture everything"
shape. It does NOT boot or flash anything -- it attaches to a board that is
already running and sitting at a shell prompt.

THE CLOCAL TRAP (this cost a real hour, do not re-derive it)
============================================================
Opening a macOS USB-serial tty *blocks indefinitely* waiting for carrier
detect (DCD) unless CLOCAL is set -- and the natural fixes both fail:

  - `stty -f /dev/tty.usbserial-0001 clocal` must itself open the port to
    set the flag, so it hangs on the very thing it is trying to fix.
  - `printf '\r' > /dev/tty.usbserial-0001` hangs for the same reason, in a
    state that ignores SIGTERM (only SIGKILL clears it).

The only ordering that works is the one below: open with O_NONBLOCK (which
skips the DCD wait), set CLOCAL via termios on the now-open fd, then clear
O_NONBLOCK for normal blocking reads. `/dev/cu.*` would also avoid the DCD
wait, but `/dev/tty.*` is what this project's own docs and the user's
minicom invocation reference, so this keeps that path working.

USAGE
=====
    tools/realhw_serial_test.py --cmd 'ifconfig en0' --cmd 'ipconfig getpacket en0'
    tools/realhw_serial_test.py --log /tmp/pi.log --cmd 'mount'
    tools/realhw_serial_test.py --raw --timeout 30      # just capture output

The port must be FREE -- quit any attached minicom/screen first, or this
exits with a clear error rather than fighting over the fd (two readers on one
tty silently split the byte stream between them, which looks exactly like the
board dropping output).
"""
import argparse
import os
import select
import sys
import termios
import time

DEFAULT_PORT = "/dev/tty.usbserial-0001"
PROMPT_CANDIDATES = ("-sh-3.2# ", "-bash-3.2# ")


def open_port(path, baud):
    """Open a USB-serial tty without hanging on carrier detect. See header."""
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)

    attrs = termios.tcgetattr(fd)
    iflag, oflag, cflag, lflag, ispeed, ospeed, cc = attrs

    # Raw mode: no canonical line editing, no echo, no signal generation, no
    # CR/LF translation in either direction, no flow control. Anything less
    # mangles a console stream that carries real \r\n and real control bytes.
    iflag &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK |
               termios.ISTRIP | termios.INLCR | termios.IGNCR |
               termios.ICRNL | termios.IXON)
    oflag &= ~termios.OPOST
    lflag &= ~(termios.ECHO | termios.ECHONL | termios.ICANON |
               termios.ISIG | termios.IEXTEN)
    cflag &= ~(termios.CSIZE | termios.PARENB | termios.CSTOPB | termios.CRTSCTS)
    cflag |= termios.CS8
    # CLOCAL is the load-bearing flag: "ignore modem control lines". Without
    # it every read/write waits on a DCD line the Pi's 3-wire cable does not
    # drive. CREAD enables the receiver.
    cflag |= termios.CLOCAL | termios.CREAD

    cc[termios.VMIN] = 0
    cc[termios.VTIME] = 0

    termios.tcsetattr(fd, termios.TCSANOW,
                      [iflag, oflag, cflag, lflag, baud, baud, cc])

    # Back to blocking semantics for select()-driven reads.
    os.set_blocking(fd, True)
    return fd


def send(fd, text, char_delay):
    """Type `text` one byte at a time.

    XNU's serial INPUT path on this target is POLLED (serial=3 ->
    SERIALMODE_INPUT, osfmk/console/serial_general.c), and the cable has no
    flow control, so a whole line written in one os.write() at 115200
    overruns it and characters are silently dropped -- the observed symptom
    is the board echoing back a truncated prefix of what was sent ("echo MAR"
    for "echo MARKER_ALIVE"). A human typing in minicom never triggers this
    because keystroke timing is orders of magnitude slower. Hence the
    deliberate per-character delay; do not "optimize" this into one write.
    """
    for byte in text.encode():
        os.write(fd, bytes([byte]))
        time.sleep(char_delay)


def drain(fd, seconds, sink, stop_on_prompt=False):
    """Read for `seconds`, echoing to stdout and appending to `sink`.

    `sink` MUST be a list (mutable) that this appends chunks to -- an earlier
    version took a str and did `sink += text`, which only rebinds the local
    name because Python strings are immutable, so the caller's transcript
    stayed empty and --log wrote a 0-byte file.
    """
    deadline = time.time() + seconds
    while time.time() < deadline:
        remaining = max(0.0, deadline - time.time())
        r, _, _ = select.select([fd], [], [], min(0.4, remaining))
        tail = "".join(sink[-8:])
        if not r:
            if stop_on_prompt and any(tail.rstrip(" ").endswith(p.rstrip(" "))
                                      for p in PROMPT_CANDIDATES):
                return True
            continue
        try:
            chunk = os.read(fd, 4096)
        except OSError:
            break
        if not chunk:
            continue
        text = chunk.decode("utf-8", errors="replace")
        sys.stdout.write(text)
        sys.stdout.flush()
        sink.append(text)
        tail = "".join(sink[-8:])
        if stop_on_prompt and any(tail.rstrip(" ").endswith(p.rstrip(" "))
                                  for p in PROMPT_CANDIDATES):
            return True
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=DEFAULT_PORT)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--cmd", action="append", default=[],
                    help="shell command to send; repeatable, sent in order")
    ap.add_argument("--settle", type=float, default=12.0,
                    help="seconds to wait for each command's output")
    ap.add_argument("--timeout", type=float, default=20.0,
                    help="--raw capture duration")
    ap.add_argument("--raw", action="store_true",
                    help="send nothing, just capture output")
    ap.add_argument("--log", default=None)
    ap.add_argument("--char-delay", type=float, default=0.012,
                    help="seconds between typed characters; see send() on why "
                         "this cannot be 0 for this target")
    args = ap.parse_args()

    if not os.path.exists(args.port):
        sys.exit(f"error: {args.port} does not exist (board unplugged?)")

    # Refuse to share the tty. See header on why two readers is worse than
    # an error: the byte stream gets split, not duplicated.
    if os.system(f"lsof {args.port} >/dev/null 2>&1") == 0:
        sys.exit(f"error: {args.port} is already open by another process "
                 f"(quit minicom/screen first, then re-run)")

    fd = open_port(args.port, args.baud)
    transcript = []
    try:
        if args.raw:
            drain(fd, args.timeout, transcript)
        else:
            # Wake the shell and establish where the prompt is.
            send(fd, "\r", args.char_delay)
            drain(fd, 3.0, transcript, stop_on_prompt=True)
            for cmd in args.cmd:
                print(f"\n[realhw] === {cmd} ===", flush=True)
                send(fd, cmd + "\r", args.char_delay)
                got_prompt = drain(fd, args.settle, transcript,
                                   stop_on_prompt=True)
                if not got_prompt:
                    print(f"\n[realhw] (no prompt within {args.settle}s -- "
                          f"command may still be running)", flush=True)
    finally:
        os.close(fd)
        if args.log:
            with open(args.log, "w") as f:
                f.write("".join(transcript))
            print(f"\n[realhw] transcript: {args.log} "
                  f"({sum(len(c) for c in transcript)} chars)", flush=True)


if __name__ == "__main__":
    main()
