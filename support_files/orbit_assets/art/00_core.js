'use strict';
// ─────────────────────────────────────────────────────────────────────────────
// Core raster engine. Everything is drawn into a 240×135 buffer the same way
// the firmware will draw into a TFT sprite: solid RGB565 colours, no alpha,
// no anti-aliasing. Gradients and fades are ordered (Bayer) dithers.
// ─────────────────────────────────────────────────────────────────────────────
const W = 240, H = 135;

// RGB888 → nearest RGB565 → back to RGB888, packed little-endian ABGR so it can
// be written straight into an ImageData-backed Uint32Array.
function c565(hex) {
  const r = (hex >> 16) & 255, g = (hex >> 8) & 255, b = hex & 255;
  const r5 = Math.round(r * 31 / 255), g6 = Math.round(g * 63 / 255), b5 = Math.round(b * 31 / 255);
  const R = Math.round(r5 * 255 / 31), G = Math.round(g6 * 255 / 63), B = Math.round(b5 * 255 / 31);
  const packed = ((255 << 24) | (B << 16) | (G << 8) | R) >>> 0;
  return { v: (r5 << 11) | (g6 << 5) | b5, r: R, g: G, b: B, p: packed };
}
const hex565 = (hex) => '0x' + c565(hex).v.toString(16).toUpperCase().padStart(4, '0');
const pk = (hex) => c565(hex).p;
function mix(a, b, t) { // mix two RGB888 hex colours
  const ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
  const br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
  return (Math.round(ar + (br - ar) * t) << 16) | (Math.round(ag + (bg - ag) * t) << 8) | Math.round(ab + (bb - ab) * t);
}

const BAYER4 = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5];
const bayer = (x, y) => BAYER4[((y & 3) << 2) | (x & 3)];

const clamp = (v, a, b) => v < a ? a : v > b ? b : v;
const lerp = (a, b, t) => a + (b - a) * t;
const ease = {
  out: (t) => 1 - Math.pow(1 - t, 3),
  inOut: (t) => t < .5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2,
  back: (t) => { const c1 = 1.70158, c3 = c1 + 1; return 1 + c3 * Math.pow(t - 1, 3) + c1 * Math.pow(t - 1, 2); },
  in: (t) => t * t * t,
};
function mulberry32(a) {
  return function () { a |= 0; a = a + 0x6D2B79F5 | 0; let t = Math.imul(a ^ a >>> 15, 1 | a); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; };
}
function fnv(str) { let h = 0x811c9dc5; for (let i = 0; i < str.length; i++) { h ^= str.charCodeAt(i); h = Math.imul(h, 0x01000193); } return h >>> 0; }
const fmtSize = (b) => b >= 1048576 ? (b / 1048576).toFixed(b >= 10485760 ? 0 : 1) + 'MB' : b >= 1024 ? Math.round(b / 1024) + 'KB' : b + 'B';
const hex32 = (v) => '0x' + v.toString(16).toUpperCase();

