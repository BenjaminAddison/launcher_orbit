// ─────────────────────────────────────────────────────────────────────────────
// Sprites. Big icons are built with a tiny pixel DSL (shapes → bevel → outline)
// so every icon shares the same light direction (top-left) and 1px ink edge.
// Palette chars: k ink · 0..4 neutral ramp · a/A/L accent · s/S/e secondary ·
// g/G/Y gold · r/R error · b background · w white · x per-use custom colour
// ─────────────────────────────────────────────────────────────────────────────
class SB {
  constructor(w, h) { this.w = w; this.h = h; this.d = new Array(w * h).fill('.'); }
  at(x, y) { return (x < 0 || y < 0 || x >= this.w || y >= this.h) ? '.' : this.d[y * this.w + x]; }
  set(x, y, c) { if (x >= 0 && y >= 0 && x < this.w && y < this.h) this.d[y * this.w + x] = c; return this; }
  fill(x, y, w, h, c) { for (let j = y; j < y + h; j++) for (let i = x; i < x + w; i++) this.set(i, j, c); return this; }
  line(x0, y0, x1, y1, c) {
    let dx = Math.abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -Math.abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (; ;) { this.set(x0, y0, c); if (x0 === x1 && y0 === y1) break; const e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; } }
    return this;
  }
  disc(cx, cy, r, c) { for (let j = 0; j < this.h; j++) for (let i = 0; i < this.w; i++) { const dx = i + .5 - cx, dy = j + .5 - cy; if (dx * dx + dy * dy <= r * r) this.set(i, j, c); } return this; }
  ring(cx, cy, r0, r1, c, a0 = 0, a1 = 1) {
    for (let j = 0; j < this.h; j++) for (let i = 0; i < this.w; i++) {
      const dx = i + .5 - cx, dy = j + .5 - cy, d = Math.sqrt(dx * dx + dy * dy); if (d < r0 || d > r1) continue;
      let a = Math.atan2(dx, -dy) / (2 * Math.PI); if (a < 0) a += 1; if (a >= a0 && a <= a1) this.set(i, j, c);
    }
    return this;
  }
  poly(pts, c) {
    for (let j = 0; j < this.h; j++) for (let i = 0; i < this.w; i++) {
      const x = i + .5, y = j + .5; let inside = false;
      for (let k = 0, l = pts.length - 1; k < pts.length; l = k++) {
        const [xi, yi] = pts[k], [xl, yl] = pts[l];
        if (((yi > y) !== (yl > y)) && (x < (xl - xi) * (y - yi) / (yl - yi) + xi)) inside = !inside;
      }
      if (inside) this.set(i, j, c);
    }
    return this;
  }
  map(fn) { const o = this.d.slice(); for (let j = 0; j < this.h; j++) for (let i = 0; i < this.w; i++) { const r = fn(i, j, o[j * this.w + i], (x, y) => (x < 0 || y < 0 || x >= this.w || y >= this.h) ? '.' : o[y * this.w + x]); if (r) this.d[j * this.w + i] = r; } return this; }
  // light edge on top/left, dark edge on bottom/right, for pixels equal to ch
  bevel(ch, hi, lo) {
    return this.map((x, y, c, o) => {
      if (c !== ch) return;
      if (o(x, y - 1) !== ch || o(x - 1, y) !== ch) return hi;
      if (o(x, y + 1) !== ch || o(x + 1, y) !== ch) return lo;
    });
  }
  outline(c = 'k', diag = false) {
    return this.map((x, y, v, o) => {
      if (v !== '.') return;
      const n = [o(x - 1, y), o(x + 1, y), o(x, y - 1), o(x, y + 1)];
      if (diag) n.push(o(x - 1, y - 1), o(x + 1, y - 1), o(x - 1, y + 1), o(x + 1, y + 1));
      if (n.some(q => q !== '.' && q !== c)) return c;
    });
  }
  text(s, x, y, c, font = 'BODY') {
    const f = FONTS[font]; let cx = x;
    for (const ch of s) { if (ch === ' ') { cx += f.space + f.sp; continue; } const gl = glyphOf(f, ch); for (let j = 0; j < f.h; j++) for (let i = 0; i < gl.w; i++) if (gl.bits[j * gl.w + i]) this.set(cx + i, y + j, c); cx += gl.w + f.sp; }
    return this;
  }
  paste(rows, x, y) { rows.forEach((r, j) => { for (let i = 0; i < r.length; i++) if (r[i] !== '.') this.set(x + i, y + j, r[i]); }); return this; }
  replace(a, b) { this.d = this.d.map(c => c === a ? b : c); return this; }
  done() { return { w: this.w, h: this.h, d: this.d.join('') }; }
}

