#!/usr/bin/env python3
# rgb565 frames (240x135, little-endian) -> PNG, nearest-neighbour scaled
# usage: rgb2png.py <scale> <out_dir> files...   |   --sheet <out.png> <cols> files... (1x contact sheet with labels)
import sys, os, struct
from PIL import Image, ImageDraw
def load(p):
    d = open(p, 'rb').read()
    px = struct.unpack('<%dH' % (len(d) // 2), d)
    im = Image.new('RGB', (240, 135))
    im.putdata([(((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31) for v in px])
    return im
a = sys.argv[1:]
if a[0] == '--sheet':
    out, cols, files = a[1], int(a[2]), a[3:]
    rows = (len(files) + cols - 1) // cols
    sh = Image.new('RGB', (cols * 248, rows * 152), (30, 30, 30))
    dr = ImageDraw.Draw(sh)
    for i, f in enumerate(files):
        x, y = (i % cols) * 248 + 4, (i // cols) * 152 + 4
        sh.paste(load(f), (x, y)); dr.text((x, y + 136), os.path.basename(f)[:-7], fill=(200, 200, 200))
    sh.save(out)
else:
    s, od, files = int(a[0]), a[1], a[2:]
    os.makedirs(od, exist_ok=True)
    for f in files:
        load(f).resize((240 * s, 135 * s), Image.NEAREST).save(os.path.join(od, os.path.basename(f)[:-7] + '.png'), optimize=True)
