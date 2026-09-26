#!/usr/bin/env python3
"""Converts an X Window Dump (xwd -root -out file.xwd, DAR-387) to PNG on the
host, with no image library: `tools/xwd_to_png.py in.xwd out.png`.

Handles what Xvfb/Xorg on this port produce: XWD version 7 (X11/XWDFile.h),
ZPixmap, TrueColor, 32 bits per pixel, either byte order. The header is 25
big-endian CARD32s, then the window name, then ncolors 12-byte XWDColor
entries, then the pixels. Prints the geometry and the most common colours,
so a boot test can be checked from the terminal too.
"""
import struct, zlib, sys
d = open(sys.argv[1], 'rb').read()
h = struct.unpack('>25I', d[:100])
(hsize, ver, fmt, depth, w, ht, xoff, byte_order, unit, bit_order, pad, bpp, bpl,
 vclass, rmask, gmask, bmask, bprgb, cmap_entries, ncolors, ww, wh, wx, wy, bw) = h
if ver != 7 or fmt != 2 or bpp != 32:
    sys.exit(f"unsupported xwd: version {ver}, format {fmt}, {bpp} bpp (want 7, ZPixmap=2, 32)")
print(f"xwd v{ver} {w}x{ht} depth {depth} bpp {bpp} bpl {bpl} byte_order {byte_order} masks {rmask:x}/{gmask:x}/{bmask:x} ncolors {ncolors}")
off = hsize + ncolors * 12
px = d[off:off + bpl * ht]
def sh(m): 
    s = 0
    while m and not (m & 1): m >>= 1; s += 1
    return s
rs, gs, bs = sh(rmask), sh(gmask), sh(bmask)
rows = []
counts = {}
for y in range(ht):
    row = bytearray([0])
    for x in range(w):
        o = y * bpl + x * 4
        v = int.from_bytes(px[o:o+4], 'big' if byte_order else 'little')
        r, g, b = (v & rmask) >> rs, (v & gmask) >> gs, (v & bmask) >> bs
        row += bytes((r, g, b))
        counts[(r, g, b)] = counts.get((r, g, b), 0) + 1
    rows.append(bytes(row))
def chunk(t, data): return struct.pack('>I', len(data)) + t + data + struct.pack('>I', zlib.crc32(t + data) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, ht, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(b''.join(rows), 6)) + chunk(b'IEND', b'')
open(sys.argv[2], 'wb').write(png)
print("top colors:", sorted(counts.items(), key=lambda kv: -kv[1])[:5])
