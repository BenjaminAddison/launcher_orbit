#if defined(ORBIT_UI) || defined(ORBIT_NATIVE)
#include "orbit_screens.h"
#include <math.h>
#include <stdio.h>

namespace orb {

int jsround(float v) { return ifloor(v + 0.5f); }
float Mulberry::next() {
    a += 0x6D2B79F5u;
    uint32_t t = (a ^ (a >> 15)) * (1u | a);
    t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t;
    return (float)((t ^ (t >> 14)) >> 8) * (1.0f / 16777216);
}

char installed[10][18];
int ninstalled = 0;
uint8_t famOf(const char *label) {
    for (int i = 0; i < ninstalled; i++)
        if (!strcmp(installed[i], label)) return i % 4;
    char tmp[40];
    snprintf(tmp, sizeof(tmp), "%s#", label);
    return fnv(tmp) % 4;
}
uint16_t famC(uint8_t fam, int shade) {
    static const uint8_t T[4][3] = {
        {T_sec,  T_secL,  T_secD },
        {T_gold, T_goldL, T_goldD},
        {T_ok,   T_okL,   T_okD  },
        {T_acc,  T_accL,  T_accD }
    };
    return C((Tok)T[fam & 3][shade]);
}

// ── sky, stars, planet ──────────────────────────────────────────────────────
void drawSky(Gfx &g, int y0, int y1) {
    g.clear(C(T_sky0));
    for (int y = y0; y < y1; y++)
        g.dfill(0, y, W, 1, C(T_sky1), jsround(clampf((float)(y - y0) / (y1 - y0), 0, 1) * 16));
}

struct Star {
    uint8_t x, y, b;
    float ph, sp;
};
static Star STARS[70];
static bool starsReady = false;
static void initStars() {
    Mulberry r{7};
    for (auto &s : STARS) {
        s.x = (uint8_t)ifloor(r.next() * W);
        s.y = (uint8_t)ifloor(r.next() * 100);
        s.b = r.next() < 0.12f ? 2 : r.next() < 0.45f ? 1 : 0;
        s.ph = (float)(r.next() * 6.28f);
        s.sp = (float)(0.6f + r.next() * 2.2f);
    }
    starsReady = true;
}
void drawStars(Gfx &g, float t, int maxY, float drift, int count) {
    if (!starsReady) initStars();
    for (int i = 0; i < count; i++) {
        const Star &s = STARS[i];
        if (s.y >= maxY) continue;
        int x = ifloor(fmodp(fmodp(s.x - t * drift * (s.b + 1), W) + W, W));
        float tw = fsin(t * s.sp + s.ph);
        uint16_t c = s.b == 2 ? C(T_star) : s.b == 1 ? C(T_dim) : C(T_line2);
        if (tw < -0.75f) c = s.b == 2 ? C(T_dim) : C(T_line);
        g.px(x, s.y, c);
        if (s.b == 2 && tw > 0.8f) {
            g.px(x - 1, s.y, C(T_line2));
            g.px(x + 1, s.y, C(T_line2));
            g.px(x, s.y - 1, C(T_line2));
            g.px(x, s.y + 1, C(T_line2));
        }
    }
}

void drawStarsIn(Gfx &g, int x, int y, int w, int h) {
    if (!starsReady) initStars();
    for (int i = 0; i < 26; i++)
        g.px(x + STARS[i].x % w, y + STARS[i].y % h, STARS[i].b ? C(T_dim) : C(T_line2));
}

// Night side of a planet with the sun about to rise on its limb. Per pixel, but
// only over the rows and columns that can show it.
void drawPlanet(Gfx &g, float t, const PlanetP &P) {
    const float cx = P.cx, cy = P.cy, R = P.R, sunX = P.sunX;
    int top = ifloor(cy - R - 14);
    if (top < 0) top = 0;
    int ya = g.rowA(top), yb = g.rowB(H);
    int clx0 = g.cx0 - g.ox, clx1 = g.cx1 - g.ox;
    if (clx0 < 0) clx0 = 0;
    if (clx1 > W) clx1 = W;
    const float rot = t * 3.5f, Rg = R + 12.5f;
    // sun proximity only depends on the column
    static float sunS[W];
    for (int x = clx0; x < clx1; x++) {
        float q = (x - sunX) / 52;
        sunS[x] = expNeg(q * q);
    }
    const uint16_t cAtm = C(T_atm), cSky2 = C(T_sky2), cHi = C(T_hi), cPl0 = C(T_pl0), cPl1 = C(T_pl1),
                   cPl2 = C(T_pl2), cPl3 = C(T_pl3), cPl4 = C(T_pl4), cCity = C(T_city);
    for (int y = ya; y < yb; y++) {
        float dy = y + .5f - cy, span2 = Rg * Rg - dy * dy;
        if (span2 <= 0) continue;
        float span = sqrtf(span2);
        int xa = ifloor(cx - span - 1), xb = iceil(cx + span + 1);
        if (xa < clx0) xa = clx0;
        if (xb > clx1) xb = clx1;
        for (int x = xa; x < xb; x++) {
            float dx = x + .5f - cx, d = sqrtf(dx * dx + dy * dy), depth = R - d;
            float s = sunS[x];
            int b = bayer(x, y);
            if (depth < 0) {
                float gw = 2.5f + 10 * s, f = 1 + depth / gw;
                if (f <= 0) continue;
                int lvl = jsround(f * f * (8 + 8 * s));
                if (b < lvl) g.px(x, y, f > 0.72f && s > 0.35f ? cAtm : cSky2);
                continue;
            }
            if (depth < 1) {
                g.px(x, y, s > 0.55f ? cHi : cAtm);
                continue;
            }
            if (depth < 4 + 3 * s) {
                float f = 1 - (depth - 1) / (3 + 3 * s);
                int lvl = jsround(f * (10 + 6 * s));
                g.px(x, y, b < lvl ? (s > 0.4f ? cPl4 : cPl3) : cPl2);
                continue;
            }
            float v = sqrtf(depth) * 2.2f,
                  u = (dx * (1.2f - (depth / 60 < 1 ? depth / 60 : 1) * 0.4f) + rot) * 0.06f;
            float n = vnoise(u, v * 0.9f) * 0.65f + vnoise(u * 2.7f, v * 2.1f) * 0.35f;
            bool land = n > 0.53f;
            uint16_t c = land ? cPl1 : cPl0;
            if (n > 0.5f && n < 0.56f && b < 8) c = cPl1;
            if (depth < 9 && b < jsround((1 - (depth - 4) / 5) * 8)) c = cPl2;
            if (land && n > 0.6f) {
                float hh = hash2(ifloor(u * 9), ifloor(v * 3));
                if (hh < 0.09f && ((x + y) & 1) == 0) c = fsin(t * 3 + hh * 99) > -0.6f ? cCity : cPl2;
            }
            g.px(x, y, c);
        }
    }
}

void drawEmblem(Gfx &g, const char *seed, int x, int y, int cell, uint16_t on, int32_t off) {
    emblem(g, seed, x, y, cell, on, off);
}

// ── home ────────────────────────────────────────────────────────────────────
bool HomeScene::disabled(int i) const {
    uint8_t id = tiles[i].id;
    return (id == TL_SD || id == TL_USB) && !ST.sd;
}
void HomeScene::setSel(int i) {
    sel = i < 0 ? 0 : i >= n ? n - 1 : i;
    pos = sel;
}
void HomeScene::select(int i) {
    int was = sel;
    sel = (i + n) % n;
    if (sel != was) lockT = 0;
}
void HomeScene::key(uint8_t k, char ch) {
    if (!n) return;
    if (k == K_RIGHT) {
        if (sel == n - 1 && !devMode) {
            passBy++;
            if (passBy >= 5) {
                devMode = true;
                if (onDevMode) onDevMode();
                toast("Dev mode activated", TK_OK);
            }
        }
        select(sel + 1);
    } else if (k == K_LEFT) {
        passBy = 0;
        select(sel - 1);
    } else if (k == K_DOWN || k == K_UP) {
        uint8_t groups[8];
        int ng = 0, gi = 0;
        for (int i = 0; i < n; i++) {
            bool seen = false;
            for (int j = 0; j < ng; j++) seen |= groups[j] == tiles[i].group;
            if (!seen && ng < 8) groups[ng++] = tiles[i].group;
        }
        for (int j = 0; j < ng; j++)
            if (groups[j] == tiles[sel].group) gi = j;
        gi = (gi + (k == K_DOWN ? 1 : -1) + ng) % ng;
        for (int i = 0; i < n; i++)
            if (tiles[i].group == groups[gi]) {
                select(i);
                pos = sel;
                lockT = 0;
                break;
            }
    } else if (k == K_OK || k == K_HOLD) {
        if (disabled(sel)) {
            denyT = 0;
            shake = 0.12f;
            toast(tiles[sel].id == TL_SD ? "Insert an SD card" : "Insert an SD card first", TK_ERR);
            return;
        }
        res = sel;
    } else if (k == K_CHAR && shortcuts && ST.sd) {
        char c = ch >= 'A' && ch <= 'Z' ? ch + 32 : ch;
        if (c > ' ') res = 1000 + (uint8_t)c;
    }
}
void HomeScene::update(float dt, bool) {
    lockT += dt;
    denyT += dt;
    if (!n) return;
    float d = sel - pos;
    if (d > n / 2.0f) d -= n;
    if (d < -n / 2.0f) d += n;
    pos += d * (dt * 11 < 1 ? dt * 11 : 1);
    if (fabsf(d) < 0.002f) pos = sel;
    pos = fmodp(pos + n, n);
}
void HomeScene::meta(int i, char *out, uint16_t &c) {
    const Tile &tl = tiles[i];
    c = tl.bootsNext ? C(T_acc) : C(T_sec);
    switch (tl.id) {
        case TL_SD:
            strcpy(out, ST.sd ? "CARD READY" : "NO CARD INSERTED");
            c = ST.sd ? C(T_ok) : C(T_err);
            return;
        case TL_USB:
            strcpy(out, ST.sd ? "MASS STORAGE OVER USB-C" : "NEEDS AN SD CARD");
            c = ST.sd ? C(T_sec) : C(T_err);
            return;
        case TL_CHG:
            sprintf(
                out, "BATTERY %d%%%s", ST.battery, ST.charging ? " \xC2\xB7 CHARGING" : " \xC2\xB7 ON BATTERY"
            );
            c = ST.charging ? C(T_ok) : C(T_sec);
            return;
    }
    strcpy(out, tl.meta);
}
void HomeScene::drawIcon(Gfx &g, int i, int x, int y, int mode) {
    const Tile &tl = tiles[i];
    bool dis = disabled(i);
    if (dis && mode == PM_FULL) mode = PM_DIM;
    if (tl.id == TL_APP) {
        uint16_t pal[3] = {famC(tl.fam, 0), famC(tl.fam, 1), famC(tl.fam, 2)};
        blit(g, ICO_APP, x, y, (PalMode)mode, pal);
        text(
            g,
            F_BODY,
            tl.ini,
            x + 16,
            y + 12,
            mode == PM_FULL  ? C(T_ink)
            : mode == PM_DIM ? C(T_line)
                             : C(T_bg3),
            TA(AL_C)
        );
        if (mode == PM_FULL && tl.bootsNext) g.fill(x + 22, y + 9, 2, 2, C(T_ink));
    } else {
        static const SpriteDef *const IC[] = {
            &ICO_SD, &ICO_OTA, &ICO_WUI, &ICO_USB, &ICO_PMAN, &ICO_CFG, &ICO_APP, &ICO_CHG, &ICO_OFF
        };
        blit(g, *IC[tl.id], x, y, (PalMode)mode);
    }
    if (dis && mode != PM_FAR) {
        g.fill(x + 24, y + 1, 7, 7, C(T_err));
        glyph(g, G_CROSS, x + 25, y + 2, C(T_ink));
    }
}
void HomeScene::draw(Gfx &g) {
    const float tt = now;
    drawSky(g);
    drawStars(g, tt, 104);
    // satellite drifting across the sky every ~38 s
    float sp = fmodp(tt + 6, 38) / 38;
    int sx = jsround(-12 + sp * 264), sy = jsround(18 + fsin(sp * 3.14159265f) * -6 + sp * 10);
    blit(g, SPR_SAT, sx, sy, PM_DIM);
    if (ifloor(tt * 2) % 3 == 0) g.px(sx + 4, sy - 1, C(T_err));
    // shooting star
    float st = fmodp(tt + 3, 11);
    if (st < 0.5f) {
        float k = st / 0.5f, x0 = 200 - k * 70, y0 = 8 + k * 26;
        for (int i = 0; i < 7; i++)
            g.px(
                jsround(x0 + i * 2),
                jsround(y0 - i * 0.75f),
                i < 2   ? C(T_hi)
                : i < 4 ? C(T_dim)
                        : C(T_line2)
            );
    }
    PlanetP P{120, 320, 220, 176 + fsin(tt * 0.2f) * 4};
    drawPlanet(g, tt, P);
    // flare on the limb
    int fx = 176 + jsround(fsin(tt * 0.2f) * 4);
    float ddx = fx + .5f - 120;
    int fy = jsround(320 - sqrtf(220 * 220 - ddx * ddx)) - 1, fl = 3 + jsround((fsin(tt * 1.7f) + 1) * 2);
    g.hline(fx - fl * 2, fy, fl * 4 + 1, C(T_atm));
    g.hline(fx - fl, fy, fl * 2 + 1, C(T_hi));
    g.vline(fx, fy - 2, 5, C(T_hi));
    g.px(fx, fy - 3, C(T_atm));

    // header, transparent over the sky
    glyph(g, G_ROCKET, 4, 1, C(T_acc), C(T_sec));
    int hx = 14 + text(g, F_MICRO, "LAUNCHER", 14, 3, C(T_text));
    text(g, F_MICRO, version, hx + 4, 3, C(T_dim));
    int met = ifloor(tt + metBase);
    char clk[16];
    sprintf(clk, "T+%02d:%02d:%02d", (met / 3600) % 100, (met / 60) % 60, met % 60);
    int left = statusCluster(g, W - 3, 2);
    text(g, F_MICRO, clk, left - 6, 3, C(T_line2), TA(AL_R));

    // the orbit the modules ride on
    int ph = ifloor(tt * 6);
    for (int x = 0; x < W; x++) {
        float o = fabsf((x - 120) / 60.0f);
        int y = jsround(40 + o * sqrtf(o) * 6.5f);
        int k = (x + ph) % 6;
        if (k == 0) g.px(x, y, C(T_dim));
        else if (k == 3) g.px(x, y, C(T_line2));
    }
    if (!n) return;
    // carousel, far icons first
    struct Slot {
        int k;
        float o;
    } slots[8];
    int ns = 0;
    for (int k = 0; k < n && ns < 8; k++) {
        float o = k - pos;
        if (o > n / 2.0f) o -= n;
        if (o < -n / 2.0f) o += n;
        if (fabsf(o) <= 2.6f) slots[ns++] = {k, o};
    }
    for (int i = 1; i < ns; i++)
        for (int j = i; j > 0 && fabsf(slots[j].o) > fabsf(slots[j - 1].o); j--) {
            Slot tmp = slots[j];
            slots[j] = slots[j - 1];
            slots[j - 1] = tmp;
        }
    float dsel = fabsf(pos - sel);
    bool settled = dsel < 0.02f || fabsf(dsel - n) < 0.02f;
    int bob = settled ? jsround(fsin(tt * 2.4f)) : 0;
    for (int i = 0; i < ns; i++) {
        float a = fabsf(slots[i].o);
        int x = jsround(120 + slots[i].o * 60 - 16), y = jsround(24 + a * sqrtf(a) * 6.5f);
        int mode = a < 0.5f ? PM_FULL : a < 1.6f ? PM_DIM : PM_FAR;
        int yy = y + (mode == PM_FULL ? bob : 0);
        int dx = slots[i].k == sel && denyT < 0.25f ? jsround(fsin(denyT * 60) * 2) : 0;
        drawIcon(g, slots[i].k, x + dx, yy, mode);
    }
    // lock-on reticle
    float lk = clampf(lockT / 0.32f, 0, 1);
    int r = jsround(11 + (3 - 11) * easeBack(lk));
    bool blink = lockT < 0.3f && ifloor(lockT / 0.06f) % 2 == 1;
    if (!blink) {
        int bx = 104 - r, by = 24 - r + bob, bw = 32 + 2 * r;
        corners(g, bx, by, bw, bw, disabled(sel) ? C(T_err) : C(T_acc), 4);
        g.px(bx + bw / 2, by - 2, C(T_accD));
        g.px(bx + bw / 2, by + bw + 1, C(T_accD));
        char cnt[16];
        sprintf(cnt, "%02d/%02d", sel + 1, n);
        text(g, F_MICRO, cnt, bx + bw + 1, by - 6, C(T_dim));
    }
    // title, with a short decode effect after each move
    const Tile &tl = tiles[sel];
    char title[22];
    strcpy(title, tl.title);
    if (lockT < 0.22f) {
        static const char GL[] = "ABCDEFGHJKLMNPRSTUVXYZ0123456789";
        int f30 = ifloor(tt * 30);
        for (int i = 0; title[i]; i++)
            if (title[i] != ' ' && lockT < 0.04f + i * 0.018f) title[i] = GL[ifloor(hash2(i, f30) * 32)];
    }
    text(g, F_ORBIT, title, 120, 64, C(T_hi), TA(AL_C));
    text(g, F_BODY, tl.desc, 120, 77, C(T_dim), TA(AL_C));
    char mt[48];
    uint16_t mc;
    meta(sel, mt, mc);
    text(g, F_MICRO, mt, 120, 89, mc, TA(AL_C));
    // group dots above the limb
    int dx = 120 - jsround((n * 4 - 2) / 2.0f);
    for (int k = 0; k < n; k++) {
        uint8_t gr = tiles[k].group;
        if (k > 0 && tiles[k - 1].group != gr) dx += 3;
        if (k == sel) {
            g.fill(dx - 1, 96, 4, 1, C(T_acc));
            g.px(dx, 95, C(T_accL));
        } else g.px(dx, 96, gr == 1 ? C(T_sec) : C(T_dim));
        dx += 4;
    }
    // hints over the night side
    hintsOver(
        g, "\xE2\x97\x80\xE2\x96\xB6|MOVE|\xE2\x96\xB2\xE2\x96\xBC|GROUP|\xE2\x8F\x8E|OPEN", 126, T_pl0
    );
    if (shortcuts && ST.sd) {
        text(g, F_MICRO, "A-Z SHORTCUTS", W - 4, 127, C(T_dim), TOUT(TA(AL_R), T_pl0));
    }
}

// ── charge ──────────────────────────────────────────────────────────────────
void ChargeScene::key(uint8_t k, char) {
    if (k == K_OK || k == K_BACK) res = 0;
}
void ChargeScene::draw(Gfx &g) {
    const float tt = t;
    g.clear(0);
    const int x = 78, y = 40, w = 74, h = 34, pct = ST.battery;
    g.rect(x, y, w, h, C(T_line2));
    g.rect(x + 1, y + 1, w - 2, h - 2, C(T_line));
    g.fill(x + w, y + 11, 4, 12, C(T_line2));
    int fw = jsround((w - 6) * pct / 100.0f);
    g.fill(x + 3, y + 3, fw, h - 6, C(T_okD));
    g.fill(x + 3, y + 3, fw, 2, C(T_ok));
    for (int k = 0; k < 9; k++) {
        char seed[4] = {'b', (char)('0' + k), 0};
        int bx = x + 6 + (int)(fnv(seed) % (uint32_t)(fw - 6 > 1 ? fw - 6 : 1));
        float ph = fmodp(tt * (0.4f + (k % 3) * 0.2f) + k / 9.0f, 1);
        int by = jsround(y + h - 5 - ph * (h - 9));
        g.px(bx, by, C(T_ok));
        if (k % 2) g.px(bx + 1, by - 1, C(T_okL));
    }
    if (ST.charging) {
        static const char *const B[10] = {
            "......###",
            ".....###.",
            "....###..",
            "...###...",
            "..#######",
            "....###..",
            "...###...",
            "..###....",
            ".###.....",
            "###......"
        };
        bool on = ifloor(tt * 1.5f) % 2 == 0;
        int bx = x + w / 2 - 6, by = y + 7;
        for (int j = 0; j < 10; j++)
            for (int i = 0; B[j][i]; i++)
                if (B[j][i] == '#') {
                    g.px(bx + i, by + j * 2, on ? C(T_hi) : C(T_okL));
                    g.px(bx + i, by + j * 2 + 1, on ? C(T_hi) : C(T_okL));
                }
    }
    char s[48];
    sprintf(s, "%d%%", pct);
    text(g, F_ORBIT, s, 120, y + h + 10, C(T_text), TA(AL_C));
    if (volts > 0)
        sprintf(
            s,
            "%s \xC2\xB7 %d.%02d V \xC2\xB7 SCREEN 25%%",
            ST.charging ? "CHARGING" : "ON BATTERY",
            (int)volts,
            (int)(volts * 100) % 100
        );
    else sprintf(s, "%s \xC2\xB7 SCREEN 25%%", ST.charging ? "CHARGING" : "ON BATTERY");
    text(g, F_MICRO, s, 120, y + h + 24, C(T_dim), TA(AL_C));
    text(g, F_MICRO, "CPU 80 MHZ", 120, 18, C(T_line2), TA(AL_C));
    if (ifloor(tt * 0.5f) % 2 == 0) text(g, F_MICRO, "\xE2\x8F\x8E EXIT", 120, 122, C(T_line2), TA(AL_C));
}

// ── power off: the picture collapses like an old CRT ───────────────────────
void PowerOffScene::update(float, bool) {
    if (t > 0.9f && res == RUNNING) res = 0;
}
void PowerOffScene::draw(Gfx &g) {
    // the scene below has been drawn already (this is an overlay)
    if (t < 0.45f && snap) {
        float v = t < 0.3f ? 1 - easeIn(t / 0.3f) : 0, hs = t < 0.3f ? 1 : 1 - easeIn((t - 0.3f) / 0.15f);
        int hh = jsround(H * v), ww = jsround(W * hs);
        if (hh < 1) hh = 1;
        if (ww < 1) ww = 1;
        int y0 = jsround((H - hh) / 2.0f), x0 = jsround((W - ww) / 2.0f);
        g.clear(0);
        for (int y = g.rowA(y0); y < g.rowB(y0 + hh); y++) {
            int sy = ifloor((float)(y - y0) / hh * H);
            uint16_t *row = g.buf + (y - g.by0) * W;
            for (int x = x0; x < x0 + ww; x++)
                row[x] = hh < 3 ? 0xFFFF : snap[sy * W + ifloor((float)(x - x0) / ww * W)];
        }
        return;
    }
    if (t < 0.3f) { // no snapshot memory: crop instead of squash
        float v = 1 - easeIn(t / 0.3f);
        int hh = jsround(H * v);
        if (hh < 1) hh = 1;
        int y0 = (H - hh) / 2;
        g.fill(0, 0, W, y0, 0);
        g.fill(0, y0 + hh, W, H - y0 - hh, 0);
        if (hh < 3) g.fill(0, y0, W, hh, 0xFFFF);
        return;
    }
    g.clear(0);
    if (t < 0.45f) {
        int ww = jsround(W * (1 - easeIn((t - 0.3f) / 0.15f)));
        g.hline((W - (ww < 1 ? 1 : ww)) / 2, 67, ww < 1 ? 1 : ww, 0xFFFF);
    } else {
        float f = (t - 0.45f) / 0.45f;
        if (f < 0.6f) g.px(120, 67, f < 0.3f ? 0xFFFF : 0x8C51);
    }
}

} // namespace orb
#endif
