#!/usr/bin/env python3
# rgb565 frame dir -> animated GIF (nearest-neighbour scaled)
# usage: frames2gif.py <framedir> <out.gif> <scale> <ms_per_frame> [first] [last] [hold_last_ms]
import sys, os, struct
from PIL import Image
d, out, s, ms = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
files = sorted(f for f in os.listdir(d) if f.startswith('f') and f.endswith('.rgb565'))
a = int(sys.argv[5]) if len(sys.argv) > 5 else 0
b = int(sys.argv[6]) if len(sys.argv) > 6 else len(files)
hold = int(sys.argv[7]) if len(sys.argv) > 7 else 0
frames = []
for f in files[a:b]:
    raw = open(os.path.join(d, f), 'rb').read()
    px = struct.unpack('<%dH' % (len(raw) // 2), raw)
    im = Image.new('RGB', (240, 135))
    im.putdata([(((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31) for v in px])
    frames.append(im.resize((240 * s, 135 * s), Image.NEAREST).quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE))
dur = [ms] * len(frames)
if hold: dur[-1] = hold
frames[0].save(out, save_all=True, append_images=frames[1:], duration=dur, loop=0, optimize=True, disposal=1)
print(out, len(frames), 'frames', os.path.getsize(out), 'bytes')
