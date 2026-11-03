#!/usr/bin/env python3
"""A fake BCM43430A1 on the other end of QEMU's PL011, for testing
BCM43430Bluetooth without a radio (docs/bluetooth-plan.md, Testing).

QEMU wires the PL011 to serial_hd(0), which runvm.sh sets from BT_SERIAL.
This script listens on a Unix socket, QEMU connects to it as a client:

    tools/bt_h4_responder.py qemu/bt.sock &
    BT_SERIAL=unix:qemu/bt.sock EXTRA_BOOT_ARGS=bt=1 ./runvm.sh

It answers H4 commands the way the controller does on the bring-up path, and
netbt's unit init after `btconfig bcmbt0 up` (Read_Buffer_Size, the feature
pages, Read_Local_Supported_Commands; docs/bluetooth-stack-plan.md S2)
(Reset, Read_Local_Version with LMP subversion 0x2209, Download_Minidriver,
every Write_RAM / Launch_RAM record, Update_UART_Baud_Rate, Read_BD_ADDR
with the chip default AA:AA:AA:AA:AA:AA, Read_Local_Name, event masks) and
gives the scan path something to find: one Extended Inquiry Result plus
Inquiry Complete after HCI_Inquiry, and one LE Advertising Report after
LE_Set_Scan_Enable. Anything else gets a Command Complete with status 0.

It also plays a Bluetooth HID keyboard (FAKE_HID) for the HID host test
(tools/bt_hid_qemu_test.sh; docs/bluetooth-stack-plan.md S5). Once the
host turns on page scan (Write_Scan_Enable with bit 1, `btconfig bcmbt0
pscan`; again on each re-enable while it is not connected), the keyboard
reconnects as a real one does: Connection_Request,
then after Accept_Connection_Request a Connection_Complete, then L2CAP
channels to the HID control (0x11) and interrupt (0x13) PSMs with the
configuration exchange both ways, then boot-keyboard input reports
("h", "i" pressed and released) on the interrupt channel. Every ACL packet
from the host is acknowledged with Number_Of_Completed_Packets.

The keyboard pairs the legacy way (tools/bt_pair_qemu_test.sh): on
Authentication_Requested it asks the host for a link key; with none
(Link_Key_Request_Negative_Reply) it asks for a PIN and accepts only
HID_PIN, then issues a link key (Link_Key_Notification) and completes
authentication; later connections must present that key
(Link_Key_Request_Reply). This is what bthcid(8) answers. Once the host
enables Secure Simple Pairing (Write_Simple_Pairing_Mode 1, `btconfig
bcmbt0 ssp`), it pairs that way instead, as a keyboard with KeyboardOnly
IO capability: IO Capability Request/Response, then Passkey Entry -- the
host is told the passkey (User_Passkey_Notification, HID_PASSKEY), the
"user" types it on the keyboard, Simple Pairing Complete, an authenticated
link key. It encrypts the
link on Set_Connection_Encryption (btdevctl gives keyboards link mode
"encrypt").

It also serves SDP on PSM 1, enough for btdevctl(8)'s query
(usr.sbin/btdevctl/sdp.c cfg_search): ServiceSearchAttributeRequest finds
no PnP Information record and one HID record with the control PSM (0x11,
protocol descriptor list), interrupt PSM (0x13, additional list),
HIDReconnectInitiate true, and HIDDescriptorList holding KBD_DESC.

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
FAKE_HID = bytes([0x00, 0x1f, 0x20, 0x4b, 0x1d, 0x01])     # display order
HID_HANDLE = 0x0001
HID_PIN = b"1234"
HID_PASSKEY = 123456
CTL_CID, INT_CID, SDP_CID = 0x0040, 0x0041, 0x0042         # the keyboard's channel ids
# the boot keyboard report descriptor (HID 1.11 appendix B.1), as in
# tools/userland_staging/hid_userdevice_test.c
KBD_DESC = bytes([0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00,
                  0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
                  0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65,
                  0x81, 0x00, 0xc0])


# SDP data elements (Bluetooth Core, SDP: type << 3 | size index; big endian)
def de_u8(v): return bytes([0x08, v])
def de_u16(v): return bytes([0x09]) + struct.pack(">H", v)
def de_uuid16(v): return bytes([0x19]) + struct.pack(">H", v)
def de_bool(v): return bytes([0x28, 1 if v else 0])
def de_str(b): return bytes([0x25, len(b)]) + b
def de_seq(*items):
    body = b"".join(items)
    return (bytes([0x35, len(body)]) if len(body) < 256 else bytes([0x36]) + struct.pack(">H", len(body))) + body


HID_RECORD = de_seq(
    de_u16(0x0000), bytes([0x0a]) + struct.pack(">I", 0x00010001),          # ServiceRecordHandle
    de_u16(0x0001), de_seq(de_uuid16(0x1124)),                               # ServiceClassIDList: HID
    de_u16(0x0004), de_seq(de_seq(de_uuid16(0x0100), de_u16(0x0011)),       # L2CAP, control PSM
                           de_seq(de_uuid16(0x0011))),                       # HIDP
    de_u16(0x000d), de_seq(de_seq(de_seq(de_uuid16(0x0100), de_u16(0x0013)),  # interrupt PSM
                                  de_seq(de_uuid16(0x0011)))),
    de_u16(0x0205), de_bool(True),                                           # HIDReconnectInitiate
    de_u16(0x0206), de_seq(de_seq(de_u8(0x22), de_str(KBD_DESC))),           # HIDDescriptorList
)


def acl(cid, payload, handle=HID_HANDLE):
    l2 = struct.pack("<HH", len(payload), cid) + payload
    return bytes([0x02]) + struct.pack("<HH", handle | (0x2 << 12), len(l2)) + l2


def sig(code, ident, data):
    return acl(0x0001, bytes([code, ident]) + struct.pack("<H", len(data)) + data)


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
    scan = [0]
    pairing = {"key": None, "host_ssp": False}  # link key issued once paired; SSP on?
    hid = {"paging": False, "connected": False, "host_cid": {}, "cfg_done": set(), "reported": False}

    def hid_reports():
        # boot keyboard reports {modifiers, 0, key...}: h (0x0b), i (0x0c),
        # once a second for as long as the interrupt channel stays open, so
        # a test can open its IOHIDEventService client at any point.
        sent = 0
        time.sleep(2)
        while hid["connected"] and INT_CID in hid["host_cid"] and sent < 240:
            for key in (0x0b, 0x0c):
                for rep in (bytes([0, 0, key, 0, 0, 0, 0, 0]), bytes(8)):
                    try:
                        send(acl(hid["host_cid"][INT_CID], bytes([0xa1]) + rep))
                    except (OSError, KeyError):
                        return
                    sent += 1
                    time.sleep(0.05)
            time.sleep(1)
        print(f"bt_h4_responder: HID: sent {sent} input reports", flush=True)

    def hid_sdp(payload):
        """An SDP PDU from the host on the keyboard's SDP channel."""
        if len(payload) < 5 or SDP_CID not in hid["host_cid"]:
            return
        pdu, tid = payload[0], payload[1:3]
        if pdu != 0x06:                         # only ServiceSearchAttributeRequest
            body = struct.pack(">H", 0x0003)    # Invalid request syntax
            send(acl(hid["host_cid"][SDP_CID], bytes([0x01]) + tid + struct.pack(">H", len(body)) + body))
            return
        wants_hid = b"\x19\x11\x24" in payload[5:]  # ServiceSearchPattern has UUID 0x1124
        lists = de_seq(HID_RECORD) if wants_hid else de_seq()
        body = struct.pack(">H", len(lists)) + lists + b"\x00"   # no continuation
        send(acl(hid["host_cid"][SDP_CID], bytes([0x07]) + tid + struct.pack(">H", len(body)) + body))
        print(f"bt_h4_responder: HID: SDP search -> {'HID record' if wants_hid else 'no records'}", flush=True)

    def hid_l2cap(payload):
        """L2CAP from the host to the fake keyboard (basic header stripped)."""
        if len(payload) < 4:
            return
        code, ident, dlen = payload[0], payload[1], struct.unpack("<H", payload[2:4])[0]
        d = payload[4:4 + dlen]
        names = {0x01: "CommandReject", 0x02: "ConnReq", 0x03: "ConnRsp", 0x04: "ConfReq", 0x05: "ConfRsp",
                 0x06: "DisconnReq", 0x07: "DisconnRsp", 0x0a: "InfoReq"}
        print(f"bt_h4_responder: HID: host L2CAP {names.get(code, hex(code))} id {ident} {d.hex()}", flush=True)
        if code == 0x03 and len(d) >= 8:      # Connection Response {dcid, scid, result, status}
            dcid, scid, result = struct.unpack("<HHH", d[:6])
            if result == 0:
                hid["host_cid"][scid] = dcid
                # our Configure Request to the host's channel: no options
                send(sig(0x04, 0x10 + scid - CTL_CID, struct.pack("<HH", dcid, 0)))
                print(f"bt_h4_responder: HID: channel 0x{scid:04x} -> host cid 0x{dcid:04x}", flush=True)
        elif code == 0x04 and len(d) >= 4:    # Configure Request {dcid (ours), flags, options}
            ours = struct.unpack("<H", d[:2])[0]
            host = hid["host_cid"].get(ours, 0)
            send(sig(0x05, ident, struct.pack("<HHH", host, 0, 0)))   # success, no options
            hid["cfg_done"].add(ours)
            if ours == CTL_CID and INT_CID not in hid["host_cid"] and hid.get("we_opened"):
                send(sig(0x02, 2, struct.pack("<HH", 0x0013, INT_CID)))  # open interrupt
            if ours == INT_CID and not hid["reported"]:
                hid["reported"] = True
                print("bt_h4_responder: HID: both channels open", flush=True)
                threading.Thread(target=hid_reports, daemon=True).start()
        elif code == 0x02 and len(d) >= 4:    # Connection Request from the host {psm, scid}
            psm, hcid = struct.unpack("<HH", d[:4])
            ours = {0x0011: CTL_CID, 0x0013: INT_CID, 0x0001: SDP_CID}.get(psm)
            if ours is None:
                send(sig(0x03, ident, struct.pack("<HHHH", 0, hcid, 2, 0)))       # PSM not supported
                return
            hid["host_cid"][ours] = hcid
            send(sig(0x03, ident, struct.pack("<HHHH", ours, hcid, 0, 0)))        # success
            send(sig(0x04, 0x20 + ours - CTL_CID, struct.pack("<HH", hcid, 0)))   # our Configure Request
            print(f"bt_h4_responder: HID: host opened PSM 0x{psm:02x} (host cid 0x{hcid:04x})", flush=True)
        elif code == 0x06 and len(d) >= 4:    # Disconnection Request {dcid (ours), scid}
            send(sig(0x07, ident, d[:4]))
            ours = struct.unpack("<H", d[:2])[0]
            hid["host_cid"].pop(ours, None)
            hid["cfg_done"].discard(ours)
            if ours == INT_CID:
                hid["reported"] = False
        elif code == 0x0a and len(d) >= 2:    # Information Request: not supported
            send(sig(0x0b, ident, d[:2] + struct.pack("<H", 1)))
    bdaddr = b"\xaa" * 6        # wire order; Write_BD_ADDR changes it and HCI_Reset keeps it,
                                # as the real chip does (Linux sets it before its init reset)
    while True:
        try:
            data = conn.recv(4096)
        except OSError:
            break
        if not data:
            break
        buf += data
        while buf:
            if buf[0] == 0x02:                # ACL from the host
                if len(buf) < 5:
                    break
                hf, alen = struct.unpack("<HH", buf[1:5])
                if len(buf) < 5 + alen:
                    break
                pkt = buf[5:5 + alen]
                buf = buf[5 + alen:]
                send(event(0x13, bytes([1]) + struct.pack("<HH", hf & 0x0fff, 1)))
                if len(pkt) >= 4:
                    llen, cid = struct.unpack("<HH", pkt[:4])
                    if cid == 0x0001:
                        hid_l2cap(pkt[4:4 + llen])
                    elif cid == SDP_CID:
                        hid_sdp(pkt[4:4 + llen])
                continue
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
                send(cc(op, b"\x00" + bdaddr))
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
                bdaddr = bytes(p[:6])
                send(cc(op))
                print("bt_h4_responder: Write_BD_ADDR " + ":".join(f"{b:02x}" for b in reversed(p[:6])), flush=True)
            elif op == 0x0401:   # HCI_Inquiry: Command Status, then results
                send(cs(op))
                eir = eir_name(b"Fake Classic").ljust(240, b"\x00")
                res = bytes([1]) + le_addr(FAKE_CLASSIC) + bytes([1, 0]) + b"\x0c\x02\x5a" + b"\x00\x00" + bytes([0xc4]) + eir
                later(0.5, event(0x2f, res))
                later(1.0, event(0x01, b"\x00"))
                print("bt_h4_responder: inquiry", flush=True)
            elif op == 0x1005:   # Read_Buffer_Size (netbt init, hci_event.c hci_cmd_read_buffer_size)
                send(cc(op, b"\x00" + struct.pack("<HBHH", 1021, 64, 8, 1)))
            elif op == 0x1003:   # Read_Local_Features: a BT 4.x set, page 1 present (byte 7 bit 7)
                send(cc(op, b"\x00" + bytes([0xbf, 0xfe, 0xcf, 0xfe, 0xdb, 0xff, 0x7b, 0x87])))
            elif op == 0x1004:   # Read_Local_Extended_Features {page}: max page 1
                page = p[0] if plen >= 1 else 0
                feat = bytes([0x01, 0, 0, 0, 0, 0, 0, 0]) if page == 1 else bytes(8)
                send(cc(op, bytes([0, page, 1]) + feat))
            elif op == 0x1002:   # Read_Local_Supported_Commands: 64 bytes
                send(cc(op, b"\x00" + b"\xff" * 64))
                print("bt_h4_responder: Read_Local_Supported_Commands (netbt init)", flush=True)
            elif op == 0x0c19:   # Read_Scan_Enable
                send(cc(op, bytes([0, scan[0]])))
            elif op == 0x0c1a:   # Write_Scan_Enable: page scan on -> the keyboard reconnects
                scan[0] = p[0] if plen else 0
                send(cc(op))
                print(f"bt_h4_responder: Write_Scan_Enable {p[0] if plen else 0:#x}", flush=True)
                if plen >= 1 and (p[0] & 2) and not hid["connected"] and not hid["paging"]:
                    hid["paging"] = True
                    later(2.0, event(0x04, le_addr(FAKE_HID) + bytes([0x40, 0x25, 0x00, 0x01])))
                    print("bt_h4_responder: HID: keyboard paging the host", flush=True)
            elif op == 0x0409:   # Accept_Connection_Request -> Connection_Complete, open control
                send(cs(op))
                hid.update(paging=False, connected=True, host_cid={}, cfg_done=set(), reported=False)
                send(event(0x03, bytes([0]) + struct.pack("<H", HID_HANDLE) + le_addr(FAKE_HID) + bytes([1, 0])))
                hid["we_opened"] = True
                send(sig(0x02, 1, struct.pack("<HH", 0x0011, CTL_CID)))
                print("bt_h4_responder: HID: connected, opening control channel", flush=True)
            elif op == 0x0405:   # Create_Connection {bdaddr, ...}: only the keyboard answers
                send(cs(op))
                if bytes(p[:6]) == le_addr(FAKE_HID) and not hid["connected"]:
                    hid.update(paging=False, connected=True, host_cid={}, cfg_done=set(), reported=False, we_opened=False)
                    send(event(0x03, bytes([0]) + struct.pack("<H", HID_HANDLE) + le_addr(FAKE_HID) + bytes([1, 0])))
                    print("bt_h4_responder: HID: host connected to the keyboard", flush=True)
                else:
                    send(event(0x03, bytes([0x04]) + struct.pack("<H", 0) + bytes(p[:6]) + bytes([1, 0])))  # page timeout
            elif op == 0x0411:   # Authentication_Requested {handle}
                send(cs(op))
                send(event(0x17, le_addr(FAKE_HID)))            # Link_Key_Request
                print("bt_h4_responder: HID: authentication requested, asking for a link key", flush=True)
            elif op == 0x040b:   # Link_Key_Request_Reply {bdaddr, key[16]}
                send(cc(op, b"\x00" + bytes(p[:6])))
                ok = pairing["key"] is not None and bytes(p[6:22]) == pairing["key"]
                send(event(0x06, bytes([0 if ok else 0x06]) + struct.pack("<H", HID_HANDLE)))
                print(f"bt_h4_responder: HID: host presented a link key: {'accepted' if ok else 'REJECTED'}", flush=True)
            elif op == 0x040c:   # Link_Key_Request_Negative_Reply: no key -> SSP or PIN
                send(cc(op, b"\x00" + bytes(p[:6])))
                if pairing["host_ssp"]:
                    send(event(0x31, le_addr(FAKE_HID)))        # IO_Capability_Request
                    print("bt_h4_responder: HID: host has no key, secure simple pairing", flush=True)
                else:
                    send(event(0x16, le_addr(FAKE_HID)))        # PIN_Code_Request
                    print("bt_h4_responder: HID: host has no key, asking for a PIN", flush=True)
            elif op == 0x0c56:   # Write_Simple_Pairing_Mode
                pairing["host_ssp"] = plen >= 1 and p[0] == 1
                send(cc(op))
                print(f"bt_h4_responder: Write_Simple_Pairing_Mode {p[0] if plen else 0}", flush=True)
            elif op == 0x042b:   # IO_Capability_Request_Reply {bdaddr, io_cap, oob, auth}
                send(cc(op, b"\x00" + bytes(p[:6])))
                print(f"bt_h4_responder: HID: host IO capability {p[6]}, auth {p[8]:#x}", flush=True)
                send(event(0x32, le_addr(FAKE_HID) + bytes([0x02, 0x00, 0x05])))   # KeyboardOnly, MITM
                if p[6] in (0x00, 0x01, 0x04):   # host can display: Passkey Entry on the keyboard
                    send(event(0x3b, le_addr(FAKE_HID) + struct.pack("<I", HID_PASSKEY)))
                    import hashlib
                    pairing["key"] = hashlib.sha256(b"fake hid ssp key").digest()[:16]
                    later(1.0, event(0x36, bytes([0]) + le_addr(FAKE_HID)))       # Simple_Pairing_Complete
                    later(1.1, event(0x18, le_addr(FAKE_HID) + pairing["key"] + bytes([0x05])))
                    later(1.2, event(0x06, bytes([0]) + struct.pack("<H", HID_HANDLE)))
                    print(f"bt_h4_responder: HID: passkey {HID_PASSKEY:06d} shown to the host, typed, paired (SSP)", flush=True)
                else:
                    send(event(0x36, bytes([0x05]) + le_addr(FAKE_HID)))
                    send(event(0x06, bytes([0x05]) + struct.pack("<H", HID_HANDLE)))
            elif op == 0x0434:   # IO_Capability_Request_Negative_Reply {bdaddr, reason}
                send(cc(op, b"\x00" + bytes(p[:6])))
                send(event(0x36, bytes([p[6] if plen > 6 else 0x18]) + le_addr(FAKE_HID)))
                send(event(0x06, bytes([0x18]) + struct.pack("<H", HID_HANDLE)))
                print("bt_h4_responder: HID: host refused to pair (SSP)", flush=True)
            elif op == 0x040d:   # PIN_Code_Request_Reply {bdaddr, len, pin[16]}
                send(cc(op, b"\x00" + bytes(p[:6])))
                pin = bytes(p[7:7 + p[6]]) if plen >= 7 else b""
                if pin == HID_PIN:
                    import hashlib
                    pairing["key"] = hashlib.sha256(b"fake hid link key" + pin).digest()[:16]
                    send(event(0x18, le_addr(FAKE_HID) + pairing["key"] + bytes([0x00])))   # Link_Key_Notification
                    send(event(0x06, bytes([0]) + struct.pack("<H", HID_HANDLE)))
                    print("bt_h4_responder: HID: PIN accepted, paired", flush=True)
                else:
                    send(event(0x06, bytes([0x05]) + struct.pack("<H", HID_HANDLE)))
                    print(f"bt_h4_responder: HID: wrong PIN {pin!r}", flush=True)
            elif op == 0x040e:   # PIN_Code_Request_Negative_Reply
                send(cc(op, b"\x00" + bytes(p[:6])))
                send(event(0x06, bytes([0x18]) + struct.pack("<H", HID_HANDLE)))     # pairing not allowed
                print("bt_h4_responder: HID: host refused to give a PIN", flush=True)
            elif op == 0x0413:   # Set_Connection_Encryption {handle, enable}
                send(cs(op))
                send(event(0x08, bytes([0]) + p[:2] + bytes([p[2] if plen >= 3 else 1])))   # Encryption_Change
                print(f"bt_h4_responder: HID: encryption {'on' if plen >= 3 and p[2] else 'off'}", flush=True)
            elif op == 0x1408:   # Read_Encryption_Key_Size {handle}: netbt waits for >= 7
                send(cc(op, bytes([0]) + p[:2] + bytes([16])))     # (hci_event.c hci_cmd_read_encryption_key_size)
            elif op == 0x0406:   # Disconnect {handle, reason}
                send(cs(op))
                send(event(0x05, bytes([0]) + p[:2] + bytes([0x16])))
                hid.update(paging=False, connected=False)
                print(f"bt_h4_responder: HID: host disconnected handle {struct.unpack('<H', p[:2])[0]:#x}", flush=True)
            elif op == 0x040a:   # Reject_Connection_Request
                send(cs(op))
                hid.update(paging=False, connected=False)
                print("bt_h4_responder: HID: host rejected the keyboard", flush=True)
            elif op == 0x041f:   # Read_Clock_Offset: Command Status, then the event
                send(cs(op))
                send(event(0x1c, bytes([0]) + p[:2] + b"\x00\x00"))
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