// ── 32×32 module icons ──────────────────────────────────────────────────────
const ICON = {};

ICON.sd = new SB(32, 32)
  .poly([[6, 2], [20, 2], [26, 8], [26, 30], [6, 30]], '2')
  .fill(6, 11, 1, 5, '.')
  .map((x, y, c) => (c === '2' && y >= 4 && y <= 7 && x >= 9 && x <= 21 && (x & 1)) ? (y === 7 ? 'G' : 'g') : undefined)
  .bevel('2', '3', '1')
  .fill(9, 13, 14, 14, 'a').bevel('a', 'L', 'A')
  .text('SD', 10, 16, 'k')
  .set(10, 14, 'w').set(11, 14, 'w')
  .outline('k').done();

ICON.ota = (() => {
  const s = new SB(32, 32);
  s.poly([[9, 31], [23, 31], [20, 26], [12, 26]], '2').bevel('2', '3', '1');           // base
  s.fill(15, 18, 2, 8, '1').set(15, 18, '2');                                          // mast
  // parabolic bowl, axis pointing up-right: back shell, then rim face on top
  const cx = 13.5, cy = 14.5, a = 10.2, b = 3.4, c = 6.4;
  s.map((x, y) => {
    const dx = x + .5 - cx, dy = y + .5 - cy, u = (dx + dy) / Math.SQRT2, w = (dx - dy) / Math.SQRT2;
    if (w <= 0 && (u * u) / (a * a) + (w * w) / (c * c) <= 1) return '1';
  });
  s.bevel('1', '2', '0');
  s.map((x, y) => {
    const dx = x + .5 - cx, dy = y + .5 - cy, u = (dx + dy) / Math.SQRT2, w = (dx - dy) / Math.SQRT2;
    if ((u * u) / (a * a) + (w * w) / (b * b) <= 1) return u < -4 ? '4' : u < 3 ? '3' : '2';
  });
  s.line(14, 14, 19, 9, '0').line(15, 14, 20, 9, '1');                                // feed arm
  s.fill(19, 7, 3, 3, 'a').set(19, 7, 'L').set(21, 9, 'A');                           // feed horn
  s.ring(20.5, 8.5, 4.5, 5.5, 'a', 0.03, 0.22).ring(20.5, 8.5, 7.5, 8.5, 'a', 0.05, 0.2).ring(20.5, 8.5, 10.5, 11.5, 'A', 0.07, 0.18);
  return s.outline('k').done();
})();

ICON.wui = (() => {
  const s = new SB(32, 32);
  s.fill(2, 5, 28, 22, '1').fill(2, 5, 28, 5, '2').bevel('2', '3', '2');
  s.fill(4, 11, 24, 14, 'b');
  s.fill(5, 7, 2, 1, 'a').fill(8, 7, 2, 1, 'g').fill(11, 7, 2, 1, '3');
  s.fill(15, 7, 12, 1, '1');
  s.disc(16, 18, 6.2, 's');
  s.map((x, y, c) => {
    if (c !== 's') return; const dx = x + .5 - 16, dy = y + .5 - 18;
    if (Math.abs(dy) < .6 || Math.abs(Math.abs(dx) - 3.2 * Math.sqrt(Math.max(0, 1 - dy * dy / 38))) < .55 || Math.abs(dx) < .6) return 'S';
  });
  s.bevel('s', 'e', 's');
  s.paste(['k.....', 'kk....', 'k4k...', 'k44k..', 'k444k.', 'k4444k', 'k44kkk', 'k4k...', 'kk....'], 20, 19);
  return s.outline('k').done();
})();

