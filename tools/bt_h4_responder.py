#!/usr/bin/env python3
"""A fake BCM43430A1 on the other end of QEMU's PL011, for testing
BCM43430Bluetooth without a radio (docs/bluetooth-plan.md, Testing).

QEMU wires the PL011 to serial_hd(0), which runvm.sh sets from BT_SERIAL.
This script listens on a Unix socket, QEMU connects to it as a client:

    tools/bt_h4_responder.py qemu/bt.sock &
    BT_SERIAL=unix:qemu/bt.sock EXTRA_BOOT_ARGS=bt=1 ./runvm.sh

It answers H4 commands the way the controller does on the bring-up path
(Reset, Read_Local_Version with LMP subversion 0x2209, Download_Minidriver,
every Write_RAM / Launch_RAM record, Update_UART_Baud_Rate, Read_BD_ADDR
with the chip default AA:AA:AA:AA:AA:AA, Read_Local_Name, event masks) and
gives the scan path something to find: one Extended Inquiry Result plus
Inquiry Complete after HCI_Inquiry, and one LE Advertising Report after
LE_Set_Scan_Enable. Anything else gets a Command Complete with status 0.

It tests framing, parsing and the user client, not timing: QEMU's PL011
ignores the baud rate. A summary line per notable command goes to stdout.
"""
import os
import socket
import struct
import sys
import threading
import time

NAME = b"BCM43430A1 (h4 responder)"
FAKE_CLASSIC = bytes([0x11, 0x22, 0x33, 0x44, 0x55, 0x66])   # display order
FAKE_LE = bytes([0xc0, 0xff, 0xee, 0x00, 0x00, 0x01])


def le_addr(a):
    return bytes(reversed(a))


def cc(op, params=b"\x00"):
    body = bytes([1]) + struct.pack("<H", op) + params
    return bytes([0x04, 0x0e, len(body)]) + body


def cs(op, status=0):
    body = bytes([status, 1]) + struct.pack("<H", op)
    return bytes([0x04, 0x0f, len(body)]) + body


def event(code, params):
    return bytes([0x04, code, len(params)]) + params


def eir_name(name):
    return bytes([len(name) + 1, 0x09]) + name


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "qemu/bt.sock"
    if os.path.exists(path):
        os.unlink(path)
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(path)
    srv.listen(1)
    print(f"bt_h4_responder: listening on {path}", flush=True)
    conn, _ = srv.accept()
    print("bt_h4_responder: QEMU connected", flush=True)
    lock = threading.Lock()

    def send(b):
        with lock:
            conn.sendall(b)

    def later(delay, b):
        def run():
            time.sleep(delay)
            try:
                send(b)
            except OSError:
                pass
        threading.Thread(target=run, daemon=True).start()

    buf = b""
    write_ram = 0
    while True:
        try:
            data = conn.recv(4096)
        except OSError:
            break
        if not data:
            break
        buf += data
        while buf:
            if buf[0] != 0x01:
                print(f"bt_h4_responder: unexpected byte {buf[0]:#x}, skipping", flush=True)
                buf = buf[1:]
                continue
            if len(buf) < 4:
                break
            op, plen = struct.unpack("<HB", buf[1:4])
            if len(buf) < 4 + plen:
                break
            p = buf[4:4 + plen]
            buf = buf[4 + plen:]

            if op == 0x1001:     # Read_Local_Version
                send(cc(op, bytes([0, 0x06]) + struct.pack("<HBHH", 0x00ff, 0x06, 0x000f, 0x2209)))
                print("bt_h4_responder: Read_Local_Version", flush=True)
            elif op == 0x1009:   # Read_BD_ADDR
                send(cc(op, b"\x00" + b"\xaa" * 6))
            elif op == 0x0c14:   # Read_Local_Name
                send(cc(op, b"\x00" + NAME.ljust(248, b"\x00")))
            elif op == 0xfc4c:   # Write_RAM
                write_ram += 1
                send(cc(op))
            elif op == 0xfc4e:   # Launch_RAM
                send(cc(op))
                print(f"bt_h4_responder: patch: {write_ram} Write_RAM + Launch_RAM", flush=True)
            elif op == 0xfc18:   # Update_UART_Baud_Rate
                baud = struct.unpack("<I", p[2:6])[0] if plen >= 6 else 0
                send(cc(op))
                print(f"bt_h4_responder: Update_UART_Baud_Rate {baud}", flush=True)
            elif op == 0xfc01:   # Write_BD_ADDR
                send(cc(op))
                print("bt_h4_responder: Write_BD_ADDR " + ":".join(f"{b:02x}" for b in reversed(p[:6])), flush=True)
            elif op == 0x0401:   # HCI_Inquiry: Command Status, then results
                send(cs(op))
                eir = eir_name(b"Fake Classic").ljust(240, b"\x00")
                res = bytes([1]) + le_addr(FAKE_CLASSIC) + bytes([1, 0]) + b"\x0c\x02\x5a" + b"\x00\x00" + bytes([0xc4]) + eir
                later(0.5, event(0x2f, res))
                later(1.0, event(0x01, b"\x00"))
                print("bt_h4_responder: inquiry", flush=True)
            elif op == 0x200c:   # LE_Set_Scan_Enable
                send(cc(op))
                if plen >= 1 and p[0] == 1:
                    ad = eir_name(b"Fake LE")
                    rep = bytes([0x02, 1, 0x00, 0x00]) + le_addr(FAKE_LE) + bytes([len(ad)]) + ad + bytes([0xc8])
                    later(0.3, event(0x3e, rep))
                    print("bt_h4_responder: LE scan on", flush=True)
            else:
                send(cc(op))
                if op in (0x0c03, 0xfc2e, 0x0c01, 0x2001):
                    print(f"bt_h4_responder: {op:#06x}", flush=True)
    print("bt_h4_responder: QEMU disconnected", flush=True)


if __name__ == "__main__":
    main()
