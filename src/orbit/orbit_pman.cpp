#if defined(ORBIT_UI) || defined(ORBIT_NATIVE)
#include "orbit_screens.h"
#include <math.h>
#include <stdio.h>

// PARTITIONS — "flash map": the whole chip as one bar, each app slot in the
// colour of its cartridge on the home screen. Edits stay a draft until applied.
namespace orb {

void drawPartSwatch(Gfx &g, const PartRow &p, int x, int y, int w, int h, bool sel) {
    if (p.kind == PK_FREE) {
        g.rect(x, y, w, h, sel ? C(T_onAcc) : C(T_line2));
        return;
    }
    uint16_t c = p.kind == PK_LAUNCHER ? C(T_text)
                 : p.kind == PK_SYS    ? C(T_line2)
                 : p.kind == PK_APP    ? famC(p.fam, 0)
                 : p.kind == PK_OWNED  ? famC(p.fam, 1)
                                       : C(T_dim);
    g.fill(x, y, w, h, sel ? C(T_onAcc) : c);
    if (p.prot)
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++)
                if ((x + i + y + j) % 3 == 0) g.px(x + i, y + j, sel ? C(T_acc) : C(T_bg));
    if (p.kind == PK_OWNED)
        for (int j = 0; j < h; j += 2)
            for (int i = 0; i < w; i += 2) g.px(x + i, y + j, sel ? C(T_acc) : C(T_bg));
}

void PmanScene::build(int sel) {
    list.y = 34;
    list.h = FOOT_Y - 35;
    list.iconW = 11;
    int keep = sel >= 0 ? sel : list.src ? list.sel : 0;
    list.init(this, keep < count() ? keep : 0);
}
uint8_t PmanScene::kind(int i) {
    if (i < nrows) return RK_ITEM;
    if (i == nrows) return RK_SECTION;
    return ops[i - nrows - 1].kind;
}
int PmanScene::optAt(int i) const {
    if (i < nrows) return rows[i].opt;
    if (i == nrows) return -1;
    return ops[i - nrows - 1].opt;
}
void PmanScene::row(int i, Row &r) {
    if (i < nrows) {
        const PartRow &p = rows[i];
        r.label = p.kind == PK_FREE ? "Free space" : p.label;
        r.color = p.kind == PK_FREE || p.prot ? C(T_dim) : C(T_text);
        strcpy(r.right, p.right);
        return;
    }
    if (i == nrows) {
        r.kind = RK_SECTION;
        r.label = "TABLE";
        return;
    }
    const Op &o = ops[i - nrows - 1];
    r.label = o.label;
    r.icon = o.icon;
    r.kind = o.kind;
    if (o.icon == G_CHECK) r.color = C(T_acc);
}
bool PmanScene::icon(int i, Gfx &g, int x, int y, bool sel) {
    if (i >= nrows) return false;
    drawPartSwatch(g, rows[i], x + 1, y + 3, 6, 5, sel);
    return true;
}
int PmanScene::widget(int i, Gfx &g, int rx, int y, bool sel) {
    if (i >= nrows || !rows[i].prot) return rx;
    glyph(g, G_LOCK, rx - 5, y, sel ? C(T_onAcc) : C(T_line2));
    return rx - 6;
}
void PmanScene::key(uint8_t k, char) {
    if (list.key(k)) return;
    if (k == K_BACK) res = nops ? ops[nops - 1].opt : -1; // the Back row asks when there are edits
    else if (k == K_OK || k == K_HOLD) {
        int o = optAt(list.sel);
        if (o >= 0) res = o;
    }
}
void PmanScene::draw(Gfx &g) {
    const float tt = now;
    char s[48], a[16], b[16];
    g.clear(C(T_bg));
    Hdr h;
    h.icon = G_CHIP;
    h.title = "PARTITIONS";
    sprintf(s, "%d MB FLASH", (int)(flash >> 20));
    h.crumb = s;
    if (dirty) {
        h.right = ifloor(tt * 2) % 2 ? "\xE2\x80\xA2 UNSAVED" : "  UNSAVED";
        h.rightC = C(T_warn);
    }
    header(g, h);
    const int bx = 4, bw = 232, by = 15, bh = 9;
    auto sx = [&](uint32_t o) { return bx + jsround((float)o / flash * bw); };
    g.fill(bx, by, bw, bh, C(T_bg2));
    for (int i = sx(0); i < sx(0x10000); i++) // nvs, otadata, phy: not listed, hatched
        for (int j = 0; j < bh; j++)
            if ((i + j) % 2 == 0) g.px(i, by + j, C(T_line2));
    int sel = list.sel < nrows ? list.sel : -1;
    for (int k = 0; k < nrows; k++) {
        const PartRow &p = rows[k];
        int x0 = sx(p.ofs), x1 = sx(p.ofs + p.size);
        if (x1 < x0 + 2) x1 = x0 + 2;
        x1 -= 1;
        int w = x1 - x0;
        if (p.kind == PK_FREE) {
            for (int i = x0; i < x0 + w; i += 2) {
                g.px(i, by, C(T_line2));
                g.px(i, by + bh - 1, C(T_line2));
            }
            g.vline(x0, by, bh, C(T_line2));
        } else drawPartSwatch(g, p, x0, by, w > 1 ? w : 1, bh, false);
        if (k == sel) {
            bool blink = ifloor(tt * 3) % 2 == 0;
            int ww = w > 2 ? w : 2;
            g.hline(x0, by - 2, ww, C(T_acc));
            g.vline(x0, by - 2, 2, C(T_acc));
            g.vline(x0 + ww - 1, by - 2, 2, C(T_acc));
            g.hline(x0, by + bh + 1, ww, blink ? C(T_acc) : C(T_accD));
        }
    }
    int mb = flash >> 20, stepMb = mb > 8 ? 4 : 2;
    for (int m = 0; m <= mb; m += stepMb) {
        int x = sx((uint32_t)m << 20);
        g.vline(x < bx + bw - 1 ? x : bx + bw - 1, by + bh + 3, 2, C(T_line2));
        sprintf(s, "%dM", m);
        int cx = x < bx + 4 ? bx + 4 : x > bx + bw - 6 ? bx + bw - 6 : x;
        text(g, F_MICRO, s, cx, by + bh + 4, C(T_line2), TA(m == mb ? AL_R : m == 0 ? AL_L : AL_C));
    }
    g.hline(0, 32, W, C(T_line));
    list.draw(g);
    s[0] = 0;
    if (sel >= 0) {
        hex32(a, rows[sel].ofs);
        hex32(b, rows[sel].ofs + rows[sel].size);
        snprintf(s, sizeof(s), "%s \xE2\x80\x93 %s", a, b);
    }
    hints(g, sel >= 0 ? "\xE2\x8F\x8E|ACTIONS|`|BACK" : "\xE2\x8F\x8E|RUN|`|BACK", s, C(T_line2));
}