ICON.usb = (() => {
  const s = new SB(32, 32);
  s.fill(15, 26, 2, 6, '2').set(16, 26, '1').set(16, 28, '1').set(16, 30, '1');
  s.fill(9, 13, 14, 13, 'a').bevel('a', 'L', 'A');
  s.fill(10, 12, 12, 1, '1');
  s.fill(11, 3, 10, 9, '3').bevel('3', '4', '2');
  s.fill(13, 6, 2, 2, 'k').fill(17, 6, 2, 2, 'k');
  // USB trident
  s.line(16, 15, 16, 23, 'A').line(15, 16, 17, 16, 'A').set(16, 14, 'A');
  s.line(16, 21, 13, 18, 'A').fill(12, 17, 2, 2, 'A');
  s.line(16, 20, 19, 18, 'A').set(19, 17, 'A').set(20, 17, 'A').set(20, 18, 'A');
  s.fill(15, 23, 3, 1, 'A');
  return s.outline('k').done();
})();

ICON.pman = (() => {
  const s = new SB(32, 32);
  for (let k = 0; k < 5; k++) {
    s.fill(3, 9 + k * 3, 4, 1, '3').fill(25, 9 + k * 3, 4, 1, '3');
    s.fill(9 + k * 3, 3, 1, 4, '3').fill(9 + k * 3, 25, 1, 4, '3');
  }
  s.fill(7, 7, 18, 18, '1').bevel('1', '2', '0');
  s.fill(9, 13, 14, 6, 'b');
  s.fill(10, 14, 4, 4, 'a').fill(15, 14, 3, 4, 's').fill(19, 14, 2, 4, 'g').fill(22, 14, 1, 4, '2');
  s.set(10, 14, 'L').set(15, 14, 'e').set(19, 14, 'Y');
  s.set(9, 9, '3').set(10, 9, '3').set(9, 10, '3');
  s.fill(9, 21, 8, 1, '2').fill(9, 23, 5, 1, '2');
  return s.outline('k').done();
})();

ICON.cfg = (() => {
  const s = new SB(32, 32), cx = 16, cy = 16;
  s.map((x, y) => {
    const dx = x + .5 - cx, dy = y + .5 - cy, r = Math.sqrt(dx * dx + dy * dy), th = Math.atan2(dy, dx);
    const tooth = Math.cos(8 * th + Math.PI / 8) > 0.15;
    if (r < 4.6) return; if (r <= (tooth ? 13.6 : 10.6)) return '2';
  });
  s.bevel('2', '3', '1');
  s.ring(cx, cy, 4.6, 6.8, 'a').bevel('a', 'L', 'A');
  s.map((x, y, c) => { const dx = x + .5 - cx, dy = y + .5 - cy, r = Math.hypot(dx, dy); if (c === '2' && r > 8.6 && r < 9.6) return '1'; });
  return s.outline('k').done();
})();

ICON.off = (() => {
  const s = new SB(32, 32);
  s.ring(16, 17, 8.4, 11.6, 'r', 0.11, 0.89).bevel('r', 'q', 'R');
  s.fill(14, 3, 4, 13, 'r').bevel('r', 'q', 'R');
  return s.outline('k').done();
})();

ICON.chg = (() => {
  const s = new SB(32, 32);
  s.fill(2, 9, 25, 15, '2').bevel('2', '3', '1');               // battery shell
  s.fill(27, 13, 3, 7, '2').bevel('2', '3', '1');               // terminal nub
  s.fill(4, 11, 21, 11, 'b');                                     // cavity
  s.fill(5, 12, 13, 9, 'o').bevel('o', 'p', 'O');               // charge level
  for (let x = 8; x < 18; x += 4) s.fill(x, 12, 1, 9, 'O');      // cell dividers
  s.poly([[18, 3], [9, 17], [15, 17], [12, 29], [23, 13], [17, 13], [21, 3]], 'Y');
  s.bevel('Y', 'w', 'g');
  return s.outline('k').done();
})();

