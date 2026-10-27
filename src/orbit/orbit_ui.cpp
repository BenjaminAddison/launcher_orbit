#if defined(ORBIT_UI) || defined(ORBIT_NATIVE)
#include "orbit_ui.h"
#include <math.h>
#include <stdio.h>

namespace orb {

Status ST;
float now = 0, shake = 0;

// ── kit ─────────────────────────────────────────────────────────────────────
void fmtSize(char *out, uint32_t b) {
    if (b >= 10485760) sprintf(out, "%dMB", (int)(b / 1048576.0f + 0.5f));
    else if (b >= 1048576) {
        int tenths = (int)(b / 104857.6f + 0.5f);
        sprintf(out, "%d.%dMB", tenths / 10, tenths % 10);
    } else if (b >= 1024) sprintf(out, "%dKB", (int)(b / 1024.0f + 0.5f));
    else sprintf(out, "%dB", (int)b);
}
void hex32(char *out, uint32_t v) { sprintf(out, "0x%X", (unsigned)v); }

int statusCluster(Gfx &g, int xr, int y) {
    int x = xr - 11;
    char ps[6];
    if (ST.hasBattery) {
        int pct = ST.battery;
        bool low = pct <= 15;
        uint16_t bc = low ? C(T_err) : C(T_dim);
        g.rect(x, y, 10, 6, bc);
        g.fill(x + 10, y + 2, 1, 2, bc);
        int fillW = ifloor(8 * pct / 100.0f + 0.5f);
        if (ST.charging) {
            int k = ifloor(now * 3) % 5;
            int f2 = fillW + k;
            if (f2 > 8) f2 = 8;
            if (f2 > fillW) fillW = f2;
        }
        g.fill(x + 1, y + 1, fillW, 4, low ? C(T_err) : ST.charging ? C(T_ok) : C(T_text));
        if (ST.charging) {
            int bx = x + 3;
            glyph(g, G_BOLT, bx, y, C(T_ink));
            g.px(bx + 2, y + 2, C(T_hi));
        }
        sprintf(ps, "%d%%", pct);
        int pw = textW(F_MICRO, ps);
        x -= pw + 2;
        text(g, F_MICRO, ps, x, y + 1, low ? C(T_err) : C(T_dim));
    } else x += 11;
    if (ST.wifi) {
        x -= 9;
        glyph(g, G_WIFI, x, y, C(T_text));
    }
    if (ST.sd) {
        x -= 7;
        glyph(g, G_SD, x, y - 1, C(T_text));
    }
    return x;
}

void header(Gfx &g, const Hdr &o) {
    g.fill(0, 0, W, HDR_H, o.bg >= 0 ? (uint16_t)o.bg : C(T_bg));
    int x = 4;
    if (o.icon != 255) {
        glyph(g, o.icon, x, 2, C(T_acc), C(T_sec));
        x += glyphW(o.icon) + 4;
    }
    x += text(g, F_MICRO, o.title, x, 3, C(T_text));
    int tabEnd = x + 3;
    if (o.crumb && *o.crumb) {
        x += 3;
        text(g, F_MICRO, "/", x, 3, C(T_line2));
        x += 5;
        text(g, F_MICRO, o.crumb, x, 3, C(T_dim), TA(AL_L, o.crumbMax));
    }
    g.hline(0, HDR_H - 1, W, C(T_line));
    g.hline(2, HDR_H - 1, tabEnd - 2, C(T_acc));
    uint16_t rc = o.rightC >= 0 ? (uint16_t)o.rightC : C(T_dim);
    if (o.status) {
        int left = statusCluster(g, W - 3, 2);
        if (o.right && *o.right) text(g, F_MICRO, o.right, left - 5, 3, rc, TA(AL_R));
    } else if (o.right && *o.right) text(g, F_MICRO, o.right, W - 4, 3, rc, TA(AL_R));
}

int keycap(Gfx &g, const char *k, int x, int y, bool hot) {
    int w = textW(F_MICRO, k) + 4;
    g.rrect(x, y, w, 7, hot ? C(T_acc) : C(T_bg3), 1);
    text(g, F_MICRO, k, x + 2, y + 1, hot ? C(T_onAcc) : C(T_text));
    return w;
}

// splits "A|B|C|D" into pairs; calls back with each pair
static int splitHints(const char *items, char parts[][24], int maxParts) {
    int n = 0, l = 0;
    if (!items) return 0;
    for (const char *p = items;; p++) {
        if (*p == '|' || !*p) {
            parts[n][l] = 0;
            n++;
            l = 0;
            if (!*p || n >= maxParts) break;
        } else if (l < 23) parts[n][l++] = *p;
    }
    return n;
}

static void drawHintRow(Gfx &g, const char *items, int x, int y, int outline) {
    char parts[10][24];
    int n = splitHints(items, parts, 10);
    TextOpt o = outline >= 0 ? TOUT(0, (Tok)outline) : 0;
    for (int i = 0; i + 1 < n; i += 2) {
        x += keycap(g, parts[i], x, y) + 3;
        x += text(g, F_MICRO, parts[i + 1], x, y + 1, C(T_dim), o) + (outline >= 0 ? 6 : 7);
    }
}

void hints(Gfx &g, const char *items, const char *right, int32_t rightC, int y, bool bg) {
    g.hline(0, FOOT_Y, W, C(T_line));
    if (bg) g.fill(0, FOOT_Y + 1, W, H - FOOT_Y - 1, C(T_bg));
    drawHintRow(g, items, 4, y, -1);
    if (right && *right)
        text(g, F_MICRO, right, W - 4, y + 1, rightC >= 0 ? (uint16_t)rightC : C(T_dim), TA(AL_R));
}

void hintsOver(Gfx &g, const char *items, int y, int outline, int x) { drawHintRow(g, items, x, y, outline); }

void toggle(Gfx &g, int x, int y, bool on, bool sel, float anim) {
    uint16_t fg = sel ? C(T_onAcc) : on ? C(T_acc) : C(T_line2);
    g.rrect(x, y, 13, 7, fg, 2, on);
    int kx = x + 1 + ifloor(anim * 6 + 0.5f);
    g.fill(kx + 1, y + 2, 3, 3, on ? (sel ? C(T_acc) : C(T_onAcc)) : (sel ? C(T_onAcc) : C(T_dim)));
}

void miniBar(Gfx &g, int x, int y, int w, float frac, bool sel) {
    g.fill(x, y, w, 3, sel ? C(T_accD) : C(T_line));
    g.fill(x, y, ifloor(w * clampf(frac, 0, 1) + 0.5f), 3, sel ? C(T_onAcc) : C(T_acc));
}

void segbar(
    Gfx &g, int x, int y, int w, int h, float frac, int32_t color, int32_t lead, int32_t empty, int seg
) {
    const int gap = 1, n = (w + gap) / (seg + gap);
    float filled = frac * n;
    bool blink = ifloor(now * 8) % 2 == 0;
    for (int i = 0; i < n; i++) {
        int sx = x + i * (seg + gap);
        uint16_t c = empty >= 0 ? (uint16_t)empty : C(T_line);
        if (i < ifloor(filled)) c = color >= 0 ? (uint16_t)color : C(T_acc);
        else if (i < filled && blink) c = lead >= 0 ? (uint16_t)lead : C(T_accL);
        g.fill(sx, y, seg, h, c);
    }
}

void spinner(Gfx &g, int x, int y, uint16_t c) {
    static const int8_t pts[8][2] = {
        {1, 0},
        {2, 0},
        {3, 1},
        {3, 2},
        {2, 3},
        {1, 3},
        {0, 2},
        {0, 1}
    };
    int k = ifloor(now * 10) % 8;
    for (int n = 0; n < 8; n++) {
        int d = (k - n + 8) % 8;
        if (d < 4) g.px(x + pts[n][0], y + pts[n][1], d == 0 ? C(T_hi) : d < 2 ? c : C(T_line2));
    }
}

void corners(Gfx &g, int x, int y, int w, int h, uint16_t c, int len) {
    g.hline(x, y, len, c);
    g.vline(x, y, len, c);
    g.hline(x + w - len, y, len, c);
    g.vline(x + w - 1, y, len, c);
    g.hline(x, y + h - 1, len, c);
    g.vline(x, y + h - len, len, c);
    g.hline(x + w - len, y + h - 1, len, c);
    g.vline(x + w - 1, y + h - len, len, c);
}

void backdrop(Gfx &g, int strength) {
    if (strength > 1) g.dfill(-g.ox, -g.oy, W, H, C(T_bg), 12);
    else g.checker(-g.ox, -g.oy, W, H, C(T_bg), 0);
}

// ── list view ───────────────────────────────────────────────────────────────
void MenuSrc::row(int i, Row &r) {
    const MenuItem &m = items[i];
    r.label = m.label;
    r.icon = m.icon;
    r.kind = m.kind;
    if (m.right) {
        strncpy(r.right, m.right, sizeof(r.right) - 1);
        r.right[sizeof(r.right) - 1] = 0;
    }
}

void ListView::init(ListSrc *s, int selIdx) {
    src = s;
    sel = firstSelectable(selIdx);
    scrollTarget = -1;
    selT = 0;
    snap();
}
void ListView::refresh(bool keepSel) {
    int n = src->count();
    if (!keepSel) sel = 0;
    if (sel >= n) sel = n - 1;
    if (sel < 0) sel = 0;
    sel = firstSelectable(sel);
    scrollTarget = -1;
    scrollTarget = targetScroll();
}
bool ListView::selectable(int i) {
    if (i < 0 || i >= src->count()) return false;
    return src->kind(i) != RK_SECTION;
}
int ListView::firstSelectable(int i) {
    int n = src->count();
    for (int k = 0; k < n; k++) {
        int j = (i + k) % n;
        if (selectable(j)) return j;
    }
    return 0;
}
int ListView::rowHeight(int i) {
    if (i < src->count() && src->kind(i) == RK_SECTION) return 9;
    return rowH + (i == sel && expand ? expand : 0);
}
int ListView::rowTop(int i) {
    int yy = 0;
    for (int k = 0; k < i; k++) yy += rowHeight(k);
    return yy;
}
float ListView::targetScroll() {
    int n = src->count();
    if (!n) return 0;
    int tp = rowTop(sel), bt = tp + rowHeight(sel);
    float s = scrollTarget >= 0 ? scrollTarget : 0;
    int margin = rowH;
    if (tp - margin < s) s = tp - margin;
    if (bt + margin > s + h) s = bt + margin - h;
    if (sel == firstSelectable(0)) s = 0;
    int mx = contentH() - h;
    return clampf(s, 0, mx > 0 ? mx : 0);
}
void ListView::snap() {
    scrollTarget = -1;
    scrollTarget = targetScroll();
    scroll = scrollTarget;
}
bool ListView::move(int d) {
    int n = src->count(), i = sel;
    if (!n) return false;
    for (int k = 0; k < n; k++) {
        i += d;
        if (i < 0) {
            if (!wrap) return false;
            i = n - 1;
        }
        if (i >= n) {
            if (!wrap) return false;
            i = 0;
        }
        if (selectable(i)) break;
    }
    if (i == sel) return false;
    sel = i;
    selT = 0;
    scrollTarget = targetScroll();
    return true;
}
bool ListView::key(uint8_t k) {
    if (k == K_UP) {
        move(-1);
        return true;
    }
    if (k == K_DOWN) {
        move(1);
        return true;
    }
    return false;
}
void ListView::update(float dt) {
    selT += dt;
    if (scrollTarget < 0) scrollTarget = targetScroll();
    scroll += (scrollTarget - scroll) * (dt * 18 < 1 ? dt * 18 : 1);
    if (fabsf(scrollTarget - scroll) < 0.4f) scroll = scrollTarget;
}
void ListView::draw(Gfx &g) {
    int sc = ifloor(scroll + 0.5f);
    int n = src->count(), ch = contentH();
    bool hasBar = ch > h;
    int rw = w - (hasBar ? 5 : 0);
    uint32_t saved = g.saveClip();
    g.clip(x, y, w, h);
    int ry = y - sc;
    for (int i = 0; i < n; i++) {
        int rh = rowHeight(i);
        if (ry + rh >= y && ry < y + h) drawRow(g, i, ry, rw, rh, i == sel);
        ry += rh;
        if (ry >= y + h) break;
    }
    g.restoreClip(saved);
    if (hasBar) {
        int bx = x + w - 3;
        g.dline(bx + 1, y + 1, bx + 1, y + h - 2, C(T_line), 1, 1);
        int th = ifloor((float)h * h / ch + 0.5f);
        if (th < 6) th = 6;
        int den = ch - h > 1 ? ch - h : 1;
        int ty = y + ifloor((h - th) * ((float)sc / den) + 0.5f);
        g.fill(bx, ty, 3, th, C(T_dim));
        g.vline(bx + 1, ty + 1, th - 2, C(T_text));
    }
}
void ListView::drawRow(Gfx &g, int i, int ry, int rw, int rh, bool sel_) {
    Row r;
    src->row(i, r);
    if (r.kind == RK_SECTION) {
        int tw = text(g, F_MICRO, r.label, x + pad, ry + 3, C(T_dim));
        g.dline(x + pad + tw + 3, ry + 5, x + rw - 3, ry + 5, C(T_line), 1, 1);
        return;
    }
    uint16_t fg = r.color >= 0          ? (uint16_t)r.color
                  : r.kind == RK_DANGER ? C(T_err)
                  : r.kind == RK_OP     ? C(T_sec)
                                        : C(T_text);
    if (r.disabled) fg = C(T_line2);
    if (sel_) {
        uint16_t bc = r.kind == RK_DANGER ? C(T_err) : C(T_acc);
        g.fill(x + 1, ry, rw - 2, rh, bc);
        g.vline(x, ry + 1, rh - 2, bc);
        g.vline(x + rw - 1, ry + 1, rh - 2, bc);
        float ph = fmodp(selT + 0.6f, 4.5f);
        if (ph < 0.5f) {
            int gx = x + ifloor(ph / 0.5f * (rw + 20) + 0.5f) - 10;
            for (int j = 1; j < rh - 1; j++) {
                g.px(gx - (j >> 1), ry + j, C(T_accL));
                g.px(gx - (j >> 1) + 3, ry + j, C(T_accL));
            }
        }
        fg = C(T_onAcc);
    }
    int tx = x + pad;
    if (src->icon(i, g, tx, ry, sel_)) tx += iconW;
    else if (r.icon != 255) {
        int gw = glyphW(r.icon), gh = glyphH(r.icon);
        uint16_t ic = sel_           ? C(T_onAcc)
                      : r.disabled   ? C(T_line2)
                      : r.iconC >= 0 ? (uint16_t)r.iconC
                                     : C(T_dim);
        glyph(g, r.icon, tx + (8 - gw) / 2, ry + (rowH - gh) / 2, ic, sel_ ? C(T_onAcc) : C(T_acc));
        tx += iconW;
    }
    int rx = x + rw - pad;
    int wx = src->widget(i, g, rx, ry + (rowH - 7) / 2, sel_);
    if (wx != rx) rx = wx - 3;
    else if (r.right[0]) {
        uint16_t rc = sel_ ? C(T_onAcc) : r.rightC >= 0 ? (uint16_t)r.rightC : C(T_dim);
        int rwid = text(g, F_MICRO, r.right, rx, ry + 3, rc, TA(AL_R));
        rx -= rwid + 4;
    }
    int maxW = rx - tx;
    int lw = textW(F_BODY, r.label);
    int ty = ry + 2;
    if (sel_ && lw > maxW && maxW > 0) {
        const int gap = 24, period = lw + gap;
        const float delay = 0.9f;
        int off = selT > delay ? (int)fmodp((selT - delay) * 22, (float)period) : 0;
        uint32_t saved = g.saveClip();
        // intersect the marquee window with the list clip
        int ax0 = g.cx0, ay0 = g.cy0, ax1 = g.cx1, ay1 = g.cy1;
        g.clip(tx, ry, maxW, rh);
        if (g.cx0 < ax0) g.cx0 = ax0;
        if (g.cy0 < ay0) g.cy0 = ay0;
        if (g.cx1 > ax1) g.cx1 = ax1;
        if (g.cy1 > ay1) g.cy1 = ay1;
        text(g, F_BODY, r.label, tx - off, ty, fg);
        text(g, F_BODY, r.label, tx - off + period, ty, fg);
        g.restoreClip(saved);
    } else text(g, F_BODY, r.label, tx, ty, fg, TA(AL_L, maxW > 0 ? maxW : 1));
    if (sel_ && expand && r.sub)
        text(g, F_MICRO, r.sub, tx, ry + rowH + 1, C(T_onAcc), TA(AL_L, rw - (tx - x) - pad));
}

// ── scene stack ─────────────────────────────────────────────────────────────
static Scene *stk[12];
static int nstk = 0;
struct TransState {
    uint8_t kind = TR_NONE; // TR_WIPE / TR_FADE
    bool fwd = true;
    float t = 0, dur = 0.24f;
    Scene *leaving = nullptr;
};
static TransState tr;
static Scene *pendingRemove = nullptr;
static uint32_t lastMs = 0, lastDraw = 0;
static int shx = 0, shy = 0;
static int bandRows = H;
struct Toast {
    char msg[48];
    uint8_t kind;
    float t, dur;
    bool on;
};
static Toast tst;

void begin(uint16_t *buf, int rows) {
    G.buf = buf;
    bandRows = rows;
    lastMs = plat::ms();
}
Scene *top() { return nstk ? stk[nstk - 1] : nullptr; }
bool transitioning() { return tr.kind != TR_NONE; }
int depth() { return nstk; }
Scene *at(int i) { return i >= 0 && i < nstk ? stk[i] : nullptr; }
bool onStack(const Scene &s) {
    for (int i = 0; i < nstk; i++)
        if (stk[i] == &s) return true;
    return false;
}

void push(Scene &s, uint8_t kind) {
    if (onStack(s) || nstk >= 12) return;
    if (kind == TR_AUTO) kind = s.overlay() ? TR_NONE : TR_WIPE;
    s.t = 0;
    s.res = RUNNING;
    stk[nstk++] = &s;
    if (kind != TR_NONE && nstk > 1) {
        tr.kind = kind;
        tr.fwd = true;
        tr.t = 0;
        tr.dur = kind == TR_FADE ? 0.45f : 0.24f;
        tr.leaving = nullptr;
    }
}

void remove(Scene &s) {
    for (int i = 0; i < nstk; i++)
        if (stk[i] == &s) {
            for (int j = i; j < nstk - 1; j++) stk[j] = stk[j + 1];
            nstk--;
            break;
        }
    if (tr.leaving == &s) tr.kind = TR_NONE;
}

void removeAfterTransition(Scene &s) {
    if (tr.kind == TR_NONE) remove(s);
    else pendingRemove = &s;
}

void pop(uint8_t kind) {
    if (!nstk) return;
    Scene *s = stk[nstk - 1];
    if (kind == TR_AUTO) kind = s->overlay() ? TR_NONE : TR_WIPE;
    nstk--;
    if (kind == TR_NONE || !nstk) {
        if (tr.leaving == s) tr.kind = TR_NONE;
        return;
    }
    // the leaving scene is drawn until the transition ends, so play it out here
    tr.kind = kind;
    tr.fwd = false;
    tr.t = 0;
    tr.dur = kind == TR_FADE ? 0.45f : 0.24f;
    tr.leaving = s;
    while (tr.kind != TR_NONE && tr.leaving == s) frame(false);
    tr.leaving = nullptr;
}

void toast(const char *msg, uint8_t kind, float dur) {
    strncpy(tst.msg, msg, sizeof(tst.msg) - 1);
    tst.msg[sizeof(tst.msg) - 1] = 0;
    tst.kind = kind;
    tst.t = 0;
    tst.dur = dur;
    tst.on = true;
}

static void drawToast(Gfx &g) {
    if (!tst.on) return;
    float inE = easeOut(clampf(tst.t / 0.18f, 0, 1)), outE = easeIn(clampf((tst.t - tst.dur) / 0.18f, 0, 1));
    uint16_t col = tst.kind == TK_ERR ? C(T_err) : tst.kind == TK_OK ? C(T_ok) : C(T_acc);
    uint8_t icon = tst.kind == TK_ERR ? G_WARN : tst.kind == TK_OK ? G_CHECK : G_INFO;
    int tw = textW(F_BODY, tst.msg), w = tw + 22, x = ifloor((W - w) / 2.0f + 0.5f);
    int y = ifloor(FOOT_Y - 4 - 13 * inE + 20 * outE + 0.5f);
    g.panel(x, y, w, 13, C(T_bg2), col, 2);
    g.fill(x + 1, y + 2, 2, 9, col);
    glyph(g, icon, x + 6, y + 4 - (icon == G_CHECK ? -1 : 1) + 1, col);
    text(g, F_BODY, tst.msg, x + 15, y + 3, C(T_text));
}

// draw scene i with the overlays above it down to its full-screen base
static void drawLayers(int idx) {
    if (idx < 0) return;
    int base = idx;
    while (base > 0 && stk[base]->overlay()) base--;
    if (stk[base]->overlay()) G.fill(-G.ox, -G.oy, W, H, C(T_bg));
    for (int i = base; i <= idx; i++) {
        G.resetClip();
        stk[i]->draw(G);
    }
}

static void renderBand() {
    G.ox = shx;
    G.oy = shy;
    G.mask = 16;
    G.base(0, W);
    if (!nstk) {
        G.fill(-G.ox, -G.oy, W, H, C(T_bg));
        return;
    }
    if (tr.kind == TR_WIPE) {
        float k = easeInOut(clampf(tr.t / tr.dur, 0, 1));
        int edge = ifloor((tr.fwd ? W * (1 - k) : W * k) + 0.5f);
        if (tr.fwd) {
            G.base(0, edge);
            drawLayers(nstk - 2);
            G.base(edge, W);
            drawLayers(nstk - 1);
        } else {
            G.base(0, edge);
            drawLayers(nstk - 1);
            G.base(edge, W);
            G.resetClip();
            tr.leaving->draw(G);
        }
        G.ox = G.oy = 0;
        G.base(0, W);
        G.vline(edge, 0, H, C(T_accL));
        G.vline(tr.fwd ? edge - 1 : edge + 1, 0, H, C(T_acc));
        G.dline(tr.fwd ? edge + 1 : edge - 1, 0, tr.fwd ? edge + 1 : edge - 1, H, C(T_accD), 1, 1);
    } else if (tr.kind == TR_FADE) {
        float k = easeInOut(clampf(tr.t / tr.dur, 0, 1));
        if (tr.fwd) drawLayers(nstk - 2);
        else {
            G.resetClip();
            tr.leaving->draw(G);
        }
        G.mask = (uint8_t)ifloor(k * 17);
        if (G.mask > 0) drawLayers(nstk - 1);
        G.mask = 16;
    } else drawLayers(nstk - 1);
    G.ox = G.oy = 0;
    G.base(0, W);
    drawToast(G);
}

static void render() {
    for (int y0 = 0; y0 < H; y0 += bandRows) {
        int h = H - y0 < bandRows ? H - y0 : bandRows;
        G.band(y0, h);
        renderBand();
        plat::push(y0, h, G.buf);
    }
    lastDraw = plat::ms();
}

void redraw() { render(); }
void snapshotBelow(uint16_t *dst) {
    uint16_t *b = G.buf;
    int y0 = G.by0, bh = G.bh;
    G.buf = dst;
    G.band(0, H);
    G.ox = G.oy = 0;
    G.mask = 16;
    G.base(0, W);
    drawLayers(nstk - 2);
    G.buf = b;
    G.band(y0, bh);
}
#if defined(ORBIT_NATIVE)
float testDt = 0;
void testSettle() {
    tr.kind = TR_NONE;
    shake = 0;
    shx = shy = 0;
}
void testReset() {
    nstk = 0;
    tr.kind = TR_NONE;
    tst.on = false;
    now = 0;
    shake = 0;
    lastMs = plat::ms();
}
#endif

static void advance(float dt) {
    now += dt;
    int base = nstk - 1;
    while (base > 0 && stk[base]->overlay()) base--;
    for (int i = base < 0 ? 0 : base; i < nstk; i++) {
        stk[i]->t += dt;
        stk[i]->update(dt, i == nstk - 1);
    }
    if (shake > 0) shake -= dt;
    if (tst.on) {
        tst.t += dt;
        if (tst.t >= tst.dur + 0.2f) tst.on = false;
    }
    if (tr.kind != TR_NONE) {
        tr.t += dt;
        if (tr.t >= tr.dur) tr.kind = TR_NONE;
    }
    if (tr.kind == TR_NONE && pendingRemove) {
        Scene *s = pendingRemove;
        pendingRemove = nullptr;
        remove(*s);
    }
}

void frame(bool input) {
    uint32_t start = plat::ms();
    float dt = (start - lastMs) / 1000.0f;
    if (dt > 0.1f) dt = 0.1f;
#if defined(ORBIT_NATIVE)
    if (testDt > 0) dt = testDt;
#endif
    lastMs = start;
    plat::status(ST);
    if (input && nstk) {
        Scene *s = stk[nstk - 1];
        Input in = plat::poll(s->textMode(), s->holdMode());
        if (in.k != K_NONE) s->key(in.k, in.ch);
    }
    advance(dt);
    if (shake > 0) {
        shx = (int)(rnd() % 3) - 1;
        shy = (int)(rnd() % 3) - 1;
    } else shx = shy = 0;
    if (!plat::screenOff()) render();
    plat::idle(start + 33);
}

void tick(uint32_t minGapMs) {
    uint32_t t0 = plat::ms();
    if (t0 - lastDraw < minGapMs) return;
    float dt = (t0 - lastMs) / 1000.0f;
    if (dt > 0.1f) dt = 0.1f;
    lastMs = t0;
    plat::status(ST);
    advance(dt);
    shx = shy = 0;
    if (!plat::screenOff()) render();
    else lastDraw = t0;
}

int run(Scene &s) {
    if (!onStack(s)) push(s);
    s.res = RUNNING;
    while (s.res == RUNNING) frame();
    int r = s.res;
    s.res = RUNNING;
    return r;
}

int modal(Scene &s, uint8_t kind) {
    push(s, kind);
    while (s.res == RUNNING) frame();
    int r = s.res;
    if (top() == &s) pop(kind == TR_FADE ? TR_FADE : TR_AUTO);
    else remove(s);
    return r;
}

// ── sheet ───────────────────────────────────────────────────────────────────
void SheetScene::setup(ListSrc *src, const char *title_, const char *sub_, int sel) {
    title = title_ ? title_ : "ACTIONS";
    sub = sub_;
    closing = false;
    ct = 0;
    picked = -1;
    list.x = W - pw + 2;
    list.y = 25;
    list.w = pw - 4;
    list.h = H - 25 - 13;
    list.rowH = 11;
    list.iconW = 11;
    list.init(src, sel);
}
void SheetScene::key(uint8_t k, char) {
    if (closing) return;
    if (list.key(k)) return;
    if (k == K_OK || k == K_HOLD) {
        picked = list.sel;
        closing = true;
        ct = 0;
    } else if (k == K_BACK) {
        picked = -1;
        closing = true;
        ct = 0;
    }
}
void SheetScene::update(float dt, bool) {
    list.update(dt);
    if (closing) {
        ct += dt;
        if (ct > 0.12f && res == RUNNING) res = picked;
    }
}
void SheetScene::draw(Gfx &g) {
    float inE = easeOut(clampf(t / 0.16f, 0, 1)), outE = closing ? easeIn(clampf(ct / 0.12f, 0, 1)) : 0;
    int off = ifloor((1 - inE + outE) * (pw + 4) + 0.5f);
    backdrop(g, 1);
    int ox0 = g.ox;
    g.ox = ox0 + off;
    int x = W - pw;
    g.fill(x, 0, pw, H, C(T_bg2));
    g.vline(x, 0, H, C(T_line2));
    g.vline(x + 1, 0, H, C(T_bg));
    text(g, F_MICRO, title, x + 6, 4, C(T_acc));
    if (sub) text(g, F_BODY, sub, x + 6, 12, C(T_text), TA(AL_L, pw - 12));
    g.hline(x + 4, 23, pw - 8, C(T_line));
    list.draw(g);
    g.hline(x + 4, H - 12, pw - 8, C(T_line));
    int hx = x + 6;
    hx += keycap(g, "\xE2\x8F\x8E", hx, H - 9) + 3;
    hx += text(g, F_MICRO, "RUN", hx, H - 8, C(T_dim)) + 6;
    hx += keycap(g, "`", hx, H - 9) + 3;
    text(g, F_MICRO, "CLOSE", hx, H - 8, C(T_dim));
    g.ox = ox0;
}

int sheet(ListSrc *src, const char *title, const char *sub, int sel) {
    SheetScene s;
    s.setup(src, title, sub, sel);
    return modal(s, TR_NONE);
}
int sheet(const MenuItem *items, int n, const char *title, const char *sub, int sel) {
    MenuSrc m;
    m.items = items;
    m.n = n;
    return sheet(&m, title, sub, sel);
}

// ── dialog ──────────────────────────────────────────────────────────────────
void DialogScene::key(uint8_t k, char) {
    if (closing || t < 0.12f) return;
    int n = o.nbtn;
    if (n) {
        if (k == K_LEFT || k == K_UP) sel = (sel + n - 1) % n;
        else if (k == K_RIGHT || k == K_DOWN) sel = (sel + 1) % n;
        else if (k == K_OK || k == K_HOLD) {
            pick = sel;
            closing = true;
            ct = 0;
        } else if (k == K_BACK) {
            pick = -1;
            closing = true;
            ct = 0;
        }
    } else {
        pick = -1;
        closing = true;
        ct = 0;
    }
}
void DialogScene::update(float dt, bool) {
    if (!closing && o.autoClose > 0 && t > o.autoClose) {
        pick = -1;
        closing = true;
        ct = 0;
    }
    if (closing) {
        ct += dt;
        if (ct > 0.1f && res == RUNNING) res = pick;
    }
}
void DialogScene::draw(Gfx &g) {
    uint8_t kind = o.kind;
    uint16_t col = kind == DK_ERROR     ? C(T_err)
                   : kind == DK_OK      ? C(T_ok)
                   : kind == DK_CONFIRM ? C(T_acc)
                                        : C(T_sec);
    uint8_t icon = kind == DK_ERROR ? G_WARN : kind == DK_OK ? G_CHECK : kind == DK_CONFIRM ? G_ASK : G_INFO;
    int w = o.w;
    char lines[6][64];
    int nl = wrapText(F_BODY, o.msg ? o.msg : "", w - 34, lines, 6);
    int h = 22 + nl * 10 + (o.nbtn ? 18 : 4) + (o.detail ? 9 : 0);
    int x = ifloor((W - w) / 2.0f + 0.5f), y = ifloor((H - h) / 2.0f + 0.5f);
    backdrop(g, o.dim2 ? 2 : 1);
    float inE = easeOut(clampf(t / 0.14f, 0, 1)), outE = closing ? clampf(ct / 0.1f, 0, 1) : 0;
    int openH = ifloor(h * inE * (1 - outE) + 0.5f);
    if (openH < 1) openH = 1;
    int cy = y + ifloor(h / 2.0f + 0.5f);
    if (openH < h) {
        int top_ = cy - openH / 2;
        g.fill(x - 6 + ifloor(6 * inE + 0.5f), top_, w + 12 - ifloor(12 * inE + 0.5f), openH, C(T_bg2));
        g.hline(x, top_, w, col);
        g.hline(x, top_ + openH - 1, w, col);
        return;
    }
    g.panel(x, y, w, h, C(T_bg2), col, 3);
    g.fill(x + 3, y + 3, w - 6, 9, col);
    glyph(g, icon, x + 6, y + 4 + (icon == G_CHECK ? 1 : 0), C(T_onAcc));
    const char *ttl = o.title              ? o.title
                      : kind == DK_ERROR   ? "ERROR"
                      : kind == DK_CONFIRM ? "CONFIRM"
                      : kind == DK_OK      ? "DONE"
                                           : "NOTICE";
    text(g, F_MICRO, ttl, x + 15, y + 5, C(T_onAcc), TA(AL_L, w - 30));
    if (o.code) text(g, F_MICRO, o.code, x + w - 7, y + 5, C(T_onAcc), TA(AL_R));
    for (int i = 0; i < nl; i++) text(g, F_BODY, lines[i], x + 10, y + 17 + i * 10, C(T_text));
    int by = y + 17 + nl * 10;
    if (o.detail) {
        text(g, F_MICRO, o.detail, x + 10, by + 1, C(T_dim), TA(AL_L, w - 20));
        by += 9;
    }
    if (o.nbtn) {
        int bx = x + w - 8;
        for (int i = o.nbtn - 1; i >= 0; i--) {
            int bw = textW(F_MICRO, o.buttons[i]) + 12;
            bx -= bw;
            bool s = i == sel, danger = (o.danger >> i) & 1;
            uint16_t bc = danger ? C(T_err) : C(T_acc);
            if (s) {
                g.panel(bx, by + 2, bw, 11, bc, -1, 2);
                text(g, F_MICRO, o.buttons[i], bx + 6, by + 5, C(T_onAcc));
            } else {
                g.panel(bx, by + 2, bw, 11, -1, C(T_line2), 2);
                text(g, F_MICRO, o.buttons[i], bx + 6, by + 5, danger ? C(T_err) : C(T_text));
            }
            bx -= 4;
        }
        text(g, F_MICRO, "\xE2\x97\x80\xE2\x96\xB6", x + 10, by + 5, C(T_line2));
    } else if (o.autoClose <= 0) text(g, F_MICRO, "ANY KEY", x + w - 8, y + h - 9, C(T_line2), TA(AL_R));
    if (o.autoClose > 0) {
        float f = clampf(1 - t / o.autoClose, 0, 1);
        g.hline(x + 3, y + h - 3, ifloor((w - 6) * f + 0.5f), col);
    }
}

int dialog(const DialogOpts &o) {
    DialogScene d;
    d.o = o;
    d.sel = o.sel;
    if (o.kind == DK_ERROR) shake = 0.16f;
    return modal(d, TR_NONE);
}
bool confirm(const char *title, const char *msg, const char *yes, bool danger, const char *detail) {
    static const char *btns[2];
    btns[0] = "CANCEL";
    btns[1] = yes;
    DialogOpts o;
    o.kind = DK_CONFIRM;
    o.title = title;
    o.msg = msg;
    o.detail = detail;
    o.buttons = btns;
    o.nbtn = 2;
    o.sel = 1;
    o.danger = danger ? 2 : 0;
    return dialog(o) == 1;
}
void errorBox(const char *msg, const char *title, const char *detail) {
    DialogOpts o;
    o.kind = DK_ERROR;
    o.msg = msg;
    o.title = title;
    o.detail = detail;
    dialog(o);
}

// ── keyboard map ────────────────────────────────────────────────────────────
// \b DEL, \t TAB, \x01 FN, \x02 SHIFT, \x03 CTRL, \x04 OPT, \x05 ALT, \n OK
const char *const KB_ROWS[4] = {
    "`1234567890-=\b",
    "\tqwertyuiop[]\\",
    "\x01\x02"
    "asdfghjkl;'\n",
    "\x03\x04\x05zxcvbnm,./ "
};
const char *kbLabel(char k) {
    switch (k) {
        case '\b': return "DEL";
        case '\t': return "TAB";
        case 1: return "FN";
        case 2: return "SH";
        case 3: return "CT";
        case 4: return "OP";
        case 5: return "AL";
        case '\n': return "OK";
        case ' ': return "SP";
    }
    return nullptr;
}
static bool kbSpecial(char k) {
    return k == '\b' || k == '\t' || (k >= 1 && k <= 5) || k == '\n' || k == ' ';
}
static bool kbNav(char k) { return k == ';' || k == '.' || k == ',' || k == '/' || k == '`'; }

void drawKeyMap(Gfx &g, int kx0, int ky, char lit, bool bindMode, const char *bound) {
    const int kw = 13, kh = 9;
    for (int r = 0; r < 4; r++) {
        const char *row = KB_ROWS[r];
        for (int c = 0; row[c]; c++) {
            char k = row[c];
            int kx = kx0 + c * (kw + 1), yy = ky + r * (kh + 1);
            char lk = (k >= 'A' && k <= 'Z') ? k + 32 : k;
            bool special = kbSpecial(k), nav = kbNav(k);
            bool hit = lit && lit == lk;
            char lab[4];
            const char *l = kbLabel(k);
            if (!l) {
                lab[0] = (k >= 'a' && k <= 'z') ? k - 32 : k;
                lab[1] = 0;
                l = lab;
            }
            if (!bindMode) {
                g.rrect(kx, yy, kw, kh, hit ? C(T_acc) : special ? C(T_bg3) : C(T_bg), 1);
                if (!hit) g.hline(kx + 1, yy + kh - 1, kw - 2, C(T_line));
                text(
                    g,
                    F_MICRO,
                    l,
                    kx + (kw + 1) / 2,
                    yy + 2,
                    hit       ? C(T_onAcc)
                    : nav     ? C(T_acc)
                    : special ? C(T_dim)
                              : C(T_text),
                    TA(AL_C)
                );
            } else {
                bool reserved = nav, isBound = !special && !reserved && bound && strchr(bound, lk);
                g.rrect(
                    kx,
                    yy,
                    kw,
                    kh,
                    hit        ? C(T_ok)
                    : reserved ? C(T_bg)
                    : isBound  ? C(T_secD)
                    : special  ? C(T_bg)
                               : C(T_bg3),
                    1
                );
                if (reserved)
                    for (int i = 0; i < kw; i++)
                        for (int j = 0; j < kh; j++)
                            if ((i + j) % 3 == 0) g.px(kx + i, yy + j, C(T_errD));
                text(
                    g,
                    F_MICRO,
                    l,
                    kx + 7,
                    yy + 2,
                    hit        ? C(T_onAcc)
                    : special  ? C(T_line)
                    : reserved ? C(T_err)
                    : isBound  ? C(T_sec)
                               : C(T_text),
                    TA(AL_C)
                );
            }
        }
    }
}

// ── text entry ──────────────────────────────────────────────────────────────
void KeyboardScene::key(uint8_t k, char ch) {
    if (done) return;
    if (k == K_CHAR && ch) {
        if (len < max) {
            val[len++] = ch;
            val[len] = 0;
            revealT = 0;
        }
        lastKey = (ch >= 'A' && ch <= 'Z') ? ch + 32 : ch;
        lastKeyT = 0;
    } else if (k == K_DEL) {
        if (len > 0) val[--len] = 0;
        lastKey = '\b';
        lastKeyT = 0;
    } else if (k == K_OK) {
        lastKey = '\n';
        lastKeyT = 0;
        done = true;
        ok = 1;
        doneT = 0;
    } else if (k == K_BACK) {
        lastKey = '`';
        lastKeyT = 0;
        done = true;
        ok = 0;
        doneT = 0;
    }
}
void KeyboardScene::update(float dt, bool) {
    lastKeyT += dt;
    revealT += dt;
    if (done) {
        doneT += dt;
        if (doneT > 0.09f && res == RUNNING) res = ok;
    }
}
void KeyboardScene::draw(Gfx &g) {
    backdrop(g, 2);
    float inE = easeOut(clampf(t / 0.16f, 0, 1));
    const int x = 10, w = 220, h = 98;
    int y = ifloor(18 + (1 - inE) * 20 + 0.5f);
    g.panel(x, y, w, h, C(T_bg2), C(T_line2), 3);
    int tw = textW(F_MICRO, title) + 10;
    g.panel(x + 6, y - 4, tw, 9, C(T_acc), -1, 2);
    text(g, F_MICRO, title, x + 11, y - 2, C(T_onAcc));
    if (sub) text(g, F_MICRO, sub, x + 6 + tw + 5, y - 2, C(T_dim), TA(AL_L, w - tw - 16));
    const int fx = x + 8, fy = y + 12, fw = w - 16, fh = 17;
    g.fill(fx, fy, fw, fh, C(T_bg));
    g.rect(fx, fy, fw, fh, C(T_line2));
    g.hline(fx + 1, fy + fh - 1, fw - 2, C(T_acc));
    char shown[160];
    if (mask) {
        // bullets (3 bytes each), the last typed character shows briefly
        int o = 0;
        for (int i = 0; i < len && o < 150; i++) {
            if (i == len - 1 && revealT < 0.8f) shown[o++] = val[i];
            else {
                memcpy(shown + o, "\xE2\x80\xA2", 3);
                o += 3;
            }
        }
        shown[o] = 0;
    } else {
        strncpy(shown, val, sizeof(shown) - 1);
        shown[sizeof(shown) - 1] = 0;
    }
    int inner = fw - 10, tx = fx + 5;
    int sw = textW(F_BODY, shown);
    uint32_t saved = g.saveClip();
    g.clip(fx + 2, fy + 1, fw - 4, fh - 2);
    if (sw > inner - 6) tx -= sw - inner + 6;
    text(g, F_BODY, shown, tx, fy + 4, C(T_hi));
    g.restoreClip(saved);
    if (sw > inner - 6) {
        g.fill(fx + 1, fy + 1, 4, fh - 2, C(T_bg));
        text(g, F_BODY, "\xE2\x80\xB9", fx + 1, fy + 4, C(T_acc));
    }
    int cx = tx + sw + 1;
    if (cx > fx + fw - 6) cx = fx + fw - 6;
    if (ifloor(now * 2.2f) % 2 == 0 || lastKeyT < 0.4f) g.fill(cx, fy + 3, 5, 10, C(T_acc));
    char cnt[12];
    sprintf(cnt, "%d/%d", len, max);
    text(g, F_MICRO, cnt, fx + fw, fy + fh + 3, len >= max ? C(T_warn) : C(T_dim), TA(AL_R));
    if (mask)
        text(g, F_MICRO, "HIDDEN", fx + fw - textW(F_MICRO, cnt) - 8, fy + fh + 3, C(T_line2), TA(AL_R));
    const int kw = 13;
    int kx0 = x + ifloor((w - 14 * (kw + 1) + 1) / 2.0f + 0.5f);
    drawKeyMap(g, kx0, y + 44, lastKeyT < 0.35f ? lastKey : 0, false, nullptr);
    int hy = y + h - 10, hx = x + 8;
    hx += keycap(g, "\xE2\x8F\x8E", hx, hy) + 3;
    hx += text(g, F_MICRO, "DONE", hx, hy + 1, C(T_dim)) + 6;
    hx += keycap(g, "DEL", hx, hy) + 3;
    hx += text(g, F_MICRO, "ERASE", hx, hy + 1, C(T_dim)) + 6;
    hx += keycap(g, "FN+`", hx, hy) + 3;
    text(g, F_MICRO, "CANCEL", hx, hy + 1, C(T_dim));
}

bool askText(const char *title, const char *sub, char *buf, int max, bool mask) {
    KeyboardScene k;
    k.title = title;
    k.sub = sub;
    k.val = buf;
    k.max = max;
    k.len = strlen(buf);
    if (k.len > max) {
        k.len = max;
        buf[max] = 0;
    }
    k.mask = mask;
    return modal(k, TR_NONE) == 1;
}

// ── steps ───────────────────────────────────────────────────────────────────
void StepsScene::add(const char *s) {
    if (n - first >= MAXS) first++;
    char *d = steps[n % MAXS];
    int i = 0;
    for (; s[i] && i < 31; i++) d[i] = (s[i] >= 'a' && s[i] <= 'z') ? s[i] - 32 : s[i];
    while (i > 0 && (d[i - 1] == '.' || d[i - 1] == ' ')) i--; // "Writing table..." -> "WRITING TABLE"
    d[i] = 0;
    n++;
    frac = -1;
    t = 0;
}
void StepsScene::draw(Gfx &g) {
    backdrop(g, 2);
    int shownN = n - first;
    const int x = 40, w = 160;
    int h = 30 + shownN * 10, y = ifloor((H - h) / 2.0f + 0.5f);
    g.panel(x, y, w, h, C(T_bg2), failed ? C(T_err) : C(T_line2), 3);
    text(g, F_MICRO, title, x + 8, y + 6, failed ? C(T_err) : C(T_acc), TA(AL_L, w - 24));
    if (!finished && !failed) spinner(g, x + w - 12, y + 6, C(T_acc));
    for (int k = 0; k < shownN; k++) {
        int idx = first + k, yy = y + 17 + k * 10;
        bool cur = idx == n - 1 && !finished, doneS = !cur;
        if (cur && failed) glyph(g, G_CROSS, x + 8, yy + 1, C(T_err));
        else if (doneS) glyph(g, G_CHECK, x + 8, yy + 1, C(T_ok));
        else if (ifloor(now * 5) % 2) glyph(g, G_PLAY, x + 9, yy, C(T_acc));
        text(g, F_MICRO, steps[idx % MAXS], x + 18, yy, doneS ? C(T_text) : C(T_hi), TA(AL_L, w - 64));
        if (cur && frac >= 0) miniBar(g, x + w - 40, yy + 1, 32, frac, false);
    }
    if (ifloor(now * 1.5f) % 2 == 0 && !failed)
        text(g, F_MICRO, "KEEP POWER ON", x + 8, y + h - 9, C(T_warn));
}

// ── ring progress ───────────────────────────────────────────────────────────
void ArcScene::draw(Gfx &g) {
    backdrop(g, 2);
    const int cx = 120, cy = 62;
    g.panel(70, 26, 100, 82, C(T_bg2), C(T_line2), 3);
    g.ring(cx, cy, 17, 22, 0, 1, C(T_line));
    g.ring(cx, cy, 17, 22, 0, p, C(T_acc));
    float tip = p * 6.2831853f;
    g.fill(
        ifloor(cx + fsin(tip) * 19.5f + 0.5f) - 1, ifloor(cy - fcos(tip) * 19.5f + 0.5f) - 1, 3, 3, C(T_accL)
    );
    char pct[8];
    sprintf(pct, "%d%%", ifloor(p * 100 + 0.5f));
    text(g, F_BODY, pct, cx, cy - 4, C(T_hi), TA(AL_C));
    text(g, F_MICRO, title, cx, 90, C(T_acc), TA(AL_C));
    char up[40];
    int i = 0;
    for (; sub[i] && i < 39; i++) up[i] = (sub[i] >= 'a' && sub[i] <= 'z') ? sub[i] - 32 : sub[i];
    up[i] = 0;
    text(g, F_MICRO, up, cx, 98, C(T_dim), TA(AL_C, 92));
}

} // namespace orb
#endif
