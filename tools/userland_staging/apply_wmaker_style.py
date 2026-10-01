#!/usr/bin/env python3
"""Apply a Window Maker theme (<Name>.themed/style + its images) to the staged
global defaults.

usage: apply_wmaker_style.py <theme-dir> <WindowMaker defaults file> <Pixmaps dir> [font-family]

Window Maker's global domain file (/usr/X11/etc/WindowMaker/WindowMaker) is an
old-style property list. A theme's `style` file is a dictionary of the same
keys (colours, textures, fonts). Every single-line `Key = value;` entry in the
defaults file whose key is also in the style is replaced, and keys that were
not there are added before the closing brace. The theme's images (the
`(spixmap, title.png, ...)` textures) are copied into the Pixmaps directory,
which is on the default PixmapPath (WindowMaker.in:24-32, "#pkgdatadir#/Pixmaps").
If a font family is given, the family part of every *Font entry is replaced by
it (the theme names fonts this image does not have, e.g. Trebuchet MS).
"""
import os, re, shutil, sys

theme, defaults, pixdir = sys.argv[1], sys.argv[2], sys.argv[3]
family = sys.argv[4] if len(sys.argv) > 4 else None

style_lines = []
for l in open(os.path.join(theme, "style")):
    m = re.match(r"^\s*(\w+)\s*=\s*(.*);\s*$", l)
    if m:
        style_lines.append((m.group(1), m.group(2)))
if not style_lines:
    sys.exit("error: no entries found in %s/style" % theme)

def retarget_font(key, value):
    if family is None or not key.endswith("Font"):
        return value
    # "Trebuchet MS,Luxi Sans:bold:pixelsize=12" -> "<family>:bold:pixelsize=12"
    inner = value.strip().strip('"')
    parts = inner.split(":", 1)
    rest = (":" + parts[1]) if len(parts) > 1 else ""
    rest = re.sub(r":slant=\d+|:width=\d+", "", rest)       # Rik asks for slant=0, width=100
    rest = re.sub(r":weight=\d+", "", rest)                  # numeric weight names a face Inter may lack
    return '"%s%s"' % (family, rest)

keys = {k for k, _ in style_lines}
out = []
src = open(defaults).read().split("\n")
for l in src:
    m = re.match(r"^\s*(\w+)\s*=\s*.*;\s*$", l)
    if m and m.group(1) in keys:
        continue                       # replaced below
    out.append(l)
while out and out[-1].strip() == "":
    out.pop()
assert out[-1].strip() == "}", "defaults file does not end with }"
out.pop()
for k, v in style_lines:
    out.append("\t%s = %s;" % (k, retarget_font(k, v)))
out.append("}")
open(defaults, "w").write("\n".join(out) + "\n")

os.makedirs(pixdir, exist_ok=True)
n = 0
for f in sorted(os.listdir(theme)):
    if f.lower().endswith((".png", ".xpm")):
        shutil.copy(os.path.join(theme, f), os.path.join(pixdir, f))
        n += 1
print("applied %d style entries from %s, copied %d images to %s" % (len(style_lines), theme, n, pixdir))