ICON.app = (() => {
  const s = new SB(32, 32);
  s.poly([[5, 2], [24, 2], [27, 5], [27, 30], [5, 30]], '2').bevel('2', '3', '1');
  for (let x = 8; x <= 22; x += 2) s.set(x, 4, '1').set(x, 5, '1');
  s.fill(8, 8, 17, 15, 'x').bevel('x', 'X', 'Z');
  s.poly([[13, 24], [19, 24], [16, 27]], '1');
  s.map((x, y, c) => (y >= 28 && y <= 29 && x >= 8 && x <= 24) ? ((x & 1) ? 'g' : 'G') : undefined);
  return s.outline('k').done();
})();

// ── Larger illustration sprites ─────────────────────────────────────────────
// Rocket on the pad (boot screen) — 18×48
const ROCKET = (() => {
  const s = new SB(18, 48), cx = 9;
  s.map((x, y) => {                                 // ogive nose + body
    const dx = Math.abs(x + .5 - cx);
    if (y < 13) { const half = 5 * Math.sqrt(y / 13) + 0.2; if (dx <= half) return '3'; }
    else if (y < 38 && dx <= 5.2) return '3';
  });
  s.map((x, y, c) => (c === '3' && x >= 11 && y >= 3) ? '2' : undefined);          // shade right side
  s.map((x, y, c) => (c === '3' && x <= 5 && y >= 6) ? '4' : undefined);           // highlight left
  s.fill(4, 13, 10, 2, 'a').fill(4, 13, 2, 2, 'L').fill(12, 13, 2, 2, 'A');      // band
  s.fill(4, 31, 10, 2, 'a').fill(4, 31, 2, 2, 'L').fill(12, 31, 2, 2, 'A');
  s.disc(9, 20.5, 2.6, 'k').disc(9, 20.5, 1.8, 's').set(8, 19, 'e');               // porthole
  s.fill(6, 25, 6, 1, '2').fill(6, 27, 4, 1, '2');                                 // panel lines
  s.poly([[4, 29], [4, 41], [0, 44], [0, 37]], 'a').poly([[14, 29], [14, 41], [18, 44], [18, 37]], 'A');   // fins
  s.poly([[5, 38], [13, 38], [14, 44], [4, 44]], '1').fill(5, 38, 8, 1, '2');      // nozzle
  s.fill(8, 30, 2, 12, '2');                                                        // centre fin edge
  return s.outline('k').done();
})();

// Small climbing rocket (install sequence) — 9×15
const MINI_ROCKET = sprite([
  '....k....',
  '...k4k...',
  '..k443k..',
  '..k432k..',
  '..k4s2k..',
  '..k432k..',
  '..kaaak..',
  '..k432k..',
  '.kk432kk.',
  'kak432kAk',
  'kakkkkkAk',
  'kk.k1k.kk',
  '...kkk...',
  '.........',
  '.........',
]);

// Satellite crossing the home sky — 9×5
const SAT = sprite([
  'ss.....ss',
  'sSs.g.sSs',
  'sSsgYgsSs',
  'sSs.g.sSs',
  'ss.....ss',
]);

