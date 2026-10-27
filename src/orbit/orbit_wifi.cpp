#if defined(ORBIT_UI) || defined(ORBIT_NATIVE)
#include "orbit_screens.h"
#include <math.h>
#include <stdio.h>

// WI-FI — radar scan and network list, then the connection handshake. Blip
// distance from the centre is signal strength; blips light up as the sweep
// passes them.
namespace orb {

static const float TAU = 6.2831853f;
static float netAngle(const char *ssid) { return (fnv(ssid) % 6283) / 1000.0f; }
static float netDist(int rssi) { return clampf((-rssi - 38) / 56.0f, 0.08f, 1) * 42; }
static void upper(char *out, const char *in, int max) {
    int i = 0;
    for (; in[i] && i < max; i++) out[i] = in[i] >= 'a' && in[i] <= 'z' ? in[i] - 32 : in[i];
    out[i] = 0;
}

void wifiBars(Gfx &g, int x, int y, int rssi, uint16_t c, uint16_t cOff) {
    int n = rssi > -55 ? 4 : rssi > -67 ? 3 : rssi > -78 ? 2 : 1;
    for (int i = 0; i < 4; i++) g.fill(x + i * 2, y + 5 - (i + 1) - 1, 1, i + 2, i < n ? c : cOff);
}

void drawDevice(Gfx &g, int x, int y, uint16_t c) {
    g.rrect(x, y, 24, 18, C(T_line2), 1);
    g.fill(x + 1, y + 1, 22, 16, C(T_bg3));
    g.fill(x + 5, y + 3, 14, 7, C(T_bg));
    g.rect(x + 5, y + 3, 14, 7, c);
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 9; i++) g.px(x + 3 + i * 2, y + 12 + (int)(j * 1.6f), C(T_dim));
    g.fill(x + 2, y + 2, 2, 2, C(T_gold));
}

void drawRouter(Gfx &g, int x, int y, uint16_t c, float t) {
    g.vline(x + 5, y - 8, 9, C(T_dim));
    g.vline(x + 19, y - 8, 9, C(T_dim));
    g.rrect(x, y, 25, 12, C(T_line2), 1);
    g.fill(x + 1, y + 1, 23, 10, C(T_bg3));
    for (int i = 0; i < 4; i++)
        g.fill(x + 4 + i * 4, y + 7, 2, 2, ifloor(t * 6 + i) % 4 == 0 ? c : C(T_line2));
    for (int r = 3; r <= 6; r += 3) {
        g.ring(x + 12, y - 2, r - 0.5f, r + 0.5f, 0.875f, 1, c);
        g.ring(x + 12, y - 2, r - 0.5f, r + 0.5f, 0, 0.125f, c);
    }
}

