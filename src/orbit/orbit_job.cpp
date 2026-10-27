#if defined(ORBIT_UI) || defined(ORBIT_NATIVE)
#include "orbit_screens.h"
#include <math.h>
#include <stdio.h>

// INSTALL / DOWNLOAD — a rocket climbs its trajectory as bytes are written; the
// right side has the big percentage, the stage checklist and live telemetry.
namespace orb {

void JobScene::reset(bool dl, const char *nm) {
    download = dl;
    finished = failed = false;
    strncpy(name, nm ? nm : "", sizeof(name) - 1);
    name[sizeof(name) - 1] = 0;
    nst = si = np = 0;
    sp = speed = doneT = shownP = 0;
    bytes = 0;
    doneLine[0] = doneSub[0] = 0;
    doneWait = 0;
    anyKey = false;
}
int JobScene::addStage(const char *label, uint32_t size, uint32_t ofs, bool app) {
    if (nst >= MAXST) { // keep the newest: drop a finished one
        for (int i = 0; i + 1 < nst; i++) st[i] = st[i + 1];
        nst--;
        if (si > 0) si--;
    }
    Stage &s = st[nst];
    int i = 0;
    for (; label[i] && i < 19; i++)
        s.label[i] = label[i] >= 'a' && label[i] <= 'z' ? label[i] - 32 : label[i];
    while (i > 0 && (s.label[i - 1] == '.' || s.label[i - 1] == ' ')) i--;
    s.label[i] = 0;
    s.size = size;
    s.ofs = ofs;
    s.app = app;
    return nst++;
}
int JobScene::findStage(const char *label) const {
    char up[20];
    int i = 0;
    for (; label[i] && i < 19; i++) up[i] = label[i] >= 'a' && label[i] <= 'z' ? label[i] - 32 : label[i];
    while (i > 0 && (up[i - 1] == '.' || up[i - 1] == ' ')) i--;
    up[i] = 0;
    for (int k = si; k < nst; k++)
        if (!strcmp(st[k].label, up)) return k;
    return -1;
}
void JobScene::goStage(int i) {
    if (i < 0 || i >= nst) return;
    si = i;
    sp = 0;
    bytes = 0;
}
void JobScene::progress(uint32_t done, uint32_t tot) {
    if (si >= nst) return;
    if (tot && st[si].size != tot && st[si].ofs == 0 && !st[si].app) st[si].size = tot;
    bytes = done;
    sp = tot ? clampf((float)done / tot, 0, 1) : 0;
}
void JobScene::finish(const char *line, const char *sub) {
    finished = true;
    si = nst;
    doneT = 0;
    strncpy(doneLine, line ? line : "", sizeof(doneLine) - 1);
    strncpy(doneSub, sub ? sub : "", sizeof(doneSub) - 1);
}
uint32_t JobScene::total() const {
    uint32_t t_ = 0;
    for (int i = 0; i < nst; i++) t_ += st[i].size;
    return t_;
}
float JobScene::overall() const {
    if (finished) return 1;
    uint32_t tot = total(), d = 0;
    if (!tot) return 0;
    for (int i = 0; i < si && i < nst; i++) d += st[i].size;
    float p = (d + sp * (si < nst ? st[si].size : 0)) / tot;
    return clampf(p > shownP ? p : shownP, 0, 1); // stages appear as they start: never go backwards
}
uint32_t JobScene::flashAddr() const {
    if (si >= nst || !st[si].ofs) return 0;
    return (st[si].ofs + bytes) & ~0xfffu;
}
void JobScene::curve(float u, float &x, float &y) const {
    const float x0 = 14, y0 = 112, cx = 18, cy = 40, x1 = 86, y1 = 26;
    float a = (1 - u) * (1 - u), b = 2 * u * (1 - u), c = u * u;
    x = a * x0 + b * cx + c * x1;
    y = a * y0 + b * cy + c * y1;
}
void JobScene::rocketPos(float &x, float &y, float &dx, float &dy) const {
    float u = finished ? 1 : overall(), qx, qy;
    curve(u, x, y);
    curve(u + 0.02f < 1 ? u + 0.02f : 1, qx, qy);
    float l = sqrtf((qx - x) * (qx - x) + (qy - y) * (qy - y));
    if (l == 0) l = 1;
    dx = (qx - x) / l;
    dy = (qy - y) / l;
}
void JobScene::key(uint8_t, char) {
    if (finished && doneT > 0.5f) anyKey = true;
}
void JobScene::update(float dt, bool) {
    if (finished) {
        doneT += dt;
        return;
    }
    shownP = overall();
    float x, y, dx, dy;
    rocketPos(x, y, dx, dy);
    for (int i = 0; i < 40 * dt; i++) {
        float px = x + (rng.next() - .5f) * 2, vx = (rng.next() - .5f) * 8 - dx * 10,
              vy = 10 + rng.next() * 8 - dy * 10, mx = 0.4f + rng.next() * 0.5f;
        if (np < 64) parts[np++] = {px, y + 12, vx, vy, 0, mx, 0, 0};
    }
    int k = 0;
    for (int i = 0; i < np; i++) {
        Particle &q = parts[i];
        q.life += dt;
        q.x += q.vx * dt;
        q.y += q.vy * dt;
        if (q.life < q.max) parts[k++] = q;
    }
    np = k;
}

void JobScene::draw(Gfx &g) {
    const float tt = now;
    float P = finished ? 1 : overall();
    char s[64], a[16], b[16];
    g.clear(C(T_bg));
    Hdr h;
    h.icon = download ? G_DL : G_INSTALL;
    h.title = download ? "DOWNLOAD" : "INSTALL";
    h.crumb = name;
    h.crumbMax = 100;
    header(g, h);
    // left: trajectory window
    const int wx = 3, wy = 14, ww = 90, wh = 108;
    g.fill(wx, wy, ww, wh, C(T_sky0));
    g.rect(wx, wy, ww, wh, C(T_line));
    uint32_t saved = g.saveClip();
    g.clip(wx + 1, wy + 1, ww - 2, wh - 2);
    for (int y = wy; y < wy + wh; y++)
        g.dfill(wx, y, ww, 1, C(T_sky1), jsround(clampf((float)(y - wy) / wh, 0, 1) * 14));
    drawStarsIn(g, wx, wy, ww, wh);
    drawPlanet(g, tt, PlanetP{18, 250, 132, 70});
    for (int y = wy + 6; y < wy + wh - 4; y += 6) g.hline(wx + 2, y, 2, C(T_line2));
    for (int i = 0; i <= 60; i++) {
        float u = i / 60.0f, x, y;
        curve(u, x, y);
        if (u <= P) g.px(jsround(x), jsround(y), C(T_acc));
        else if (i % 2 == 0) g.px(jsround(x), jsround(y), C(T_line2));
    }
    float ex, ey;
    curve(1, ex, ey);
    corners(g, jsround(ex) - 4, jsround(ey) - 4, 9, 9, finished ? C(T_ok) : C(T_line2), 2);
    for (int i = 0; i < np; i++) {
        const Particle &q = parts[i];
        float f = q.life / q.max;
        g.px(
            jsround(q.x),
            jsround(q.y),
            f < 0.2f    ? C(T_flame0)
            : f < 0.45f ? C(T_flame1)
            : f < 0.7f  ? C(T_flame2)
                        : C(T_line2)
        );
    }
    float rx_, ry_, rdx, rdy;
    rocketPos(rx_, ry_, rdx, rdy);
    blit(g, SPR_MINIROCKET, jsround(rx_) - 4, jsround(ry_) - 6);
    g.hline(wx + 1, jsround(ry_), 6, C(T_acc));
    sprintf(s, "ALT %dKM", jsround(P * 408));
    text(g, F_MICRO, s, wx + 4, wy + 3, C(T_dim));
    g.restoreClip(saved);
    // right: numbers
    const int rx = 100;
    if (finished) {
        text(
            g,
            F_MICRO,
            failed     ? "ABORTED"
            : download ? "DOWNLINK COMPLETE"
                       : "ORBIT REACHED",
            rx,
            15,
            failed ? C(T_err) : C(T_ok)
        );
        text(g, F_ORBIT, failed ? "FAILED" : download ? "SAVED" : "INSTALLED", rx, 23, C(T_hi));
    } else {
        sprintf(s, "%d", ifloor(P * 100));
        int w = text(g, F_ORBIT, s, rx, 14, C(T_hi), T_BIG);
        text(g, F_ORBIT, "%", rx + w + 3, 23, C(T_dim));
        if (si < nst) {
            text(g, F_MICRO, st[si].label, 236, 15, C(T_acc), TA(AL_R));
            fmtSize(a, (uint32_t)(sp * st[si].size + 0.5f));
            fmtSize(b, st[si].size);
            snprintf(s, sizeof(s), "%s/%s", a, b);
            text(g, F_MICRO, s, 236, 23, C(T_dim), TA(AL_R));
        }
    }
    // stage checklist (the last seven)
    int first = nst > 7 ? nst - 7 : 0, y = 42;
    for (int i = first; i < nst; i++) {
        bool doneS = i < si, cur = i == si && !finished;
        bool bad = failed && i == (si < nst ? si : nst - 1);
        if (bad) glyph(g, G_CROSS, rx, y + 1, C(T_err));
        else if (doneS) glyph(g, G_CHECK, rx, y + 1, C(T_ok));
        else if (cur) {
            if (ifloor(tt * 4) % 2 == 0) glyph(g, G_PLAY, rx + 1, y, C(T_acc));
        } else g.px(rx + 2, y + 2, C(T_line2));
        text(g, F_MICRO, st[i].label, rx + 8, y, doneS ? C(T_text) : cur ? C(T_hi) : C(T_line2));
        if (cur) miniBar(g, 200, y + 1, 36, sp, false);
        else {
            if (!doneS) fmtSize(a, st[i].size);
            text(g, F_MICRO, doneS ? "OK" : a, 236, y, doneS ? C(T_ok) : C(T_line2), TA(AL_R));
        }
        y += 9;
    }
    // telemetry
    if (!finished) {
        float rem = (1 - P) * total();
        int eta = speed > 0 ? jsround(rem / speed) : 0;
        sprintf(s, "%d KB/S", jsround(speed / 1024));
        text(g, F_MICRO, s, rx, 112, C(T_dim));
        sprintf(s, "ETA %d:%02d", eta / 60, eta % 60);
        text(g, F_MICRO, s, 168, 112, C(T_dim), TA(AL_C));
        uint32_t ad = flashAddr();
        if (ad) {
            s[0] = '@';
            hex32(s + 1, ad);
            text(g, F_MICRO, s, 236, 112, C(T_line2), TA(AL_R));
        }
    } else {
        text(g, F_BODY, doneLine, rx, 108, C(T_text), TA(AL_L, 136));
        if (doneWait > 0) {
            int left = (int)doneWait - ifloor(doneT);
            sprintf(s, "IN %d \xC2\xB7 ANY KEY NOW", left > 0 ? left : 0);
            text(g, F_MICRO, s, rx, 118, C(T_dim));
        } else text(g, F_MICRO, doneSub, rx, 118, C(T_dim));
    }
    // bottom: overall progress
    g.hline(0, FOOT_Y, W, C(T_line));
    segbar(g, 4, 128, 150, 4, P, finished ? (failed ? C(T_err) : C(T_ok)) : C(T_acc));
    if (!finished) {
        if (ifloor(tt * 1.5f) % 2 == 0) text(g, F_MICRO, "KEEP POWER ON", 236, 128, C(T_warn), TA(AL_R));
    } else
        text(
            g,
            F_MICRO,
            failed     ? "NOTHING BOOTS CHANGED"
            : download ? "DONE"
                       : "BOOT SLOT SET",
            236,
            128,
            failed ? C(T_err) : C(T_ok),
            TA(AL_R)
        );
}

} // namespace orb
#endif