// ── 7–9px glyph icons (single colour, '#' = ink, '+' = secondary ink) ───────
const GLYPH = {};
const G = (name, rows) => { GLYPH[name] = sprite(rows); };
G('rocket', ['...#...', '..###..', '..#.#..', '..###..', '..###..', '.#####.', '##.#.##', '...+...']);
G('sd', ['###..', '####.', '#####', '#.#.#', '#####', '#####', '#####']);
G('wifi', ['.#####.', '#.....#', '..###..', '.#...#.', '.......', '...#...']);
G('bolt', ['..##', '.##.', '####', '.##.', '##..']);
G('folder', ['###.....', '#..#####', '#......#', '#......#', '#......#', '########']);
G('folderF', ['###.....', '########', '########', '########', '########', '########']);
G('bin', ['.#.#.#.', '#######', '#.....#', '#.###.#', '#.....#', '#######', '.#.#.#.']);
G('file', ['####..', '#..##.', '#...##', '#....#', '#....#', '#....#', '######']);
G('up', ['..#....', '.###...', '#.#.#..', '..#....', '..#....', '..####.', '.......']);
G('lock', ['.###.', '#...#', '#####', '##.##', '##.##', '#####']);
G('star', ['..#..', '.###.', '#####', '.###.', '.#.#.']);
G('gear', ['.#.#.#.', '#######', '.##.##.', '###.###', '.##.##.', '#######', '.#.#.#.']);
G('globe', ['.###.', '#.#.#', '#####', '#.#.#', '.###.']);
G('usb', ['..#..', '.###.', '..#.#', '#.#.#', '.###.', '..#..', '..#..']);
G('chip', ['.#.#.', '#####', '##.##', '#####', '.#.#.']);
G('power', ['..#..', '#.#.#', '#.#.#', '#...#', '.###.']);
G('sat', ['#.....#', '#..#..#', '##.#.##', '#..#..#', '#.....#']);
G('search', ['.###..', '#...#.', '#...#.', '#...#.', '.####.', '....##']);
G('key', ['.###...', '#...###', '#.#.#.#', '.###...']);
G('trash', ['.###.', '#####', '.#.#.', '.#.#.', '.#.#.', '.###.']);
G('copy', ['####.', '#..##', '#.#.#', '##..#', '.#..#', '.####']);
G('paste', ['.##.', '####', '#..#', '#..#', '#..#', '####']);
G('pen', ['....##', '...###', '..###.', '.###..', '###...', '##....']);
G('install', ['..#..', '..#..', '#.#.#', '.###.', '..#..', '#####']);
G('newdir', ['###...', '#..###', '#.#..#', '#.###.', '#..#.#', '#####.']);
G('home', ['..#..', '.###.', '#####', '.#.#.', '.###.']);
G('dl', ['..#..', '..#..', '#.#.#', '.###.', '..#..', '.....', '#####']);
G('back', ['..#....', '.#.....', '#######', '.#....#', '..#...#', '......#']);
G('check', ['....#', '...#.', '#.#..', '.#...']);
G('cross', ['#...#', '.#.#.', '..#..', '.#.#.', '#...#']);
G('warn', ['..#..', '.#.#.', '.#.#.', '#...#', '#.#.#', '#####']);
G('info', ['.###.', '##.##', '#####', '##.##', '##.##', '.###.']);
G('ask', ['.###.', '#...#', '...#.', '..#..', '.....', '..#..']);
G('bkp', ['#####', '#...#', '#.#.#', '#...#', '#####']);
G('restore', ['.###.', '#...#', '#.###', '#..#.', '.###.']);
G('bind', ['#####', '#...#', '#.#.#', '#...#', '##.##']);
G('play', ['#...', '##..', '###.', '##..', '#...']);
G('cart', ['.####', '#####', '#...#', '#...#', '#####', '#.#.#']);
G('ap', ['#.....#', '.#...#.', '..#.#..', '...#...', '...#...', '..###..']);
G('net', ['..#..', '.#.#.', '#...#', '.....', '..#..', '.###.']);
G('ble', ['..#..', '#.##.', '.###.', '..#..', '.###.', '#.##.', '..#..']);
G('tag', ['###.', '####', '#.##', '####', '###.']);
G('plus', ['..#..', '..#..', '#####', '..#..', '..#..']);
G('minus', ['.....', '.....', '#####']);
G('sort', ['#....', '##...', '###..', '####.', '#####']);
G('bright', ['#.#.#', '.###.', '##.##', '.###.', '#.#.#']);
G('rotate', ['.###.', '#...#', '#....', '#..#.', '.####']);
G('timer', ['.###.', '#.#.#', '#.###', '#...#', '.###.']);
G('eye', ['.###.', '#.#.#', '#####', '.###.']);
G('battery', ['#####.', '#...##', '#...##', '#####.']);
G('reset', ['.###.', '#...#', '##..#', '#...#', '.###.']);
G('dots', ['.......', '.......', '#.#.#..']);
G('enter', ['....#', '....#', '.#..#', '#####', '.#...']);