// ── scan + list ─────────────────────────────────────────────────────────────
void WifiScene::results(float delay) {
    scanDone = true;
    t0 = t;
    sweep0 = fmodp(t * 2.4f, TAU);
    listAt = t + delay;
}
void WifiScene::showList(int sel) {
    state = S_LIST;
    list.x = 101;
    list.y = HDR_H + 2;
    list.w = W - 102;
    list.h = FOOT_Y - HDR_H - 4;
    list.rowH = 11;
    list.iconW = 10;
    list.init(this, sel);
}
void WifiScene::row(int i, Row &r) {
    if (i < n) {
        r.label = nets[i].ssid[0] ? nets[i].ssid : "(hidden)";
        return;
    }
    r.kind = RK_OP;
    if (i == n) {
        r.label = "Hidden network\xE2\x80\xA6";
        r.icon = G_EYE;
    } else {
        r.label = "Main menu";
        r.icon = G_HOME;
    }
}
int WifiScene::widget(int i, Gfx &g, int rx, int y, bool sel) {
    if (i >= n) return rx;
    const Net &nt = nets[i];
    wifiBars(g, rx - 7, y, nt.rssi, sel ? C(T_onAcc) : C(T_text), sel ? C(T_accD) : C(T_line2));
    if (nt.auth) glyph(g, G_LOCK, rx - 14, y, sel ? C(T_onAcc) : C(T_dim));
    if (nt.saved) glyph(g, G_STAR, rx - (nt.auth ? 21 : 14), y + 1, sel ? C(T_onAcc) : C(T_acc));
    return rx - (nt.saved ? 22 : nt.auth ? 15 : 8);
}
void WifiScene::key(uint8_t k, char) {
    if (state != S_LIST) {
        if (k == K_BACK) res = WF_BACK;
        return;
    }
    if (list.key(k)) return;
    if (k == K_BACK) res = WF_BACK;
    else if (k == K_OK) res = list.sel < n ? list.sel : list.sel == n ? WF_HIDDEN : WF_HOME;
}
void WifiScene::update(float dt, bool top) {
    if (scanDone) {
        float swept = (t - t0) * 2.4f;
        for (int i = 0; i < n && i < 32; i++) {
            float a = fmodp(netAngle(nets[i].ssid) - sweep0 + TAU, TAU);
            if (swept >= a) found |= 1u << i;
        }
    }
    if (state == S_SCAN && scanDone && t > listAt) {
        if (autoNet >= 0) {
            state = S_AUTO;
            at = 0;
        } else showList();
    }
    if (state == S_AUTO && top) {
        at += dt;
        if (at > 0.9f) {
            showList(autoNet);
            res = autoNet;
        }
    }
    if (state == S_LIST) list.update(dt);
}
void WifiScene::draw(Gfx &g) {
    const float tt = t;
    char s[48];
    g.clear(C(T_bg));
    Hdr h;
    h.icon = G_WIFI;
    h.title = "WI-FI";
    if (state == S_SCAN) h.crumb = "SCANNING";
    else if (state == S_AUTO) h.crumb = "KNOWN NETWORK";
    else {
        sprintf(s, "%d NETWORKS", n);
        h.crumb = s;
    }
    header(g, h);
    // radar
    const int cx = 51, cy = 69, R = 46;
    float sweep = fmodp(tt * 2.4f, TAU);
    g.fillCircle(cx, cy, R, C(T_bg2));
    int ja = g.rowA(cy - R) - cy, jb = g.rowB(cy + R + 1) - cy;
    for (int j = ja; j < jb; j++)
        for (int i = -R; i <= R; i++) {
            int d2 = i * i + j * j;
            if (d2 > R * R) continue;
            float a = atan2_((float)i, (float)-j);
            if (a < 0) a += TAU;
            float diff = sweep - a;
            if (diff < 0) diff += TAU;
            if (diff < 1.1f) {
                int lvl = jsround((1 - diff / 1.1f) * 9);
                if (bayer(cx + i, cy + j) < lvl) g.px(cx + i, cy + j, diff < 0.25f ? C(T_accD) : C(T_line));
            }
        }
    for (int r = 14; r <= 42; r += 14)
        for (int k = 0; k < 96; k += 2) {
            float a = k / 96.0f * TAU;
            g.px(jsround(cx + fsin(a) * r), jsround(cy - fcos(a) * r), C(T_line2));
        }
    g.circle(cx, cy, R, C(T_line2));
    g.dline(cx - R, cy, cx + R, cy, C(T_line), 1, 2);
    g.dline(cx, cy - R, cx, cy + R, C(T_line), 1, 2);
    for (int k = 0; k < 36; k++) {
        float a = k / 36.0f * TAU;
        int r0 = k % 3 == 0 ? R - 3 : R - 1;
        g.line(
            jsround(cx + fsin(a) * r0),
            jsround(cy - fcos(a) * r0),
            jsround(cx + fsin(a) * R),
            jsround(cy - fcos(a) * R),
            C(T_line2)
        );
    }
    g.line(cx, cy, jsround(cx + fsin(sweep) * (R - 1)), jsround(cy - fcos(sweep) * (R - 1)), C(T_acc));
    g.fill(cx - 1, cy - 1, 3, 3, C(T_hi));
    text(g, F_MICRO, "-50", cx + 2, cy - 20, C(T_line2));
    text(g, F_MICRO, "-70", cx + 2, cy - 34, C(T_line2));
    // blips, with phosphor persistence
    int selNet = state == S_LIST && list.sel < n ? list.sel : -1;
    for (int i = 0; i < n && i < 32; i++) {
        if (!(found >> i & 1)) continue;
        float a = netAngle(nets[i].ssid), d = netDist(nets[i].rssi);
        int bx = jsround(cx + fsin(a) * d), by = jsround(cy - fcos(a) * d);
        float age = sweep - a;
        if (age < 0) age += TAU;
        float fresh = 1 - age / TAU;
        uint16_t c = fresh > 0.8f ? C(T_hi) : fresh > 0.45f ? C(T_acc) : C(T_accD);
        g.fill(bx - 1, by - 1, 2, 2, c);
        if (fresh > 0.85f) {
            g.px(bx - 2, by - 1, C(T_accD));
            g.px(bx + 1, by - 1, C(T_accD));
        }
        if (i == selNet || (state == S_AUTO && i == autoNet)) {
            bool blink = ifloor(tt * 5) % 2 == 0;
            corners(g, bx - 5, by - 5, 9, 9, blink ? C(T_acc) : C(T_accL), 2);
            char up[16];
            upper(up, nets[i].ssid, 12);
            text(
                g,
                F_MICRO,
                up,
                bx + (bx > cx ? -6 : 6),
                by - 12 < 14 ? by + 7 : by - 12,
                C(T_text),
                TOUT(TA(bx > cx ? AL_R : AL_L), T_bg2)
            );
        }
    }
    if (state == S_SCAN || state == S_AUTO) {
        const int x = 106;
        text(g, F_MICRO, state == S_SCAN ? "LISTENING ON 2.4 GHZ" : "AUTO-CONNECT", x, 20, C(T_dim));
        text(g, F_ORBIT, state == S_SCAN ? "SCANNING" : "FOUND", x, 30, state == S_SCAN ? C(T_hi) : C(T_acc));
        int shown[6], ns = 0;
        for (int i = 0; i < n && i < 32; i++)
            if (found >> i & 1) {
                if (ns == 6) {
                    for (int k = 0; k < 5; k++) shown[k] = shown[k + 1];
                    ns = 5;
                }
                shown[ns++] = i;
            }
        for (int k = 0; k < ns; k++) {
            const Net &nt = nets[shown[k]];
            int y = 46 + k * 10;
            text(
                g, F_BODY, nt.ssid, x, y, k == ns - 1 && state == S_SCAN ? C(T_acc) : C(T_text), TA(AL_L, 96)
            );
            wifiBars(g, 228, y + 1, nt.rssi, C(T_dim), C(T_line));
        }
        for (int k = 0; k < ns; k++)
            if (state == S_AUTO && shown[k] == autoNet) {
                g.fill(x - 2, 46 + k * 10 - 1, 134, 10, C(T_bg3));
                text(g, F_BODY, nets[autoNet].ssid, x, 46 + k * 10, C(T_acc));
            }
        if (state == S_AUTO) text(g, F_MICRO, "SAVED \xC2\xB7 CONNECTING", x, 112, C(T_acc));
        else {
            spinner(g, x, 113, C(T_acc));
            sprintf(s, "CH %d", 1 + ifloor(tt * 9) % 13);
            text(g, F_MICRO, s, x + 7, 113, C(T_dim));
        }
        hints(g, "`|CANCEL");
    } else {
        list.draw(g);
        g.vline(100, HDR_H + 1, FOOT_Y - HDR_H - 1, C(T_line));
        s[0] = 0;
        if (list.sel < n) {
            const Net &nt = nets[list.sel];
            sprintf(s, "%s%d DBM%s", nt.auth ? "WPA2 " : "OPEN ", nt.rssi, nt.saved ? " \xC2\xB7 SAVED" : "");
        }
        hints(
            g, list.sel == n + 1 ? "\xE2\x8F\x8E|EXIT|`|BACK" : "\xE2\x8F\x8E|CONNECT|`|BACK", s, C(T_line2)
        );
    }
}

