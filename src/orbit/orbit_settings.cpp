#if defined(ORBIT_UI) || defined(ORBIT_NATIVE)
#include "orbit_screens.h"
#include <math.h>
#include <stdio.h>

// SETTINGS — grouped list with inline widgets: left/right adjusts values in
// place, Enter toggles or opens. Brightness and theme pickers preview live.
// Also: key binding, the installed-app dossier.
namespace orb {

static void upper(char *out, const char *in, int max) {
    int i = 0;
    for (; in[i] && i < max; i++) out[i] = in[i] >= 'a' && in[i] <= 'z' ? in[i] - 32 : in[i];
    out[i] = 0;
}

void drawSwatches(Gfx &g, int x, int y, uint8_t th, bool sel) {
    const Tok cs[4] = {T_acc, T_sec, T_text, T_bg2};
    for (int i = 0; i < 4; i++) g.fill(x + i * 5, y, 4, 7, THEME_COLORS[th][cs[i]]);
    g.rect(x - 1, y - 1, 21, 9, sel ? C(T_onAcc) : C(T_line));
}

void SettingsScene::begin(int sel) {
    for (int i = 0; i < n; i++) items[i].anim = items[i].on ? 1 : 0;
    list.iconW = 11;
    list.init(this, sel);
}
uint8_t SettingsScene::kind(int i) {
    uint8_t k = items[i].kind;
    return k == SK_SECTION ? RK_SECTION : k == SK_DANGER ? RK_DANGER : k == SK_OP ? RK_OP : RK_ITEM;
}
void SettingsScene::row(int i, Row &r) {
    const SetItem &it = items[i];
    r.label = it.label;
    r.icon = it.icon;
    r.kind = kind(i);
}
int SettingsScene::widget(int i, Gfx &g, int rx, int y, bool sel) {
    const SetItem &it = items[i];
    uint16_t vc = sel ? C(T_onAcc) : C(T_sec);
    switch (it.kind) {
        case SK_TOGGLE: toggle(g, rx - 13, y, it.on, sel, it.anim); return rx - 13;
        case SK_VALUE:
        case SK_ACTION: {
            if (!it.val[0]) return rx;
            text(g, F_MICRO, it.val, rx, y + 1, vc, TA(AL_R));
            int w = textW(F_MICRO, it.val);
            if (it.adj) {
                text(g, F_MICRO, "\xE2\x97\x80", rx - w - 6, y + 1, sel ? C(T_onAcc) : C(T_line2));
                return rx - w - 8;
            }
            return rx - w;
        }
        case SK_BAR: {
            text(g, F_MICRO, it.val, rx, y + 1, vc, TA(AL_R));
            int w = textW(F_MICRO, it.val);
            miniBar(g, rx - w - 34, y + 3, 30, it.bar, sel);
            return rx - w - 36;
        }
        case SK_THEME: {
            text(g, F_MICRO, it.val, rx, y + 1, vc, TA(AL_R));
            int w = textW(F_MICRO, it.val);
            drawSwatches(g, rx - w - 24, y, themeIdx, sel);
            return rx - w - 26;
        }
    }
    return rx;
}
void SettingsScene::key(uint8_t k, char) {
    if (list.key(k)) return;
    int i = list.sel;
    const SetItem &it = items[i];
    if (k == K_BACK) res = -1;
    else if (k == K_LEFT || k == K_RIGHT) {
        if (it.adj) res = i * 4 + (k == K_LEFT ? SA_LEFT : SA_RIGHT);
        else if (it.kind == SK_TOGGLE && (k == K_RIGHT) != it.on) res = i * 4 + SA_OPEN;
    } else if (k == K_OK || k == K_HOLD) res = i * 4 + SA_OPEN;
}
void SettingsScene::update(float dt, bool) {
    list.update(dt);
    for (int i = 0; i < n; i++) {
        float tgt = items[i].on ? 1 : 0, a = items[i].anim;
        items[i].anim = fabsf(tgt - a) < 0.05f ? tgt : a + (tgt - a) * (dt * 16 < 1 ? dt * 16 : 1);
    }
}
void SettingsScene::draw(Gfx &g) {
    g.clear(C(T_bg));
    const char *sec = "";
    for (int i = list.sel; i >= 0; i--)
        if (items[i].kind == SK_SECTION) {
            sec = items[i].label;
            break;
        }
    char ver[24];
    snprintf(ver, sizeof(ver), "V%s", version);
    Hdr h;
    h.icon = G_GEAR;
    h.title = "SETTINGS";
    h.crumb = sec;
    h.right = ver;
    header(g, h);
    list.draw(g);
    const SetItem &it = items[list.sel];
    hints(
        g,
        it.kind == SK_TOGGLE ? "\xE2\x8F\x8E|TOGGLE|\xE2\x97\x80\xE2\x96\xB6|OFF/ON|`|BACK"
        : it.adj             ? "\xE2\x97\x80\xE2\x96\xB6|ADJUST|\xE2\x8F\x8E|OPEN|`|BACK"
                             : "\xE2\x8F\x8E|OPEN|`|BACK"
    );
}

// ── brightness ──────────────────────────────────────────────────────────────
void BrightnessScene::key(uint8_t k, char) {
    int was = val;
    if (k == K_LEFT || k == K_DOWN) val = val - 5 < 5 ? 5 : val - 5;
    else if (k == K_RIGHT || k == K_UP) val = val + 5 > 100 ? 100 : val + 5;
    else if (k == K_OK) res = 1;
    else if (k == K_BACK) res = 0;
    if (val != was && preview) preview(val);
}
void BrightnessScene::draw(Gfx &g) {
    backdrop(g, 2);
    const int x = 26, y = 28, w = 188, h = 78;
    g.panel(x, y, w, h, C(T_bg2), C(T_line2), 3);
    text(g, F_MICRO, "BRIGHTNESS", x + 8, y + 6, C(T_acc));
    const int cx = x + 26, cy = y + 38;
    float v = val / 100.0f;
    g.fillCircle(cx, cy, 5, C(T_warn));
    g.fillCircle(cx, cy, 3, C(T_goldL));
    for (int k = 0; k < 8; k++) {
        float a = k / 8.0f * 6.2831853f + now * 0.4f;
        int r0 = 8, r1 = 8 + jsround(2 + 6 * v);
        g.line(
            jsround(cx + fcos(a) * r0),
            jsround(cy + fsin(a) * r0),
            jsround(cx + fcos(a) * r1),
            jsround(cy + fsin(a) * r1),
            k % 2 ? C(T_warn) : C(T_gold)
        );
    }
    char s[8];
    sprintf(s, "%d", val);
    int pw = text(g, F_ORBIT, s, x + 56, y + 20, C(T_hi), T_BIG);
    text(g, F_ORBIT, "%", x + 59 + pw, y + 29, C(T_dim));
    const int sx = x + 56, sy = y + 46;
    for (int i = 0; i < 20; i++) {
        bool on = (i + 1) * 5 <= val;
        int hg = 4 + jsround(i * 0.35f);
        g.fill(sx + i * 6, sy + 12 - hg, 5, hg, on ? (i == val / 5 - 1 ? C(T_accL) : C(T_acc)) : C(T_line));
    }
    text(g, F_MICRO, "5", sx, sy + 15, C(T_line2));
    text(g, F_MICRO, "100", sx + 119, sy + 15, C(T_line2), TA(AL_R));
    text(
        g,
        F_MICRO,
        "\xE2\x97\x80\xE2\x96\xB6 5% \xC2\xB7 \xE2\x8F\x8E KEEP \xC2\xB7 ` UNDO",
        x + w - 8,
        y + 6,
        C(T_dim),
        TA(AL_R)
    );
}

// ── theme ───────────────────────────────────────────────────────────────────
void ColorScene::key(uint8_t k, char) {
    if (k == K_LEFT || k == K_UP) {
        sel = (sel + THEME_COUNT - 1) % THEME_COUNT;
        setTheme(sel);
        ct = 0;
    } else if (k == K_RIGHT || k == K_DOWN) {
        sel = (sel + 1) % THEME_COUNT;
        setTheme(sel);
        ct = 0;
    } else if (k == K_OK) res = 1;
    else if (k == K_BACK) res = 0;
}
void ColorScene::draw(Gfx &g) {
    backdrop(g, 1);
    const int y = 30, h = 76;
    g.fill(0, y - 8, W, h + 22, C(T_bg2));
    g.hline(0, y - 8, W, C(T_line2));
    g.hline(0, y + h + 13, W, C(T_line2));
    text(g, F_MICRO, "UI COLOR", 6, y - 4, C(T_acc));
    text(
        g,
        F_MICRO,
        "\xE2\x97\x80\xE2\x96\xB6 PREVIEW \xC2\xB7 \xE2\x8F\x8E KEEP \xC2\xB7 ` UNDO",
        234,
        y - 4,
        C(T_dim),
        TA(AL_R)
    );
    const int cw = 30, gap = 3, total = THEME_COUNT * (cw + gap) - gap, x0 = jsround((W - total) / 2.0f);
    for (int i = 0; i < THEME_COUNT; i++) {
        const uint16_t *th = THEME_COLORS[i];
        int x = x0 + i * (cw + gap);
        bool s = i == sel;
        int lift = s ? jsround(easeOut(clampf(ct / 0.15f, 0, 1)) * 3) : 0, yy = y + 6 - lift;
        g.fill(x, yy, cw, 44, th[T_bg]);
        g.rect(x, yy, cw, 44, s ? C(T_acc) : C(T_line));
        g.fill(x + 1, yy + 1, cw - 2, 5, th[T_bg2]);
        g.fill(x + 2, yy + 2, 3, 3, th[T_acc]);
        g.hline(x + 7, yy + 3, 12, th[T_text]);
        g.hline(x + 1, yy + 6, cw - 2, th[T_line]);
        g.fill(x + 2, yy + 9, cw - 4, 5, th[T_acc]);
        g.hline(x + 4, yy + 11, 14, th[T_onAcc]);
        for (int r = 0; r < 3; r++)
            g.hline(x + 4, yy + 17 + r * 4, 12 + (r * 7) % 9, r ? th[T_dim] : th[T_text]);
        for (int i2 = 1; i2 < cw - 1; i2++) {
            float q = i2 - cw / 2.0f, d2 = 22 * 22 - q * q;
            int dy = jsround(sqrtf(d2 > 0 ? d2 : 0)), py = yy + 44 + 16 - dy;
            if (py < yy + 43 && py > yy + 28) {
                g.px(x + i2, py, th[T_atm]);
                for (int k = py + 1; k < yy + 43; k++) g.px(x + i2, k, th[T_pl1]);
            }
        }
        text(g, F_MICRO, THEME_NAMES[i], x + cw / 2, yy + 48, s ? C(T_acc) : C(T_dim), TA(AL_C));
        if (s) corners(g, x - 2, yy - 2, cw + 4, 48, C(T_accL), 3);
    }
    char was[24], note[40], s[80];
    upper(was, THEME_WAS[sel], 23);
    upper(note, THEME_NOTE[sel], 39);
    snprintf(s, sizeof(s), "REPLACES \"%s\" \xC2\xB7 %s", was, note);
    text(g, F_MICRO, s, 120, y + h + 4, C(T_text), TA(AL_C));
}

// ── key binding ─────────────────────────────────────────────────────────────
void BindScene::key(uint8_t k, char ch) {
    if (got) return;
    if (k == K_BACK || k == K_DEL || (k == K_CHAR && ch == '`')) {
        res = 0;
        return;
    }
    char c = ch >= 'A' && ch <= 'Z' ? ch + 32 : ch;
    if (k == K_OK || (k == K_CHAR && strchr(",;/.", c)) || k != K_CHAR || c <= ' ') {
        mt = 0;
        shake = 0.1f;
        return;
    }
    got = c;
    bt = 0;
}
void BindScene::update(float dt, bool) {
    mt += dt;
    if (got) {
        bt += dt;
        if (bt > 0.45f && res == RUNNING) res = got;
    }
}
void BindScene::draw(Gfx &g) {
    backdrop(g, 2);
    const int x = 14, y = 20, w = 212, h = 92;
    g.panel(x, y, w, h, C(T_bg2), C(T_acc), 3);
    text(g, F_MICRO, "BIND TO KEY", x + 8, y + 6, C(T_acc));
    text(g, F_BODY, name, x + 8, y + 15, C(T_hi), TA(AL_L, w - 16));
    char s[48];
    if (got) sprintf(s, "BOUND TO '%c'", got >= 'a' && got <= 'z' ? got - 32 : got);
    text(
        g, F_MICRO, got ? s : "PRESS THE KEY THAT SHOULD LAUNCH IT", x + 8, y + 27, got ? C(T_ok) : C(T_dim)
    );
    drawKeyMap(g, x + 9, y + 38, got, true, bound);
    if (mt < 1.4f) text(g, F_MICRO, "KEY RESERVED FOR NAVIGATION", x + w / 2, y + h - 7, C(T_err), TA(AL_C));
    else
        text(
            g,
            F_MICRO,
            "RED = NAVIGATION \xC2\xB7 BLUE = ALREADY BOUND",
            x + w / 2,
            y + h - 7,
            C(T_line2),
            TA(AL_C)
        );
}

// ── installed app ───────────────────────────────────────────────────────────
void AppScene::begin() {
    list.x = 104;
    list.y = 14;
    list.w = 134;
    list.h = FOOT_Y - 16;
    list.iconW = 10;
    list.init(&menu, 0);
}
void AppScene::key(uint8_t k, char) {
    if (list.key(k)) return;
    if (k == K_BACK) res = -1;
    else if (k == K_OK || k == K_HOLD) res = list.sel;
}
void AppScene::draw(Gfx &g) {
    char up[24], s[40];
    upper(up, name, 23);
    g.clear(C(T_bg));
    Hdr h;
    h.icon = G_CART;
    h.title = "APP";
    h.crumb = up;
    header(g, h);
    g.fill(6, 14, 94, FOOT_Y - 15, C(T_bg2));
    uint16_t pal[3] = {famC(fam, 0), famC(fam, 1), famC(fam, 2)};
    blit(g, ICO_APP, 10, 18, PM_FULL, pal);
    text(g, F_BODY, ini, 26, 30, C(T_ink), TA(AL_C));
    if (bootsNext) {
        g.fill(46, 20, 50, 9, C(T_acc));
        text(g, F_MICRO, "\xE2\x96\xB6 BOOTS NEXT", 49, 22, C(T_onAcc));
    }
    text(g, F_MICRO, "SLOT", 46, 33, C(T_line2));
    upper(up, label, 17);
    text(g, F_MICRO, up, 46, 40, C(T_text), TA(AL_L, 52));
    for (int i = 0; i < nrows; i++) {
        text(g, F_MICRO, keys[i], 10, 56 + i * 9, C(T_line2));
        bool none = !strcmp(keys[i], "BACKUP") && !strcmp(vals[i], "NONE YET");
        text(g, F_MICRO, vals[i], 96, 56 + i * 9, none ? C(T_line2) : C(T_text), TA(AL_R, 54));
    }
    list.draw(g);
    snprintf(s, sizeof(s), "OTA SLOT \xC2\xB7 %s", up);
    hints(g, "\xE2\x8F\x8E|RUN|`|BACK", s, C(T_line2));
}

} // namespace orb
#endif
