import struct, sys

KERNEL_PATH = "/Users/anurodhp/Development/iokit/precompiled/kernel.development.bcm2837.unstripped"
LC_SYMTAB = 0x2

data = open(KERNEL_PATH, "rb").read()
magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved = struct.unpack_from('<8I', data, 0)
off = 32
symoff = nsyms = stroff = strsize = None
for _ in range(ncmds):
    cmd, cmdsize = struct.unpack_from('<2I', data, off)
    if cmd == LC_SYMTAB:
        symoff, nsyms, stroff, strsize = struct.unpack_from('<4I', data, off+8)
    off += cmdsize

print(f"symtab: symoff={symoff:#x} nsyms={nsyms} stroff={stroff:#x} strsize={strsize:#x}")

syms = []
for i in range(nsyms):
    base = symoff + i*16
    n_strx, n_type, n_sect, n_desc, n_value = struct.unpack_from('<IBBHQ', data, base)
    if n_value == 0:
        continue
    name_end = data.index(b'\0', stroff + n_strx)
    name = data[stroff+n_strx:name_end].decode('ascii', 'replace')
    syms.append((n_value, name))
syms.sort()

targets = [int(x, 16) for x in sys.argv[1:]]
addrs = [a for a, _ in syms]
import bisect
for t in targets:
    idx = bisect.bisect_right(addrs, t) - 1
    if idx >= 0:
        addr, name = syms[idx]
        print(f"{t:#x} -> {name} + {t-addr:#x}")
    else:
        print(f"{t:#x} -> no symbol found")