// ── handshake ───────────────────────────────────────────────────────────────
void ConnectScene::key(uint8_t k, char) {
    if (k == K_BACK && state == S_TRY) res = 0;
}
void ConnectScene::update(float dt, bool) {
    if (state != S_TRY) lt += dt;
}
void ConnectScene::draw(Gfx &g) {
    const float tt = t;
    char up[40], s[48];
    upper(up, ssid, 32);
    g.clear(C(T_bg));
    Hdr h;
    h.icon = G_WIFI;
    h.title = "WI-FI";
    h.crumb = ssid;
    header(g, h);
    const int y = 58;
    drawDevice(g, 30, y - 9, state == S_LINKED ? C(T_ok) : C(T_acc));
    drawRouter(g, 186, y - 3, state == S_BROKEN ? C(T_err) : state == S_LINKED ? C(T_ok) : C(T_acc), tt);
    const int x0 = 58, x1 = 182;
    if (state == S_LINKED) {
        g.hline(x0, y, x1 - x0, C(T_ok));
        g.hline(x0, y + 1, x1 - x0, C(T_okD));
    } else if (state == S_BROKEN) {
        const int mid = 120;
        g.dline(x0, y, mid - 8, y, C(T_err), 2, 2);
        g.dline(mid + 8, y, x1, y, C(T_line2), 2, 2);
        glyph(g, G_CROSS, mid - 2, y - 2, C(T_err));
    } else {
        g.dline(x0, y, x1, y, C(T_line2), 2, 2);
        for (int k = 0; k < 3; k++) {
            float f = fmodp(tt * 0.9f + k / 3.0f, 1);
            int px = jsround(x0 + (x1 - x0) * f);
            g.fill(px - 1, y - 1, 3, 3, C(T_acc));
            g.px(px - 3, y, C(T_accD));
        }
        for (int k = 0; k < 2; k++) {
            float f = 1 - fmodp(tt * 0.7f + k / 2.0f, 1);
            int px = jsround(x0 + (x1 - x0) * f);
            g.fill(px, y + 3, 2, 2, C(T_sec));
        }
    }
    const char *title = state == S_LINKED   ? "LINKED"
                        : state == S_BROKEN ? (failPassword ? "REJECTED" : "NO REPLY")
                                            : "HANDSHAKE";
    text(
        g,
        F_ORBIT,
        title,
        120,
        78,
        state == S_LINKED   ? C(T_ok)
        : state == S_BROKEN ? C(T_err)
                            : C(T_hi),
        TA(AL_C)
    );
    if (state == S_TRY) snprintf(s, sizeof(s), "CONNECTING TO %s", up);
    else if (state == S_BROKEN)
        strcpy(s, failPassword ? "AUTH FAILED \xC2\xB7 4-WAY HANDSHAKE" : "TIMED OUT");
    else strcpy(s, sub);
    text(g, F_MICRO, s, 120, 92, C(T_dim), TA(AL_C, 230));
    if (state == S_TRY) {
        int n = tries < 20 ? tries : 20;
        sprintf(s, "TRY %02d/20", n);
        text(g, F_MICRO, s, 120, 101, C(T_line2), TA(AL_C));
        segbar(g, 70, 108, 100, 2, n / 20.0f, C(T_line2), -1, C(T_bg3), 4);
    }
    text(g, F_MICRO, "LAUNCHER", 42, y + 12, C(T_line2), TA(AL_C));
    up[12] = 0;
    text(g, F_MICRO, up, 198, y + 12, C(T_line2), TA(AL_C));
    hints(g, state == S_TRY ? "`|CANCEL" : "");
}

} // namespace orb
#endif
