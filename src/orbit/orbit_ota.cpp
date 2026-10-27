#if defined(ORBIT_UI) || defined(ORBIT_NATIVE)
#include "orbit_screens.h"
#include <math.h>
#include <stdio.h>

// ONLINE — LauncherHub: hub tiles, downlink while fetching, the paged catalog and
// the firmware dossier with its version timeline. Also the generic list screen.
namespace orb {

// ── generic list ────────────────────────────────────────────────────────────
void ListScene::setup(ListSrc *src, int sel, int expand) {
    list.x = 0;
    list.y = HDR_H + 1;
    list.w = W;
    list.h = FOOT_Y - HDR_H - 2;
    list.expand = expand;
    list.iconW = expand ? 9 : 10;
    list.init(src, sel);
}
void ListScene::key(uint8_t k, char) {
    if (list.key(k)) return;
    if (k == K_BACK) res = LS_BACK;
    else if (pages && k == K_LEFT) res = LS_PREV;
    else if (pages && k == K_RIGHT) res = LS_NEXT;
    else if (k == K_OK || k == K_HOLD) res = list.sel;
}
void ListScene::draw(Gfx &g) {
    g.clear(C(T_bg));
    header(g, hdr);
    list.draw(g);
    hints(g, hk, right, C(T_line2));
}

// ── hub tiles ───────────────────────────────────────────────────────────────
void OtaMenuScene::key(uint8_t k, char) {
    if (k == K_LEFT || k == K_UP) {
        sel = (sel + n - 1) % n;
        ct = 0;
    } else if (k == K_RIGHT || k == K_DOWN) {
        sel = (sel + 1) % n;
        ct = 0;
    } else if (k == K_BACK) res = -1;
    else if (k == K_OK) res = tiles[sel].id;
}
void OtaMenuScene::draw(Gfx &g) {
    g.clear(C(T_bg));
    Hdr h;
    h.icon = G_SAT;
    h.title = "ONLINE";
    h.crumb = crumb;
    header(g, h);
    drawStars(g, now, H, 0);
    int tw = n == 3 ? 72 : 96;
    const int gap = 5;
    int x0 = jsround((W - (n * tw + (n - 1) * gap)) / 2.0f);
    for (int i = 0; i < n; i++) {
        const HubTile &tl = tiles[i];
        int x = x0 + i * (tw + gap);
        const int y = 24, hh = 84;
        bool s = i == sel;
        int lift = s ? jsround(easeOut(clampf(ct / 0.15f, 0, 1)) * 2) : 0;
        g.panel(x, y - lift, tw, hh, s ? C(T_bg3) : C(T_bg2), s ? C(T_acc) : C(T_line), 3);
        if (s) corners(g, x - 2, y - 2 - lift, tw + 4, hh + 4, C(T_accL), 3);
        glyph(g, tl.icon, x + tw - 11, y + 5 - lift, s ? C(T_acc) : C(T_dim));
        text(g, F_MICRO, tl.label, x + 6, y + 6 - lift, s ? C(T_acc) : C(T_text));
        text(g, F_ORBIT, tl.big, x + 6, y + 26 - lift, s ? C(T_hi) : C(T_text), T_BIG);
        text(g, F_MICRO, tl.sub, x + 6, y + 56 - lift, C(T_dim), TA(AL_L, tw - 10));
        if (tl.badge > 0) {
            char b[16];
            sprintf(b, "%d NEW", tl.badge);
            g.fill(x + 6, y + 66 - lift, textW(F_MICRO, b) + 6, 9, C(T_sec));
            text(g, F_MICRO, b, x + 9, y + 68 - lift, C(T_onAcc));
        } else if (tl.foot) text(g, F_MICRO, tl.foot, x + 6, y + 68 - lift, C(T_line2), TA(AL_L, tw - 10));
    }
    hints(g, "\xE2\x97\x80\xE2\x96\xB6|CHOOSE|\xE2\x8F\x8E|OPEN|`|BACK");
}

// ── downlink ────────────────────────────────────────────────────────────────
void FetchScene::draw(Gfx &g) {
    const float tt = t;
    g.clear(C(T_sky0));
    for (int y = 30; y < H; y++) g.dfill(0, y, W, 1, C(T_sky1), jsround(clampf((y - 30) / 90.0f, 0, 1) * 16));
    drawStars(g, now, H);
    Hdr h;
    h.icon = G_SAT;
    h.title = "LAUNCHERHUB";
    h.crumb = "API.LAUNCHERHUB.NET";
    h.bg = C(T_sky0);
    header(g, h);
    g.fill(0, 112, W, 13, C(T_pl0));
    g.hline(0, 112, W, C(T_pl2));
    blit(g, ICO_OTA, 18, 81);
    int sx = 196 + jsround(fsin(tt * 0.8f) * 3), sy = 28 + jsround(fcos(tt * 0.6f) * 2);
    g.fill(sx - 3, sy - 3, 7, 7, C(T_gold));
    g.rect(sx - 3, sy - 3, 7, 7, C(T_goldD));
    g.px(sx - 1, sy - 1, C(T_goldL));
    for (int d = -1; d <= 1; d += 2) {
        int px = d < 0 ? sx - 16 : sx + 5;
        g.fill(px, sy - 2, 12, 5, C(T_sec));
        for (int i = 0; i < 12; i += 3) g.vline(px + i, sy - 2, 5, C(T_secD));
        g.hline(px, sy, 12, C(T_secD));
        g.hline(d < 0 ? sx - 4 : sx + 4, sy, 1, C(T_dim));
    }
    g.vline(sx, sy + 4, 3, C(T_dim));
    g.px(sx, sy + 7, C(T_acc));
    const int ax = 40, ay = 88, bx = sx, by = sy + 7;
    g.dline(ax, ay, bx, by, C(T_line2), 2, 3, ifloor(tt * 20));
    for (int k = 0; k < 4; k++) {
        float f = fmodp(tt * 0.8f + k / 4.0f, 1);
        int x = jsround(bx + (ax - bx) * f), y = jsround(by + (ay - by) * f);
        g.fill(x - 1, y - 1, 2, 2, C(T_acc));
    }
    char first[24];
    const char *sp = strchr(label, ' ');
    int l = sp ? sp - label : strlen(label);
    if (l > 23) l = 23;
    memcpy(first, label, l);
    first[l] = 0;
    text(g, F_MICRO, "DOWNLINK", 120, 58, C(T_dim), TA(AL_C));
    text(g, F_ORBIT, first, 120, 66, C(T_hi), TA(AL_C));
    text(g, F_MICRO, sp ? sp + 1 : "", 120, 80, C(T_acc), TA(AL_C));
    text(g, F_MICRO, path, 120, 98, C(T_line2), TA(AL_C, 230));
    spinner(g, 60, 66, C(T_acc));
    hints(g, "", tries, C(T_line2));
}

// ── firmware dossier ────────────────────────────────────────────────────────
void FirmwareScene::key(uint8_t k, char) {
    if (k == K_RIGHT || k == K_DOWN) {
        if (nv) vi = (vi + 1) % nv;
        vt = 0;
        dir = 1;
    } else if (k == K_LEFT || k == K_UP) {
        if (nv) vi = (vi + nv - 1) % nv;
        vt = 0;
        dir = -1;
    } else if (k == K_BACK) res = -1;
    else if (k == K_OK && nv) res = 1;
}
void FirmwareScene::draw(Gfx &g) {
    char s[64];
    g.clear(C(T_bg));
    char up[48];
    int i = 0;
    for (; name[i] && i < 47; i++) up[i] = name[i] >= 'a' && name[i] <= 'z' ? name[i] - 32 : name[i];
    up[i] = 0;
    Hdr h;
    h.icon = G_SAT;
    h.title = "LAUNCHERHUB";
    h.crumb = up;
    h.crumbMax = 100;
    header(g, h);
    // emblem
    const int ex = 8, ey = 19;
    uint8_t fam = famOf(fid);
    g.fill(ex, ey, 38, 38, C(T_bg2));
    corners(g, ex - 2, ey - 2, 42, 42, C(T_line2), 4);
    emblem(g, fid, ex + 4, ey + 4, 6, famC(fam, 0), C(T_bg3));
    for (int j = 0; j < 5; j++)
        for (int k = 0; k < 5; k++)
            if (emblemBit(fid, k, j)) g.hline(ex + 4 + k * 6, ey + 4 + j * 6, 6, famC(fam, 1));
    if (star) {
        g.fill(ex, ey + 44, 38, 9, C(T_acc));
        text(g, F_MICRO, "\xE2\x98\x85 STARRED", ex + 19, ey + 46, C(T_onAcc), TA(AL_C));
    }
    // name + author
    const int x = 54;
    char lines[2][64];
    int nl = wrapText(F_BODY, name, 180, lines, 2);
    for (int k = 0; k < nl; k++) text(g, F_BODY, lines[k], x, 16 + k * 10, C(T_hi));
    int ay = 16 + nl * 10;
    snprintf(s, sizeof(s), "BY %s", author);
    for (char *p = s; *p; p++)
        if (*p >= 'a' && *p <= 'z') *p -= 32;
    text(g, F_MICRO, s, x, ay + 1, C(T_dim), TA(AL_L, 180));
    if (!nv) {
        text(g, F_BODY, "No versions published", x, ay + 14, C(T_text));
        hints(g, "`|BACK");
        return;
    }
    const FwVersion &v = vers[vi];
    // version selector
    int vy = ay + 12;
    text(g, F_MICRO, "VERSION", x, vy, C(T_line2));
    sprintf(s, "%d/%d", vi + 1, nv);
    text(g, F_MICRO, s, 236, vy, C(T_line2), TA(AL_R));
    int slide = jsround((1 - easeOut(clampf(vt / 0.16f, 0, 1))) * 8 * dir);
    bool pressL = vt < 0.15f && dir < 0, pressR = vt < 0.15f && dir > 0;
    int vw = textW(F_ORBIT, v.v);
    text(g, F_ORBIT, "<", x, vy + 9, pressL ? C(T_accL) : C(T_line2));
    uint32_t saved = g.saveClip();
    g.clip(x + 9, vy + 7, vw + 8, 12);
    text(g, F_ORBIT, v.v, x + 11 + slide, vy + 9, C(T_acc));
    g.restoreClip(saved);
    text(g, F_ORBIT, ">", x + 14 + vw, vy + 9, pressR ? C(T_accL) : C(T_line2));
    if (vi == 0) {
        g.fill(x + 25 + vw, vy + 9, 29, 9, C(T_ok));
        text(g, F_MICRO, "LATEST", x + 27 + vw, vy + 11, C(T_onAcc));
    }
    // details
    int dy = vy + 25;
    const char *K[3] = {"RELEASED", "FILE", "ON DEVICE"};
    const char *V[3] = {v.date, file[0] ? file : "-", onDevice[0] ? onDevice : "NOT INSTALLED"};
    for (int k = 0; k < 3; k++) {
        text(g, F_MICRO, K[k], x, dy + k * 9, C(T_line2));
        text(
            g,
            F_MICRO,
            V[k],
            x + 44,
            dy + k * 9,
            k == 2 && onDevice[0] ? C(T_sec) : C(T_text),
            TA(AL_L, 236 - x - 44)
        );
    }
    // version timeline, oldest left
    const int ty = 113, tx0 = x, tx1 = 232;
    g.hline(tx0, ty, tx1 - tx0, C(T_line));
    for (int k = 0; k < nv; k++) {
        int vx = jsround(tx1 + (tx0 - tx1) * (nv == 1 ? 0 : (float)k / (nv - 1)));
        bool cur = k == vi;
        g.vline(vx, ty - (cur ? 3 : 1), cur ? 7 : 3, cur ? C(T_acc) : C(T_line2));
        if (cur) corners(g, vx - 3, ty - 5, 7, 11, C(T_accL), 2);
    }
    text(g, F_MICRO, "OLDER", tx0, ty + 4, C(T_line2));
    text(g, F_MICRO, "NEWER", tx1, ty + 4, C(T_line2), TA(AL_R));
    hints(g, "\xE2\x97\x80\xE2\x96\xB6|VERSION|\xE2\x8F\x8E|OPTIONS|`|BACK");
}

} // namespace orb
#endif
