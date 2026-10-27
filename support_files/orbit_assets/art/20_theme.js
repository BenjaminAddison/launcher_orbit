// ─────────────────────────────────────────────────────────────────────────────
// Themes. Each maps to one of Launcher's "UI Color" choices. Values are RGB888
// for readability; they are snapped to RGB565 when the theme is applied, so
// what the mockup shows is exactly what the panel can show.
// ─────────────────────────────────────────────────────────────────────────────
const THEMES = [
  {
    key: 'nova', name: 'NOVA', was: 'Default', note: 'coral on deep indigo',
    bg: 0x07081a, bg2: 0x0e1029, bg3: 0x181b3f, line: 0x262b5a, line2: 0x3d4588, dim: 0x7079b4, text: 0xdfe3ff, hi: 0xffffff,
    acc: 0xff6b7d, accD: 0x9c3150, accL: 0xffb8c0, sec: 0x8fa0ff, secD: 0x3f4aa0, onAcc: 0x0a0b1c,
    ok: 0x6dffb4, warn: 0xffcc55, err: 0xff4a5a,
    sky0: 0x04050e, sky1: 0x0b0d26, sky2: 0x1b1e4c,
    pl0: 0x05071a, pl1: 0x0a1534, pl2: 0x102856, pl3: 0x1b4384, pl4: 0x3874bf, atm: 0x8fd4ff, city: 0xffc27a, star: 0xffffff,
  },
  {
    key: 'aurora', name: 'AURORA', was: 'Green', note: 'mint on polar night',
    bg: 0x050b12, bg2: 0x0a141f, bg3: 0x112133, line: 0x1b3250, line2: 0x2c5078, dim: 0x5f88ad, text: 0xd6f3ff, hi: 0xffffff,
    acc: 0x4ef5b8, accD: 0x177e5b, accL: 0xb4ffe4, sec: 0xb28cff, secD: 0x5a3fa8, onAcc: 0x04110c,
    ok: 0x4ef5b8, warn: 0xffd166, err: 0xff5d73,
    sky0: 0x03070c, sky1: 0x08121e, sky2: 0x10283a,
    pl0: 0x031016, pl1: 0x062430, pl2: 0x0b3d4a, pl3: 0x146870, pl4: 0x2fa39a, atm: 0x7dffd0, city: 0xff9ce0, star: 0xffffff,
  },
  {
    key: 'solar', name: 'SOLAR', was: 'Yellow', note: 'gold over a rust world',
    bg: 0x0c0806, bg2: 0x17100a, bg3: 0x241a10, line: 0x3a2a19, line2: 0x5c4428, dim: 0xa58a63, text: 0xfff1da, hi: 0xffffff,
    acc: 0xffcf3f, accD: 0x8f6a10, accL: 0xffe9a0, sec: 0xff6a4d, secD: 0x8f2e20, onAcc: 0x140d04,
    ok: 0x9ee86a, warn: 0xffcf3f, err: 0xff5a3d,
    sky0: 0x080504, sky1: 0x150c08, sky2: 0x2e160c,
    pl0: 0x100705, pl1: 0x28110a, pl2: 0x4a1f10, pl3: 0x7a3818, pl4: 0xbf6430, atm: 0xffb070, city: 0xffe08a, star: 0xfff3d8,
  },
  {
    key: 'ice', name: 'ICE', was: 'Blue', note: 'glacier blue, pink signal',
    bg: 0x050a16, bg2: 0x0a1426, bg3: 0x112039, line: 0x1c3456, line2: 0x2c5182, dim: 0x6a90bc, text: 0xe2f2ff, hi: 0xffffff,
    acc: 0x8ad8ff, accD: 0x2f6c99, accL: 0xd4f1ff, sec: 0xff7ad0, secD: 0x8f3270, onAcc: 0x041020,
    ok: 0x7dffc4, warn: 0xffd27a, err: 0xff6a8a,
    sky0: 0x03060e, sky1: 0x08101f, sky2: 0x112443,
    pl0: 0x061024, pl1: 0x10284a, pl2: 0x234a78, pl3: 0x5b86b4, pl4: 0xb8d8f0, atm: 0xcfefff, city: 0xff9ad8, star: 0xffffff,
  },
  {
    key: 'volt', name: 'VOLT', was: 'Purple', note: 'acid lime over violet',
    bg: 0x0b0716, bg2: 0x150e27, bg3: 0x20163b, line: 0x34245a, line2: 0x4f3886, dim: 0x8c76ba, text: 0xefe7ff, hi: 0xffffff,
    acc: 0xc8f53a, accD: 0x5e7d12, accL: 0xe8ffa2, sec: 0xff4f9a, secD: 0x8f1f50, onAcc: 0x0e1004,
    ok: 0xc8f53a, warn: 0xffcc55, err: 0xff4f6a,
    sky0: 0x07040f, sky1: 0x120a22, sky2: 0x241544,
    pl0: 0x0a0618, pl1: 0x1a0f36, pl2: 0x2d1a5c, pl3: 0x4a2c8c, pl4: 0x7a52c8, atm: 0xc8f53a, city: 0xff6fb0, star: 0xffffff,
  },
  {
    key: 'mono', name: 'MONO', was: 'White', note: 'white phosphor',
    bg: 0x000000, bg2: 0x0b0b0b, bg3: 0x1a1a1a, line: 0x2b2b2b, line2: 0x484848, dim: 0x8c8c8c, text: 0xe6e6e6, hi: 0xffffff,
    acc: 0xf2f2f2, accD: 0x6c6c6c, accL: 0xffffff, sec: 0xa6a6a6, secD: 0x4c4c4c, onAcc: 0x000000,
    ok: 0xffffff, warn: 0xd0d0d0, err: 0xffffff,
    sky0: 0x000000, sky1: 0x080808, sky2: 0x161616,
    pl0: 0x050505, pl1: 0x121212, pl2: 0x262626, pl3: 0x444444, pl4: 0x7a7a7a, atm: 0xffffff, city: 0xbdbdbd, star: 0xffffff,
  },
  {
    key: 'paper', name: 'PAPER', was: 'Black', note: 'ink on paper',
    bg: 0xe8e3d4, bg2: 0xdcd5c4, bg3: 0xcdc6b2, line: 0xb6ad97, line2: 0x958b74, dim: 0x6a6252, text: 0x1d1b17, hi: 0x000000,
    acc: 0x1d1b17, accD: 0x5a5448, accL: 0x4a463e, sec: 0xd2402e, secD: 0x8a2a1e, onAcc: 0xe8e3d4,
    ok: 0x2f7d4a, warn: 0xa87410, err: 0xc0302a,
    sky0: 0xefeadc, sky1: 0xe4ddcc, sky2: 0xd6cdb6,
    pl0: 0x1d1b17, pl1: 0x37332b, pl2: 0x5a5448, pl3: 0x857c6a, pl4: 0xb3aa94, atm: 0x1d1b17, city: 0xd2402e, star: 0x6a6252,
  },
];
const TOKENS = ['bg', 'bg2', 'bg3', 'line', 'line2', 'dim', 'text', 'hi', 'acc', 'accD', 'accL', 'sec', 'secD', 'onAcc', 'ok', 'warn', 'err',
  'sky0', 'sky1', 'sky2', 'pl0', 'pl1', 'pl2', 'pl3', 'pl4', 'atm', 'city', 'star'];