// Draw a glyph icon in a colour (+ secondary colour for '+')
function gicon(g, name, x, y, c, c2) {
  const s = GLYPH[name]; if (!s) return;
  for (let j = 0; j < s.h; j++) for (let i = 0; i < s.w; i++) { const ch = s.d[j * s.w + i]; if (ch === '#') g.px(x + i, y + j, c); else if (ch === '+') g.px(x + i, y + j, c2 !== undefined ? c2 : c); }
}

// Palettes for the big icons
function iconPal(mode = 'full', custom) {
  if (mode === 'full') return {
    k: C.ink, '0': C.line, '1': C.line2, '2': C.dim, '3': C.text, '4': C.hi, w: pk(0xffffff), b: C.bg,
    a: C.acc, A: C.accD, L: C.accL, s: C.sec, S: C.secD, e: C.secL, g: C.gold, G: C.goldD, Y: C.goldL,
    r: C.err, R: C.errD, q: C.errL, x: custom ? custom[0] : C.sec, X: custom ? custom[1] : C.secL, Z: custom ? custom[2] : C.secD,
    o: C.ok, O: C.okD, p: C.okL,
  };
  if (mode === 'dim') { const hiC = C.dim, mid = C.line2, lo = C.line; return { k: C.ink, '0': lo, '1': lo, '2': mid, '3': hiC, '4': hiC, w: hiC, b: C.bg, a: mid, A: lo, L: hiC, s: mid, S: lo, e: hiC, g: mid, G: lo, Y: hiC, r: mid, R: lo, q: hiC, x: mid, X: hiC, Z: lo, o: mid, O: lo, p: hiC }; }
  if (mode === 'far') { const hiC = C.line2, mid = C.line, lo = C.bg3; return { k: C.ink, '0': lo, '1': lo, '2': mid, '3': hiC, '4': hiC, w: hiC, b: C.bg, a: mid, A: lo, L: hiC, s: mid, S: lo, e: hiC, g: mid, G: lo, Y: hiC, r: mid, R: lo, q: hiC, x: mid, X: hiC, Z: lo, o: mid, O: lo, p: hiC }; }
}

// Procedural emblem (identicon) for a firmware / app: 5×5 mirrored cells
function emblemBits(seed) {
  const h = fnv(seed), bits = [];
  for (let j = 0; j < 5; j++) { bits.push([]); for (let i = 0; i < 3; i++) bits[j].push((h >> (j * 3 + i)) & 1); }
  return bits.map(r => [r[0], r[1], r[2], r[1], r[0]]);
}
function drawEmblem(g, seed, x, y, cell, cOn, cOff) {
  const b = emblemBits(seed);
  for (let j = 0; j < 5; j++) for (let i = 0; i < 5; i++) { const c = b[j][i] ? cOn : cOff; if (c !== undefined) g.fill(x + i * cell, y + j * cell, cell, cell, c); }
}
const APP_FAMS = [['sec', 'secL', 'secD'], ['gold', 'goldL', 'goldD'], ['ok', 'okL', 'okD'], ['acc', 'accL', 'accD']];
function appHue(seed) { // installed apps get colours in install order; anything else hashes
  const i = (typeof SIM !== 'undefined') ? SIM.apps.findIndex(a => a.label === seed) : -1;
  return APP_FAMS[i >= 0 ? i % APP_FAMS.length : fnv(seed + '#') % APP_FAMS.length];
}