void InfoScene::draw(Gfx &g) {
    backdrop(g, 2);
    const int x = 30, y = 16, w = 180, h = 102;
    g.panel(x, y, w, h, C(T_bg2), C(T_line2), 3);
    g.fill(x + 3, y + 3, w - 6, 9, C(T_bg3));
    text(g, F_MICRO, title, x + 7, y + 5, C(T_acc));
    text(g, F_MICRO, status, x + w - 7, y + 5, statusC >= 0 ? (uint16_t)statusC : C(T_ok), TA(AL_R));
    for (int i = 0; i < n; i++) {
        text(g, F_MICRO, keys[i], x + 8, y + 18 + i * 10, C(T_line2));
        text(g, F_BODY, vals[i], x + 50, y + 16 + i * 10, C(T_text), TA(AL_L, w - 58));
    }
    text(g, F_MICRO, "ANY KEY", x + w - 7, y + h - 9, C(T_line2), TA(AL_R));
}

void RangeScene::key(uint8_t k, char) {
    if (k == K_UP || k == K_DOWN) h ^= 1;
    else if (k == K_LEFT || k == K_RIGHT) {
        bool up = k == K_RIGHT;
        if (h == 0) {
            if (up && start + step + minSize <= end) start += step;
            else if (!up && start >= minO + step) start -= step;
        } else {
            if (up && end + step <= maxO) end += step;
            else if (!up && end >= start + minSize + step) end -= step;
        }
    } else if (k == K_OK) res = 1;
    else if (k == K_BACK) res = 0;
}
void RangeScene::draw(Gfx &g) {
    const float tt = now;
    char a[16], b[16], c[16], s[64];
    g.clear(C(T_bg));
    Hdr hd;
    hd.icon = G_CHIP;
    hd.title = "EDIT SIZE";
    hd.crumb = label;
    header(g, hd);
    const int x0 = 14, w = 212, y = 58;
    auto sx = [&](uint32_t o) { return x0 + jsround((float)(o - minO) / (maxO - minO) * w); };
    hex32(a, start);
    hex32(b, end);
    snprintf(s, sizeof(s), "%s \xE2\x80\x93 %s", a, b);
    text(g, F_MICRO, "RANGE", 10, 16, C(T_line2));
    text(g, F_MICRO, s, 40, 16, C(T_text));
    hex32(a, end - start);
    fmtSize(c, end - start);
    snprintf(s, sizeof(s), "%s  (%s)", a, c);
    text(g, F_MICRO, "SIZE", 10, 25, C(T_line2));
    text(g, F_MICRO, s, 40, 25, C(T_acc));
    hex32(a, step);
    fmtSize(c, step);
    snprintf(s, sizeof(s), "%s (%s)", a, c);
    text(g, F_MICRO, "STEP", 10, 34, C(T_line2));
    text(g, F_MICRO, s, 40, 34, C(T_text));
    text(g, F_MICRO, h ? "MOVING END" : "MOVING START", 230, 16, C(T_acc), TA(AL_R));
    g.fill(x0, y, w, 10, C(T_bg2));
    g.rect(x0, y, w, 10, C(T_line2));
    for (int i = x0 + 1; i < x0 + w - 1; i += 2) g.px(i, y + 5, C(T_line));
    int ax = sx(start), bxx = sx(end);
    g.fill(ax, y + 2, bxx - ax, 6, famC(fam, 0));
    for (int k = 0; k < 2; k++) {
        int hx = k ? bxx : ax;
        bool on = h == k;
        g.fill(hx - 2, y - 6, 5, 22, on ? (ifloor(tt * 4) % 2 ? C(T_acc) : C(T_accL)) : C(T_line2));
        g.fill(hx - 1, y - 5, 3, 20, on ? C(T_acc) : C(T_dim));
    }
    hex32(a, minO);
    hex32(b, maxO);
    text(g, F_MICRO, a, x0, y + 20, C(T_line2));
    text(g, F_MICRO, b, x0 + w, y + 20, C(T_line2), TA(AL_R));
    text(g, F_MICRO, "PREVIOUS PARTITION ENDS", x0, y + 28, C(T_line2));
    text(g, F_MICRO, "NEXT ONE / FREE ENDS", x0 + w, y + 28, C(T_line2), TA(AL_R));
    hints(g, "\xE2\x97\x80\xE2\x96\xB6|MOVE|\xE2\x96\xB2\xE2\x96\xBC|HANDLE|\xE2\x8F\x8E|KEEP|`|CANCEL");
}

} // namespace orb
#endif
