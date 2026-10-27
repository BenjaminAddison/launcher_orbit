#if defined(ORBIT_UI) || defined(ORBIT_NATIVE)
#include "orbit_gfx.h"
#include <math.h>
#include <stdio.h>

namespace orb {

uint16_t PAL[T_COUNT];
uint8_t themeIdx = 0;
Gfx G;

const uint8_t BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

void setTheme(uint8_t idx) {
    if (idx >= THEME_COUNT) idx = 0;
    themeIdx = idx;
    memcpy(PAL, THEME_COLORS[idx], sizeof(PAL));
}

float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
// e^-x for x >= 0, 2^-n * a polynomial for the fraction (rel. error < 2e-6)
float expNeg(float x) {
    float y = x * 1.4426950f;
    if (y > 120) return 0;
    int n = (int)y;
    float f = y - n; // 2^-f, f in [0, 1)
    float p =
        1 + f * (-0.6931472f + f * (0.2402265f + f * (-0.0555041f + f * (0.0096181f + f * -0.0013334f))));
    union {
        float f;
        uint32_t u;
    } v = {p};
    v.u -= (uint32_t)n << 23;
    return v.f;
}
// sin: reduce by whole turns to [-pi, pi], fold to [-pi/2, pi/2], Taylor to x^11
float fsin(float x) {
    float t = x * 0.15915494f;
    float y = (t - ifloor(t + 0.5f)) * 6.2831853f;
    if (y > 1.5707963f) y = 3.1415927f - y;
    else if (y < -1.5707963f) y = -3.1415927f - y;
    float y2 = y * y;
    return y * (1 + y2 * (-0.16666667f +
                          y2 * (0.0083333333f +
                                y2 * (-1.9841270e-4f + y2 * (2.7557319e-6f + y2 * -2.5052108e-8f)))));
}
float fcos(float x) { return fsin(x + 1.5707963f); }
// atan2 in radians (abs. error < 1e-5)
float atan2_(float y, float x) {
    float ax = fabsf(x), ay = fabsf(y);
    if (ax == 0 && ay == 0) return 0;
    bool sw = ay > ax;
    float z = sw ? ax / ay : ay / ax, z2 = z * z;
    float a =
        z * (0.9998660f + z2 * (-0.3302995f + z2 * (0.1801410f + z2 * (-0.0851330f + z2 * 0.0208351f))));
    if (sw) a = 1.5707963f - a;
    if (x < 0) a = 3.1415927f - a;
    return y < 0 ? -a : a;
}
float easeOut(float t) {
    float u = 1 - t;
    return 1 - u * u * u;
}
float easeIn(float t) { return t * t * t; }
float easeInOut(float t) {
    if (t < .5f) return 4 * t * t * t;
    float u = -2 * t + 2;
    return 1 - u * u * u / 2;
}
float easeBack(float t) {
    const float c1 = 1.70158f, c3 = c1 + 1;
    float u = t - 1;
    return 1 + c3 * u * u * u + c1 * u * u;
}
uint32_t fnv(const char *s) {
    uint32_t h = 0x811c9dc5u;
    for (; *s; ++s) {
        h ^= (uint8_t)*s;
        h *= 0x01000193u;
    }
    return h;
}
float hash2(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) >> 8) / 16777216.0f;
}
float vnoise(float x, float y) {
    int xi = ifloor(x), yi = ifloor(y);
    float xf = x - xi, yf = y - yi;
    float u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
    float a = hash2(xi, yi), b = hash2(xi + 1, yi), c = hash2(xi, yi + 1), d = hash2(xi + 1, yi + 1);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
static uint32_t rndState = 0x1234567u;
uint32_t rnd() {
    rndState = rndState * 1103515245u + 12345u;
    return rndState >> 8;
}

// ── Gfx ─────────────────────────────────────────────────────────────────────
void Gfx::clip(int x, int y, int w, int h) {
    x += ox;
    y += oy;
    cx0 = x < bx0 ? bx0 : x;
    cy0 = y < 0 ? 0 : y;
    cx1 = x + w > bx1 ? bx1 : x + w;
    cy1 = y + h > H ? H : y + h;
    if (cx1 < cx0) cx1 = cx0;
    if (cy1 < cy0) cy1 = cy0;
}

void Gfx::px(int x, int y, uint16_t c) {
    x += ox;
    y += oy;
    if (x < cx0 || x >= cx1 || y < cy0 || y >= cy1) return;
    int r = y - by0;
    if ((unsigned)r >= (unsigned)bh) return;
    if (mask < 16 && bayer(x, y) >= mask) return;
    buf[r * W + x] = c;
}
void Gfx::fill(int x, int y, int w, int h, uint16_t c) {
    x += ox;
    y += oy;
    int xa = x > cx0 ? x : cx0, xb = x + w < cx1 ? x + w : cx1;
    int ya = y > cy0 ? y : cy0, yb = y + h < cy1 ? y + h : cy1;
    if (ya < by0) ya = by0;
    if (yb > by0 + bh) yb = by0 + bh;
    if (xa >= xb || ya >= yb) return;
    for (int j = ya; j < yb; j++) {
        uint16_t *p = buf + (j - by0) * W;
        if (mask >= 16)
            for (int i = xa; i < xb; i++) p[i] = c;
        else
            for (int i = xa; i < xb; i++)
                if (bayer(i, j) < mask) p[i] = c;
    }
}

void Gfx::dfill(int x, int y, int w, int h, uint16_t c, int level) {
    if (level <= 0) return;
    if (level >= 16) {
        fill(x, y, w, h, c);
        return;
    }
    x += ox;
    y += oy;
    int xa = x > cx0 ? x : cx0, xb = x + w < cx1 ? x + w : cx1;
    int ya = y > cy0 ? y : cy0, yb = y + h < cy1 ? y + h : cy1;
    if (ya < by0) ya = by0;
    if (yb > by0 + bh) yb = by0 + bh;
    for (int j = ya; j < yb; j++) {
        uint16_t *p = buf + (j - by0) * W;
        for (int i = xa; i < xb; i++)
            if (bayer(i, j) < level && (mask >= 16 || bayer(i, j) < mask)) p[i] = c;
    }
}

void Gfx::checker(int x, int y, int w, int h, uint16_t c, int phase) {
    x += ox;
    y += oy;
    int xa = x > cx0 ? x : cx0, xb = x + w < cx1 ? x + w : cx1;
    int ya = y > cy0 ? y : cy0, yb = y + h < cy1 ? y + h : cy1;
    if (ya < by0) ya = by0;
    if (yb > by0 + bh) yb = by0 + bh;
    for (int j = ya; j < yb; j++) {
        uint16_t *p = buf + (j - by0) * W;
        for (int i = xa; i < xb; i++)
            if (((i + j + phase) & 1) == 0 && (mask >= 16 || bayer(i, j) < mask)) p[i] = c;
    }
}

void Gfx::rect(int x, int y, int w, int h, uint16_t c) {
    hline(x, y, w, c);
    hline(x, y + h - 1, w, c);
    vline(x, y, h, c);
    vline(x + w - 1, y, h, c);
}

void Gfx::panel(int x, int y, int w, int h, int32_t fillC, int32_t border, int cut) {
    if (fillC >= 0) {
        for (int j = 0; j < h; j++) {
            int d = j < cut ? cut - j : j >= h - cut ? cut - (h - 1 - j) : 0;
            fill(x + d, y + j, w - 2 * d, 1, (uint16_t)fillC);
        }
    }
    if (border >= 0) {
        uint16_t b = (uint16_t)border;
        hline(x + cut, y, w - 2 * cut, b);
        hline(x + cut, y + h - 1, w - 2 * cut, b);
        vline(x, y + cut, h - 2 * cut, b);
        vline(x + w - 1, y + cut, h - 2 * cut, b);
        for (int k = 1; k < cut; k++) {
            px(x + k, y + cut - k, b);
            px(x + w - 1 - k, y + cut - k, b);
            px(x + k, y + h - 1 - cut + k, b);
            px(x + w - 1 - k, y + h - 1 - cut + k, b);
        }
        if (cut > 0) {
            px(x + cut, y, b);
            px(x, y + cut, b);
        }
    }
}

void Gfx::rrect(int x, int y, int w, int h, uint16_t c, int r, bool filled) {
    if (filled) {
        fill(x + r, y, w - 2 * r, h, c);
        fill(x, y + r, r, h - 2 * r, c);
        fill(x + w - r, y + r, r, h - 2 * r, c);
    } else {
        hline(x + r, y, w - 2 * r, c);
        hline(x + r, y + h - 1, w - 2 * r, c);
        vline(x, y + r, h - 2 * r, c);
        vline(x + w - 1, y + r, h - 2 * r, c);
    }
    if (r == 2) {
        px(x + 1, y + 1, c);
        px(x + w - 2, y + 1, c);
        px(x + 1, y + h - 2, c);
        px(x + w - 2, y + h - 2, c);
    }
}

void Gfx::line(int x0, int y0, int x1, int y1, uint16_t c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void Gfx::dline(int x0, int y0, int x1, int y1, uint16_t c, int on, int off, int phase) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1), sy = y0 < y1 ? 1 : -1, err = dx + dy, n = phase;
    for (;;) {
        if ((n % (on + off)) < on) px(x0, y0, c);
        n++;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void Gfx::circle(int cx, int cy, int r, uint16_t c) {
    int x = r, y = 0, e = 1 - r;
    while (x >= y) {
        px(cx + x, cy + y, c);
        px(cx + y, cy + x, c);
        px(cx - y, cy + x, c);
        px(cx - x, cy + y, c);
        px(cx - x, cy - y, c);
        px(cx - y, cy - x, c);
        px(cx + y, cy - x, c);
        px(cx + x, cy - y, c);
        y++;
        if (e < 0) e += 2 * y + 1;
        else {
            x--;
            e += 2 * (y - x) + 1;
        }
    }
}

void Gfx::fillCircle(int cx, int cy, int r, uint16_t c) {
    for (int j = -r; j <= r; j++) {
        int w = ifloor(sqrtf((float)(r * r - j * j) + r * 0.8f));
        hline(cx - w, cy + j, 2 * w + 1, c);
    }
}

void Gfx::ring(float cxf, float cyf, float r0, float r1, float a0, float a1, uint16_t c, int level) {
    int cx = (int)cxf, cy = (int)cyf, R = iceil(r1);
    int ja = -R, jb = R;
    // skip rows outside the band
    int top = rowA(cy - R), bot = rowB(cy + R + 1);
    if (top > cy + ja) ja = top - cy;
    if (bot - 1 < cy + jb) jb = bot - 1 - cy;
    for (int j = ja; j <= jb; j++)
        for (int i = -R; i <= R; i++) {
            float d = sqrtf((float)(i * i + j * j));
            if (d < r0 || d > r1) continue;
            float a = atan2_((float)i, (float)-j) / 6.2831853f;
            if (a < 0) a += 1;
            if (a >= a0 && a <= a1) {
                if (level >= 16 || bayer(cx + i + ox, cy + j + oy) < level) px(cx + i, cy + j, c);
            }
        }
}

// ── sprites ─────────────────────────────────────────────────────────────────
// Sprite palette chars: the token each stands for, and the shade dim/far sprites
// draw it in (0 lo, 1 mid, 2 hi, 3 keep). x, X, Z can be overridden per draw.
static const char PAL_CH[] = "k01234wbaALsSegGYrRqxXZoOp";
static const uint8_t PAL_TOK[] = {T_ink,  T_line, T_line2, T_dim,  T_text, T_hi,   T_hi,    T_bg,    T_acc,
                                  T_accD, T_accL, T_sec,   T_secD, T_secL, T_gold, T_goldD, T_goldL, T_err,
                                  T_errD, T_errL, T_sec,   T_secL, T_secD, T_ok,   T_okD,   T_okL};
static const uint8_t PAL_SH[] = {3, 0, 0, 1, 2, 2, 2, 3, 1, 0, 2, 1, 0,
                                 2, 1, 0, 2, 1, 0, 2, 1, 2, 0, 1, 0, 2};
static uint16_t palColor(char ch, PalMode m, const uint16_t *x) {
    static const uint8_t SH[2][3] = {
        {T_line, T_line2, T_dim  },
        {T_bg3,  T_line,  T_line2}
    }; // dim, far: lo, mid, hi
    const char *f = ch ? strchr(PAL_CH, ch) : nullptr;
    int i = f ? f - PAL_CH : -1;
    if (m == PM_FULL) {
        if (i < 0) return C(T_text);
        if (ch == 'w') return 0xFFFF;
        if (x && i >= 20 && i <= 22) return x[i - 20];
        return C((Tok)PAL_TOK[i]);
    }
    int sh = i < 0 ? 1 : PAL_SH[i];
    return sh == 3 ? C((Tok)PAL_TOK[i]) : C((Tok)SH[m == PM_FAR][sh]);
}

void blit(Gfx &g, const SpriteDef &s, int x, int y, PalMode m, const uint16_t *xXZ) {
    uint16_t pal[16];
    for (int i = 0; i < s.npal; i++) pal[i + 1] = palColor(s.pal[i], m, xXZ);
    int ja = g.rowA(y) - y, jb = g.rowB(y + s.h) - y;
    if (jb > s.h) jb = s.h;
    // runs of (length - 1) << 4 | colour index, row-major
    const uint8_t *p = s.px;
    for (int i = 0, j = 0; j < jb;) {
        uint8_t v = *p++, c = v & 15;
        for (int r = (v >> 4) + 1; r--;) {
            if (c && j >= ja) g.px(x + i, y + j, pal[c]);
            if (++i == s.w) {
                i = 0;
                j++;
            }
        }
    }
}

void glyph(Gfx &g, uint8_t id, int x, int y, uint16_t c, int32_t c2) {
    if (id >= G_COUNT) return;
    int w = glyph_w[id], h = glyph_h[id], o = glyph_off[id];
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            int b = o + j * w + i;
            int v = (glyph_bits[b >> 2] >> ((b & 3) * 2)) & 3;
            if (v == 1) g.px(x + i, y + j, c);
            else if (v == 2) g.px(x + i, y + j, c2 >= 0 ? (uint16_t)c2 : c);
        }
}

// ── text ────────────────────────────────────────────────────────────────────
static uint32_t nextCp(const char *&s) {
    uint8_t c = (uint8_t)*s++;
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0 && *s) {
        uint32_t cp = ((c & 0x1F) << 6) | ((uint8_t)*s++ & 0x3F);
        return cp;
    }
    if ((c & 0xF0) == 0xE0 && s[0] && s[1]) {
        uint32_t cp = ((c & 0x0F) << 12) | (((uint8_t)s[0] & 0x3F) << 6) | ((uint8_t)s[1] & 0x3F);
        s += 2;
        return cp;
    }
    return '?';
}
// glyph index for a code point, 255 = space
static uint8_t glyphIndex(const FontDef &f, uint32_t cp) {
    if (cp >= 32 && cp < 128) return f.ascii[cp - 32];
    int lo = 0, hi = f.nspecial - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (f.specialCp[mid] == cp) return f.specialIdx[mid];
        if (f.specialCp[mid] < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return f.ascii['?' - 32];
}

int textW(uint8_t font, const char *s, int sp) {
    const FontDef &f = *FONTS[font];
    if (sp < 0) sp = f.sp;
    int w = 0;
    bool any = false;
    while (*s) {
        uint32_t cp = nextCp(s);
        uint8_t gi = glyphIndex(f, cp);
        w += (gi == 255 ? f.space : f.widths[gi]) + sp;
        any = true;
    }
    return any ? w - sp : 0;
}

static void drawRun(Gfx &g, const FontDef &f, const char *s, int x, int y, uint16_t c, int sp, int sc) {
    int cx = x;
    while (*s) {
        uint32_t cp = nextCp(s);
        uint8_t gi = glyphIndex(f, cp);
        if (gi == 255) {
            cx += (f.space + sp) * sc;
            continue;
        }
        int w = f.widths[gi], o = f.offs[gi];
        for (int j = 0; j < f.h; j++)
            for (int i = 0; i < w; i++) {
                int b = o + j * w + i;
                if (f.bits[b >> 3] & (1 << (b & 7))) {
                    if (sc == 1) g.px(cx + i, y + j, c);
                    else g.fill(cx + i * sc, y + j * sc, sc, sc, c);
                }
            }
        cx += (w + sp) * sc;
    }
}

void fitText(uint8_t font, const char *s, int maxW, char *out, int outLen) {
    int n = (int)strlen(s);
    if (n >= outLen) n = outLen - 1;
    memcpy(out, s, n);
    out[n] = 0;
    if (textW(font, out) <= maxW) return;
    // drop whole UTF-8 characters from the end until "text…" fits
    while (n > 0) {
        do { n--; } while (n > 0 && ((uint8_t)out[n] & 0xC0) == 0x80);
        while (n > 0 && out[n - 1] == ' ') n--;
        if (n + 4 > outLen) continue;
        memcpy(out + n, "\xE2\x80\xA6", 4); // …
        if (textW(font, out) <= maxW) return;
    }
    memcpy(out, "\xE2\x80\xA6", 4);
}

int text(Gfx &g, uint8_t font, const char *s, int x, int y, uint16_t c, TextOpt o) {
    const FontDef &f = *FONTS[font];
    int sp = f.sp, sc = o & T_BIG ? 2 : 1, max = o >> 2 & 255;
    char tmp[160];
    if (max) {
        fitText(font, s, max / sc, tmp, sizeof(tmp));
        s = tmp;
    }
    int w = textW(font, s, sp) * sc;
    if ((o & 3) == AL_C) x = ifloor(x - w / 2.0f + 0.5f);
    else if ((o & 3) == AL_R) x -= w;
    if (o >> 16) {
        static const int8_t d[8][2] = {
            {-1, 0 },
            {1,  0 },
            {0,  -1},
            {0,  1 },
            {-1, -1},
            {1,  1 },
            {-1, 1 },
            {1,  -1}
        };
        for (auto &k : d) drawRun(g, f, s, x + k[0], y + k[1], PAL[(o >> 16) - 1], sp, sc);
    }
    drawRun(g, f, s, x, y, c, sp, sc);
    return w;
}

int wrapText(uint8_t font, const char *s, int maxW, char lines[][64], int maxLines) {
    int n = 0;
    char line[64] = "";
    const char *p = s;
    while (*p && n < maxLines) {
        // next word
        char word[64];
        int wl = 0;
        while (*p == ' ') p++;
        while (*p && *p != ' ' && *p != '\n' && wl < 63) word[wl++] = *p++;
        word[wl] = 0;
        char cand[130];
        snprintf(cand, sizeof(cand), "%s%s%s", line, line[0] && wl ? " " : "", word);
        if (textW(font, cand) <= maxW || !line[0]) {
            strncpy(line, cand, 63);
            line[63] = 0;
        } else {
            strcpy(lines[n++], line);
            strncpy(line, word, 63);
            line[63] = 0;
        }
        if (*p == '\n') {
            p++;
            if (n < maxLines) strcpy(lines[n++], line);
            line[0] = 0;
        }
    }
    if (line[0] && n < maxLines) strcpy(lines[n++], line);
    return n;
}

bool emblemBit(const char *seed, int i, int j) {
    uint32_t h = fnv(seed);
    static const uint8_t mirror[5] = {0, 1, 2, 1, 0};
    return (h >> (j * 3 + mirror[i])) & 1;
}

void emblem(Gfx &g, const char *seed, int x, int y, int cell, uint16_t on, int32_t off) {
    for (int j = 0; j < 5; j++)
        for (int i = 0; i < 5; i++) {
            bool b = emblemBit(seed, i, j);
            if (b) g.fill(x + i * cell, y + j * cell, cell, cell, on);
            else if (off >= 0) g.fill(x + i * cell, y + j * cell, cell, cell, (uint16_t)off);
        }
}

} // namespace orb
#endif