// C = current packed palette (what drawing code uses), RAW = RGB888 source
let C = {}, RAW = {}, THEME = THEMES[0];
function applyTheme(key) {
  THEME = THEMES.find(t => t.key === key) || THEMES[0];
  RAW = {}; C = {};
  for (const k of TOKENS) { RAW[k] = THEME[k]; C[k] = pk(THEME[k]); }
  // a few derived mid-tones used for glows and depth
  const der = {
    accM: mix(THEME.acc, THEME.bg, 0.45), secM: mix(THEME.sec, THEME.bg, 0.5), lineM: mix(THEME.line, THEME.bg, 0.5),
    textM: mix(THEME.text, THEME.bg, 0.3), black: THEME.key === 'paper' ? 0xd8d1be : 0x000000,
    gold: 0xe7b84a, goldD: 0x8a6420, goldL: 0xffe390, flame0: 0xfff6d0, flame1: 0xffc24a, flame2: 0xff6a2a, flame3: 0xb8262e,
    ink: THEME.key === 'paper' ? 0x1d1b17 : mix(THEME.bg, 0x000000, 0.7),
    secL: mix(THEME.sec, 0xffffff, 0.45), errD: mix(THEME.err, THEME.bg, 0.5), errL: mix(THEME.err, 0xffffff, 0.45),
    okL: mix(THEME.ok, 0xffffff, 0.4), okD: mix(THEME.ok, THEME.bg, 0.5),
  };
  for (const k in der) { RAW[k] = der[k]; C[k] = pk(der[k]); }
  if (typeof onThemeChange === 'function') onThemeChange();
}
applyTheme('nova');