class Gfx {
  constructor() {
    this.buf = new Uint32Array(W * H);
    this.ox = 0; this.oy = 0;
    this.resetClip();
  }
  resetClip() { this.x0 = 0; this.y0 = 0; this.x1 = W; this.y1 = H; }
  clip(x, y, w, h) {
    x += this.ox; y += this.oy;
    this.x0 = Math.max(0, x); this.y0 = Math.max(0, y); this.x1 = Math.min(W, x + w); this.y1 = Math.min(H, y + h);
  }
  origin(x, y) { this.ox = x; this.oy = y; }
  clear(c) { this.buf.fill(c); }
  px(x, y, c) {
    x = (x + this.ox) | 0; y = (y + this.oy) | 0;
    if (x < this.x0 || y < this.y0 || x >= this.x1 || y >= this.y1) return;
    this.buf[y * W + x] = c;
  }
  get(x, y) {
    x = (x + this.ox) | 0; y = (y + this.oy) | 0;
    if (x < 0 || y < 0 || x >= W || y >= H) return 0;
    return this.buf[y * W + x];
  }
  fill(x, y, w, h, c) {
    x = (x + this.ox) | 0; y = (y + this.oy) | 0;
    const xa = Math.max(this.x0, x), ya = Math.max(this.y0, y), xb = Math.min(this.x1, x + w), yb = Math.min(this.y1, y + h);
    for (let j = ya; j < yb; j++) { const o = j * W; for (let i = xa; i < xb; i++) this.buf[o + i] = c; }
  }
  // Ordered dither fill: level 0..16 of pixels get colour c.
  dfill(x, y, w, h, c, level) {
    if (level <= 0) return; if (level >= 16) return this.fill(x, y, w, h, c);
    x = (x + this.ox) | 0; y = (y + this.oy) | 0;
    const xa = Math.max(this.x0, x), ya = Math.max(this.y0, y), xb = Math.min(this.x1, x + w), yb = Math.min(this.y1, y + h);
    for (let j = ya; j < yb; j++) { const o = j * W; for (let i = xa; i < xb; i++) if (BAYER4[((j & 3) << 2) | (i & 3)] < level) this.buf[o + i] = c; }
  }
  // 50% checker over existing pixels (cheap "dim" for backdrops)
  checker(x, y, w, h, c, phase = 0) {
    x = (x + this.ox) | 0; y = (y + this.oy) | 0;
    const xa = Math.max(this.x0, x), ya = Math.max(this.y0, y), xb = Math.min(this.x1, x + w), yb = Math.min(this.y1, y + h);
    for (let j = ya; j < yb; j++) { const o = j * W; for (let i = xa; i < xb; i++) if (((i + j + phase) & 1) === 0) this.buf[o + i] = c; }
  }
  hline(x, y, w, c) { this.fill(x, y, w, 1, c); }
  vline(x, y, h, c) { this.fill(x, y, 1, h, c); }
  rect(x, y, w, h, c) { this.hline(x, y, w, c); this.hline(x, y + h - 1, w, c); this.vline(x, y, h, c); this.vline(x + w - 1, y, h, c); }
  // Chamfered panel: corners cut by `cut` pixels. fill/border may be null.
  panel(x, y, w, h, fill, border, cut = 2) {
    if (fill != null) {
      for (let j = 0; j < h; j++) {
        const d = j < cut ? cut - j : j >= h - cut ? cut - (h - 1 - j) : 0;
        this.fill(x + d, y + j, w - 2 * d, 1, fill);
      }
    }
    if (border != null) {
      this.hline(x + cut, y, w - 2 * cut, border); this.hline(x + cut, y + h - 1, w - 2 * cut, border);
      this.vline(x, y + cut, h - 2 * cut, border); this.vline(x + w - 1, y + cut, h - 2 * cut, border);
      for (let k = 1; k < cut; k++) {
        this.px(x + k, y + cut - k, border); this.px(x + w - 1 - k, y + cut - k, border);
        this.px(x + k, y + h - 1 - cut + k, border); this.px(x + w - 1 - k, y + h - 1 - cut + k, border);
      }
      if (cut > 0) {
        this.px(x + cut, y, border); this.px(x, y + cut, border);
      }
    }
  }
  // Rounded rect with tiny radius (1 or 2) — pixel-exact
  rrect(x, y, w, h, c, r = 1, filled = true) {
    if (filled) {
      this.fill(x + r, y, w - 2 * r, h, c);
      this.fill(x, y + r, r, h - 2 * r, c); this.fill(x + w - r, y + r, r, h - 2 * r, c);
      if (r === 2) { this.px(x + 1, y + 1, c); this.px(x + w - 2, y + 1, c); this.px(x + 1, y + h - 2, c); this.px(x + w - 2, y + h - 2, c); }
    } else {
      this.hline(x + r, y, w - 2 * r, c); this.hline(x + r, y + h - 1, w - 2 * r, c);
      this.vline(x, y + r, h - 2 * r, c); this.vline(x + w - 1, y + r, h - 2 * r, c);
      if (r === 2) { this.px(x + 1, y + 1, c); this.px(x + w - 2, y + 1, c); this.px(x + 1, y + h - 2, c); this.px(x + w - 2, y + h - 2, c); }
    }
  }
  line(x0, y0, x1, y1, c) {
    x0 |= 0; y0 |= 0; x1 |= 0; y1 |= 0;
    let dx = Math.abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -Math.abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (; ;) { this.px(x0, y0, c); if (x0 === x1 && y0 === y1) break; const e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; } }
  }
  dline(x0, y0, x1, y1, c, on = 1, off = 1, phase = 0) {
    x0 |= 0; y0 |= 0; x1 |= 0; y1 |= 0;
    let dx = Math.abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -Math.abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy, n = phase;
    for (; ;) { if ((n % (on + off)) < on) this.px(x0, y0, c); n++; if (x0 === x1 && y0 === y1) break; const e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; } }
  }
  circle(cx, cy, r, c) {
    let x = r, y = 0, e = 1 - r;
    while (x >= y) {
      this.px(cx + x, cy + y, c); this.px(cx + y, cy + x, c); this.px(cx - y, cy + x, c); this.px(cx - x, cy + y, c);
      this.px(cx - x, cy - y, c); this.px(cx - y, cy - x, c); this.px(cx + y, cy - x, c); this.px(cx + x, cy - y, c);
      y++; if (e < 0) e += 2 * y + 1; else { x--; e += 2 * (y - x) + 1; }
    }
  }
  fillCircle(cx, cy, r, c) {
    for (let j = -r; j <= r; j++) { const w = Math.floor(Math.sqrt(r * r - j * j + r * 0.8)); this.hline(cx - w, cy + j, 2 * w + 1, c); }
  }
  // Ring segment; angles in turns (0..1), 0 = up, clockwise.
  ring(cx, cy, r0, r1, a0, a1, c, level = 16) {
    const R = Math.ceil(r1);
    for (let j = -R; j <= R; j++) for (let i = -R; i <= R; i++) {
      const d = Math.sqrt(i * i + j * j); if (d < r0 || d > r1) continue;
      let a = Math.atan2(i, -j) / (2 * Math.PI); if (a < 0) a += 1;
      if (a >= a0 && a <= a1) { if (level >= 16 || bayer(cx + i + this.ox, cy + j + this.oy) < level) this.px(cx + i, cy + j, c); }
    }
  }
  // Indexed sprite. pal: map char→packed colour (missing/undefined = transparent)
  blit(spr, x, y, pal, flip = false) {
    const { w, h, d } = spr;
    for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) {
      const ch = d[j * w + (flip ? w - 1 - i : i)]; if (ch === '.') continue;
      const c = pal[ch]; if (c !== undefined) this.px(x + i, y + j, c);
    }
  }
  // Single-colour silhouette of a sprite (used for outlines / shadows / dimming)
  blitMono(spr, x, y, c) {
    const { w, h, d } = spr;
    for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) if (d[j * w + i] !== '.') this.px(x + i, y + j, c);
  }
  present(ctx, img) {
    new Uint32Array(img.data.buffer).set(this.buf);
    ctx.putImageData(img, 0, 0);
  }
  snapshot() { return this.buf.slice(); }
  restore(snap) { this.buf.set(snap); }
}

function sprite(rows) {
  const h = rows.length, w = rows[0].length;
  return { w, h, d: rows.join('') };
}
