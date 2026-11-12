#!/usr/bin/env python3
# rgb565 clips -> one showreel, joined with the UI's own 4x4 Bayer dissolve.
# usage: reel.py <out.gif> <out.mp4|-> <scale> <dir:first:last> ...
# Clips are 30 fps (harness --film step 2). The GIF keeps every other frame at 60 ms
# (GIF delays are whole 10 ms steps), so it plays about 10% faster than the MP4.
import os, struct, subprocess, sys
import numpy as np
from PIL import Image

BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]])
MASK = np.tile(BAYER, (135 // 4 + 1, 240 // 4))[:135, :240]
FADE = 8  # frames per dissolve


def load(p):
    v = np.frombuffer(open(p, 'rb').read(), '<u2').reshape(135, 240).astype(np.uint32)
    return np.dstack([(v >> 11 & 31) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31]).astype(np.uint8)


def clip(spec):
    d, a, b = spec.rsplit(':', 2)
    fs = sorted(f for f in os.listdir(d) if f.startswith('f') and f.endswith('.rgb565'))
    return [load(os.path.join(d, f)) for f in fs[int(a):int(b)]]


out_gif, out_mp4, s = sys.argv[1], sys.argv[2], int(sys.argv[3])
frames = []
for spec in sys.argv[4:]:
    c = clip(spec)
    if frames:  # dissolve the last FADE frames of the reel into the first of this clip
        for i in range(FADE):
            lvl = (i + 1) * 16 // (FADE + 1)
            j = len(frames) - FADE + i
            frames[j] = np.where((MASK < lvl)[..., None], c[i], frames[j])
        c = c[FADE:]
    frames += c
frames.append(frames[0])  # loop back through the title's dark first frame
big = [np.repeat(np.repeat(f, s, 0), s, 1) for f in frames]

g = [Image.fromarray(f).quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE) for f in big[::2]]
g[0].save(out_gif, save_all=True, append_images=g[1:], duration=66, loop=0, optimize=True, disposal=1)
print(out_gif, len(g), 'frames', os.path.getsize(out_gif), 'bytes')

if out_mp4 != '-':
    v = [np.repeat(np.repeat(f, 2, 0), 2, 1) for f in big]  # twice the GIF's size
    h, w = v[0].shape[:2]
    p = subprocess.Popen(['ffmpeg', '-y', '-loglevel', 'error', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', f'{w}x{h}', '-r', '30',
                          '-i', '-', '-c:v', 'libx264', '-preset', 'slow', '-crf', '20', '-pix_fmt', 'yuv420p',
                          '-movflags', '+faststart', out_mp4], stdin=subprocess.PIPE)
    for f in v:
        p.stdin.write(f.tobytes())
    p.stdin.close()
    p.wait()
    print(out_mp4, len(v), 'frames', os.path.getsize(out_mp4), 'bytes')
