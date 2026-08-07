#!/usr/bin/env python3
"""Talk to a running QEMU's unix-socket monitor directly (no nc/sleep hacks)."""
import socket
import sys
import re
import struct
import os

SOCK_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../qemu/mon.sock")
KERNEL_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../precompiled/kernel.development.bcm2837.unstripped")


class Monitor:
    def __init__(self, path=SOCK_PATH):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(path)
        self.sock.settimeout(3)
        self._read_until_prompt()

    def _read_until_prompt(self):
        buf = b""
        try:
            while not buf.rstrip().endswith(b"(qemu)"):
                chunk = self.sock.recv(65536)
                if not chunk:
                    break
                buf += chunk
        except socket.timeout:
            pass
        return buf.decode("utf-8", "replace")

    def cmd(self, c):
        self.sock.sendall((c + "\n").encode())
        return self._read_until_prompt()

    def read_u64(self, vaddr):
        out = self.cmd(f"x/1xg {vaddr:#x}")
        m = re.search(r":\s+(0x[0-9a-f]+)", out)
        if not m:
            raise RuntimeError(f"couldn't parse x/1xg output: {out!r}")
        return int(m.group(1), 16)

    def read_bytes(self, vaddr, n):
        # read n bytes via repeated byte-granularity examine (small n only)
        out = self.cmd(f"x/{n}xb {vaddr:#x}")
        vals = re.findall(r"0x([0-9a-f]{2})\b", out)
        return bytes(int(v, 16) for v in vals[:n])

    def read_cstring(self, vaddr, maxlen=256):
        chunk = self.read_bytes(vaddr, min(maxlen, 64))
        if b"\x00" in chunk:
            return chunk.split(b"\x00", 1)[0]
        # extend if no null found in first chunk
        out = chunk
        remaining = maxlen - len(chunk)
        addr = vaddr + len(chunk)
        while remaining > 0 and b"\x00" not in out[-64:]:
            more = self.read_bytes(addr, min(64, remaining))
            if not more:
                break
            out += more
            addr += len(more)
            remaining -= len(more)
        return out.split(b"\x00", 1)[0]

    def registers(self):
        out = self.cmd("info registers")
        regs = {}
        for m in re.finditer(r"(X\d+|PC|SP|PSTATE)=([0-9a-f]+)", out):
            regs[m.group(1)] = int(m.group(2), 16)
        return regs

    def disas(self, vaddr, count=10):
        return self.cmd(f"x/{count}i {vaddr:#x}")


def load_symbols():
    data = open(KERNEL_PATH, "rb").read()
    magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved = \
        struct.unpack_from('<8I', data, 0)
    off = 32
    symoff = nsyms = stroff = strsize = None
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from('<2I', data, off)
        if cmd == 0x2:
            symoff, nsyms, stroff, strsize = struct.unpack_from('<4I', data, off + 8)
        off += cmdsize
    syms = []
    for i in range(nsyms):
        base = symoff + i * 16
        n_strx, n_type, n_sect, n_desc, n_value = struct.unpack_from('<IBBHQ', data, base)
        if n_value == 0:
            continue
        name_end = data.index(b'\0', stroff + n_strx)
        name = data[stroff + n_strx:name_end].decode('ascii', 'replace')
        syms.append((n_value, name))
    syms.sort()
    return syms


def resolve(syms, addr):
    import bisect
    addrs = [a for a, _ in syms]
    idx = bisect.bisect_right(addrs, addr) - 1
    if idx >= 0:
        a, name = syms[idx]
        return f"{name}+{addr-a:#x}"
    return "?"


if __name__ == "__main__":
    mon = Monitor()
    syms = load_symbols()
    regs = mon.registers()
    print("PC:", hex(regs.get("PC", 0)), resolve(syms, regs.get("PC", 0)))
    fp = regs.get("X29")
    print(f"Walking frame chain from X29={fp:#x}...")
    for depth in range(20):
        if not fp or fp < 0xfffffff000000000:
            print("  (fp out of kernel VA range, stopping)")
            break
        try:
            saved_fp = mon.read_u64(fp)
            saved_lr = mon.read_u64(fp + 8)
        except Exception as e:
            print("  (read failed:", e, ")")
            break
        print(f"  #{depth} fp={fp:#x} lr={saved_lr:#x} {resolve(syms, saved_lr)}")
        if saved_fp == 0 or saved_fp == fp:
            break
        fp = saved_fp
