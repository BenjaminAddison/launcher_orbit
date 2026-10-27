#if defined(ORBIT_UI) || defined(ORBIT_NATIVE)
#include "orbit_screens.h"
#include <math.h>
#include <stdio.h>

// WEB UI — choose my network or access point, then the beacon screen with a
// scannable QR code of the address, the credentials, uptime and a request log.
// USB — the SD card as a mass-storage drive, with read/write activity.
namespace orb {

// ── QR, version 2-M, byte mode ──────────────────────────────────────────────
static uint8_t gfMul(uint8_t a, uint8_t b) {
    uint8_t r = 0;
    while (b) {
        if (b & 1) r ^= a;
        a = (a << 1) ^ (a & 0x80 ? 0x1D : 0);
        b >>= 1;
    }
    return r;
}
static void qrSet(uint8_t *m, int x, int y, bool on) {
    int i = y * 25 + x;
    if (on) m[i >> 3] |= 1 << (i & 7);
    else m[i >> 3] &= ~(1 << (i & 7));
}
static bool qrGet(const uint8_t *m, int x, int y) {
    int i = y * 25 + x;
    return m[i >> 3] >> (i & 7) & 1;
}
// function modules: finders + separators, timing, alignment, format areas, dark module
static bool qrFunc(int x, int y) {
    if ((x < 9 && y < 9) || (x >= 17 && y < 9) || (x < 9 && y >= 17)) return true;
    if (x == 6 || y == 6) return true;
    if (x >= 16 && x <= 20 && y >= 16 && y <= 20) return true;
    return false;
}
static int qrPenalty(const uint8_t *m) {
    int p = 0, dark = 0;
    for (int pass = 0; pass < 2; pass++)
        for (int a = 0; a < 25; a++) {
            int run = 0;
            bool last = false;
            uint32_t bits = 0;
            for (int b = 0; b < 25; b++) {
                bool v = pass ? qrGet(m, a, b) : qrGet(m, b, a);
                if (!pass && v) dark++;
                if (b && v == last) run++;
                else {
                    if (run >= 5) p += run - 2;
                    run = 1;
                    last = v;
                }
                bits = (bits << 1 | v) & 0x7FF;
                if (b >= 10 && (bits == 0x5D0 || bits == 0x05D)) p += 40; // 1011101 with 4 light
            }
            if (run >= 5) p += run - 2;
        }
    for (int y = 0; y < 24; y++)
        for (int x = 0; x < 24; x++) {
            bool v = qrGet(m, x, y);
            if (v == qrGet(m, x + 1, y) && v == qrGet(m, x, y + 1) && v == qrGet(m, x + 1, y + 1)) p += 3;
        }
    int k = dark * 20 / 625 - 10;
    return p + (k < 0 ? -k : k) * 10;
}
static bool qrMask(int mask, int x, int y) {
    switch (mask) {
        case 0: return (x + y) % 2 == 0;
        case 1: return y % 2 == 0;
        case 2: return x % 3 == 0;
        case 3: return (x + y) % 3 == 0;
        case 4: return (y / 2 + x / 3) % 2 == 0;
        case 5: return x * y % 2 + x * y % 3 == 0;
        case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
    }
    return ((x + y) % 2 + x * y % 3) % 2 == 0;
}
#if defined(ORBIT_NATIVE)
int qrForceMask = -1; // harness: compare one mask against a reference encoder
#endif
bool qrEncode(const char *text, QR &out) {
    const int ND = 28, NE = 16;
    int len = strlen(text);
    if (len > 26) return false;
    // data codewords: mode 0100, 8-bit count, bytes, terminator, pad
    uint8_t cw[ND + NE];
    memset(cw, 0, sizeof(cw));
    int bit = 0;
    auto put = [&](uint32_t v, int n) {
        for (int i = n - 1; i >= 0; i--, bit++)
            if (v >> i & 1) cw[bit >> 3] |= 0x80 >> (bit & 7);
    };
    put(4, 4);
    put(len, 8);
    for (int i = 0; i < len; i++) put((uint8_t)text[i], 8);
    bit += 4; // terminator
    int nb = (bit + 7) >> 3;
    for (int i = nb, k = 0; i < ND; i++, k++) cw[i] = k & 1 ? 0x11 : 0xEC;
    // Reed-Solomon: generator for 16 ecc codewords
    uint8_t gen[NE + 1];
    memset(gen, 0, sizeof(gen));
    gen[0] = 1;
    uint8_t root = 1;
    for (int i = 0; i < NE; i++) {
        for (int j = i + 1; j > 0; j--) gen[j] = gen[j - 1] ^ gfMul(gen[j], root);
        gen[0] = gfMul(gen[0], root);
        root = gfMul(root, 2);
    }
    uint8_t rem[NE];
    memset(rem, 0, sizeof(rem));
    for (int i = 0; i < ND; i++) {
        uint8_t f = cw[i] ^ rem[0];
        memmove(rem, rem + 1, NE - 1);
        rem[NE - 1] = 0;
        for (int j = 0; j < NE; j++) rem[j] ^= gfMul(gen[NE - 1 - j], f);
    }
    memcpy(cw + ND, rem, NE);
    // function patterns
    uint8_t base[79];
    memset(base, 0, sizeof(base));
    auto finder = [&](int cx, int cy) {
        for (int dy = -4; dy <= 4; dy++)
            for (int dx = -4; dx <= 4; dx++) {
                int x = cx + dx, y = cy + dy;
                if (x < 0 || y < 0 || x >= 25 || y >= 25) continue;
                int d = dx < 0 ? -dx : dx, e = dy < 0 ? -dy : dy, r = d > e ? d : e;
                qrSet(base, x, y, r != 2 && r != 4);
            }
    };
    finder(3, 3);
    finder(21, 3);
    finder(3, 21);
    for (int i = 8; i < 17; i++) {
        qrSet(base, i, 6, i % 2 == 0);
        qrSet(base, 6, i, i % 2 == 0);
    }
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) {
            int d = dx < 0 ? -dx : dx, e = dy < 0 ? -dy : dy, r = d > e ? d : e;
            qrSet(base, 18 + dx, 18 + dy, r != 1);
        }
    qrSet(base, 8, 17, true); // dark module
    // data placement: two-column zigzag from the bottom right
    uint8_t dm[79];
    memcpy(dm, base, sizeof(dm));
    int bi = 0;
    for (int right = 24; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        for (int vert = 0; vert < 25; vert++)
            for (int j = 0; j < 2; j++) {
                int x = right - j;
                bool upward = ((right + 1) & 2) == 0;
                int y = upward ? 24 - vert : vert;
                if (qrFunc(x, y)) continue;
                bool v = bi < (ND + NE) * 8 && (cw[bi >> 3] >> (7 - (bi & 7)) & 1);
                bi++;
                qrSet(dm, x, y, v);
            }
    }
    // mask + format, pick the lowest penalty
    int best = -1, bestP = 1 << 30;
    uint8_t trial[79];
    for (int mask = 0; mask < 8; mask++) {
#if defined(ORBIT_NATIVE)
        if (qrForceMask >= 0 && mask != qrForceMask) continue;
#endif
        memcpy(trial, dm, sizeof(trial));
        for (int y = 0; y < 25; y++)
            for (int x = 0; x < 25; x++)
                if (!qrFunc(x, y) && qrMask(mask, x, y)) qrSet(trial, x, y, !qrGet(trial, x, y));
        // format: ECC level M (00) + mask, BCH(15,5), xor 0x5412
        int data = mask, rem2 = data;
        for (int i = 0; i < 10; i++) rem2 = (rem2 << 1) ^ ((rem2 >> 9) * 0x537);
        int fmt = ((data << 10) | rem2) ^ 0x5412;
        for (int i = 0; i <= 5; i++) qrSet(trial, 8, i, fmt >> i & 1);
        qrSet(trial, 8, 7, fmt >> 6 & 1);
        qrSet(trial, 8, 8, fmt >> 7 & 1);
        qrSet(trial, 7, 8, fmt >> 8 & 1);
        for (int i = 9; i < 15; i++) qrSet(trial, 14 - i, 8, fmt >> i & 1);
        for (int i = 0; i < 8; i++) qrSet(trial, 24 - i, 8, fmt >> i & 1);
        for (int i = 8; i < 15; i++) qrSet(trial, 8, 25 - 15 + i, fmt >> i & 1);
        qrSet(trial, 8, 17, true);
        int p = qrPenalty(trial);
        if (p < bestP) {
            bestP = p;
            best = mask;
            memcpy(out.m, trial, sizeof(out.m));
        }
    }
    return best >= 0;
}

static void drawQR(Gfx &g, const QR &q, int x, int y, int s, uint16_t dark, uint16_t light, float reveal) {
    g.fill(x - 2 * s, y - 2 * s, 29 * s, 29 * s, light);
    for (int j = 0; j < 25; j++)
        for (int i = 0; i < 25; i++)
            if (q.get(i, j) && (i + j) / 50.0f <= reveal) g.fill(x + i * s, y + j * s, s, s, dark);
}

// ── mode cards ──────────────────────────────────────────────────────────────
void WebModeScene::key(uint8_t k, char) {
    if (k == K_LEFT || k == K_RIGHT || k == K_UP || k == K_DOWN) {
        sel ^= 1;
        ct = 0;
    } else if (k == K_BACK) res = -1;
    else if (k == K_OK) res = sel;
}
void WebModeScene::draw(Gfx &g) {
    g.clear(C(T_bg));
    drawStars(g, now, H);
    Hdr h;
    h.icon = G_GLOBE;
    h.title = "WEB UI";
    h.crumb = "CHOOSE A NETWORK";
    header(g, h);
    char join[48];
    snprintf(join, sizeof(join), ssid[0] ? "Join %s" : "Scan and join", ssid);
    const char *T[2] = {"MY NETWORK", "ACCESS POINT"}, *A[2] = {join, "Make network \"Launcher\""},
               *B[2] = {"HTTP://LAUNCHER.LOCAL", apUrl},
               *Cc[2] = {ssid[0] ? "CONNECTED" : "NEEDS WI-FI", "OPEN \xC2\xB7 CH 6"};
    for (int i = 0; i < 2; i++) {
        const int x = 8 + i * 116, y = 22, w = 108, hh = 88;
        bool s = i == sel;
        g.panel(x, y, w, hh, s ? C(T_bg3) : C(T_bg2), s ? C(T_acc) : C(T_line), 3);
        if (s) corners(g, x - 2, y - 2, w + 4, hh + 4, C(T_accL), 3);
        const int ix = x + 10, iy = y + 12;
        if (i == 0) drawRouter(g, ix, iy + 10, s ? C(T_acc) : C(T_dim), now);
        else {
            g.vline(ix + 12, iy, 18, s ? C(T_acc) : C(T_dim));
            for (int r = 4; r <= 12; r += 4) {
                float pulse = fmodp(now * 1.2f + r / 12.0f, 1);
                uint16_t c = pulse > 0.5f && s ? C(T_accL) : C(T_line2);
                g.ring(ix + 12, iy + 3, r - .5f, r + .5f, 0.62f, 0.88f, c);
                g.ring(ix + 12, iy + 3, r - .5f, r + .5f, 0.12f, 0.38f, c);
            }
            g.fill(ix + 9, iy + 18, 7, 2, s ? C(T_acc) : C(T_dim));
        }
        text(g, F_MICRO, T[i], x + 46, y + 12, s ? C(T_acc) : C(T_text));
        text(
            g,
            F_MICRO,
            Cc[i],
            x + 46,
            y + 20,
            s ? (i == 0 && !ssid[0] ? C(T_warn) : C(T_ok)) : C(T_line2),
            TA(AL_L, 58)
        );
        char lines[2][64];
        int nl = wrapText(F_BODY, A[i], w - 14, lines, 2);
        for (int k = 0; k < nl; k++)
            text(g, F_BODY, lines[k], x + 7, y + 44 + k * 10, s ? C(T_hi) : C(T_text));
        text(g, F_MICRO, B[i], x + 7, y + 72, C(T_dim), TA(AL_L, w - 12));
    }
    hints(g, "\xE2\x97\x80\xE2\x96\xB6|CHOOSE|\xE2\x8F\x8E|START|`|BACK");
}

// ── server running ──────────────────────────────────────────────────────────
volatile uint32_t WebRunScene::reqCount = 0;
char WebRunScene::reqLog[4][40];
float WebRunScene::reqAt[4];
void WebRunScene::logRequest(const char *line) {
    uint32_t n = reqCount;
    char *d = reqLog[n % 4];
    int i = 0;
    for (; line[i] && i < 39; i++) d[i] = line[i] >= 'a' && line[i] <= 'z' ? line[i] - 32 : line[i];
    d[i] = 0;
    reqAt[n % 4] = now;
    reqCount = n + 1;
}
void WebRunScene::begin() {
    char url[32];
    snprintf(url, sizeof(url), "http://%s/", ip);
    qrOk = qrEncode(url, qr);
    reqCount = 0;
}
void WebRunScene::key(uint8_t k, char) {
    if (k == K_OK || k == K_BACK) res = 0;
}
void WebRunScene::draw(Gfx &g) {
    const float tt = t;
    char s[64];
    g.clear(C(T_bg));
    Hdr h;
    h.icon = G_GLOBE;
    h.title = "WEB UI";
    char up[36];
    int i = 0;
    for (; ssid[i] && i < 32; i++) up[i] = ssid[i] >= 'a' && ssid[i] <= 'z' ? ssid[i] - 32 : ssid[i];
    up[i] = 0;
    h.crumb = ap ? "ACCESS POINT" : up;
    header(g, h);
    const int qx = 10, qy = 20;
    if (qrOk) drawQR(g, qr, qx, qy, 2, 0x1083, 0xF7BF, clampf(t / 0.6f, 0, 1));
    corners(g, qx - 7, qy - 7, 64, 64, C(T_acc), 4);
    text(g, F_MICRO, "SCAN TO OPEN", qx + 25, qy + 61, C(T_dim), TA(AL_C));
    const int x = 76;
    text(g, F_MICRO, "OPEN IN A BROWSER", x, 16, C(T_dim));
    text(g, F_ORBIT, ap ? ip : "LAUNCHER.LOCAL", x, 24, C(T_hi), TA(AL_L, 160));
    snprintf(s, sizeof(s), "http://%s", ap ? "launcher.local" : ip);
    text(g, F_BODY, s, x, 37, C(T_acc), TA(AL_L, 160));
    const char *K[3], *V[3];
    if (ap) {
        K[0] = "WI-FI", V[0] = "Launcher (open)";
        K[1] = "USER", V[1] = user;
        K[2] = "PASS", V[2] = pass;
    } else {
        K[0] = "USER", V[0] = user;
        K[1] = "PASS", V[1] = pass;
        K[2] = "MDNS", V[2] = "launcher.local";
    }
    for (int k = 0; k < 3; k++) {
        text(g, F_MICRO, K[k], x, 52 + k * 10, C(T_line2));
        text(g, F_BODY, V[k], x + 24, 50 + k * 10, C(T_text), TA(AL_L, 136));
    }
    // beacon
    const int bx = 222, by = 22;
    g.vline(bx, by, 10, C(T_dim));
    g.fill(bx - 2, by + 10, 5, 2, C(T_dim));
    g.fill(bx - 1, by - 1, 3, 3, ifloor(tt * 3) % 2 ? C(T_acc) : C(T_accL));
    for (int k = 0; k < 3; k++) {
        float f = fmodp(tt * 0.7f + k / 3.0f, 1), r = 3 + f * 12;
        uint16_t c = f < 0.5f ? C(T_acc) : C(T_accD);
        int lvl = jsround((1 - f) * 16);
        g.ring(bx, by, r - .5f, r + .5f, 0.65f, 0.85f, c, lvl);
        g.ring(bx, by, r - .5f, r + .5f, 0.15f, 0.35f, c, lvl);
    }
    // live strip
    int up_ = ifloor(tt);
    g.fill(4, 88, 232, 34, C(T_bg2));
    g.hline(4, 88, 232, C(T_line));
    bool dot = ifloor(tt * 2) % 2 == 0;
    g.fill(8, 92, 3, 3, dot ? C(T_ok) : C(T_okD));
    text(g, F_MICRO, "LIVE", 14, 92, C(T_ok));
    sprintf(s, "UP %02d:%02d:%02d", (up_ / 3600) % 100, (up_ / 60) % 60, up_ % 60);
    text(g, F_MICRO, s, 34, 92, C(T_dim));
    uint32_t n = reqCount;
    sprintf(s, "REQ %u", (unsigned)n);
    text(g, F_MICRO, s, 232, 92, C(T_dim), TA(AL_R));
    int first = n > 3 ? n - 3 : 0;
    for (uint32_t k = first; k < n; k++) {
        float age = now - reqAt[k % 4];
        snprintf(s, sizeof(s), "%s%s", age < 0.3f ? "\xE2\x96\xB6 " : "  ", reqLog[k % 4]);
        text(g, F_MICRO, s, 8, 101 + (k - first) * 7, age < 0.5f ? C(T_text) : C(T_line2), TA(AL_L, 224));
    }
    hints(g, "\xE2\x8F\x8E|STOP SERVER", ap ? "CLIENTS JOIN \"LAUNCHER\"" : "SAME NETWORK ONLY", C(T_line2));
}

// ── USB drive ───────────────────────────────────────────────────────────────
void UsbScene::key(uint8_t k, char) {
    if ((k == K_OK || k == K_BACK) && state != S_EJECT) {
        state = S_EJECT;
        et = 0;
    }
}
void UsbScene::update(float dt, bool) {
    if (rd != lastRd || wr != lastWr) {
        writing = wr != lastWr;
        lastRd = rd;
        lastWr = wr;
        lastAct = t;
    }
    if (state == S_EJECT) {
        et += dt;
        if (et > 0.9f && res == RUNNING) res = 0;
    }
}
void UsbScene::draw(Gfx &g) {
    const float tt = t;
    char s[48], a[12];
    bool act = t - lastAct < 0.25f;
    g.clear(C(T_bg));
    Hdr h;
    h.icon = G_USB;
    h.title = "USB DRIVE";
    h.crumb = "SD AS MASS STORAGE";
    header(g, h);
    const int dx = 16, dy = 30, dw = 58, dh = 38;
    bool mounted = state == S_MOUNTED;
    // the Cardputer with its SD card
    g.fill(dx + 40, dy - 9, 11, 12, C(T_acc));
    g.fill(dx + 40, dy - 9, 11, 2, C(T_accL));
    g.rect(dx + 39, dy - 10, 13, 13, C(T_ink));
    for (int i = 0; i < 4; i++) g.px(dx + 42 + i * 2, dy - 7, C(T_gold));
    g.rrect(dx, dy, dw, dh, C(T_line2), 2);
    g.fill(dx + 2, dy + 2, dw - 4, dh - 4, C(T_bg3));
    g.fill(dx + 6, dy + 4, 30, 15, C(T_bg));
    g.rect(dx + 6, dy + 4, 30, 15, mounted ? C(T_ok) : C(T_acc));
    glyph(g, G_USB, dx + 19, dy + 8, mounted ? C(T_ok) : C(T_acc));
    g.fill(dx + 40, dy + 5, 12, 3, C(T_gold));
    g.fill(dx + 40, dy + 10, 12, 1, C(T_line2));
    g.fill(dx + 40, dy + 13, 8, 1, C(T_line2));
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 13; i++)
            g.fill(
                dx + 4 + i * 4,
                dy + 22 + j * 3,
                3,
                2,
                (i == 12 && j == 2) || (i == 11 && j == 3) ? C(T_line2) : C(T_bg2)
            );
    g.fill(dx + dw - 1, dy + 16, 3, 6, C(T_dim));
    // the computer
    const int lx = 168, ly = 30;
    g.rrect(lx, ly, 50, 32, C(T_line2), 1);
    g.fill(lx + 2, ly + 2, 46, 28, C(T_bg2));
    g.fill(lx + 2, ly + 2, 46, 5, C(T_bg3));
    g.px(lx + 4, ly + 4, C(T_err));
    g.px(lx + 6, ly + 4, C(T_warn));
    g.px(lx + 8, ly + 4, C(T_ok));
    if (state != S_WAIT) {
        glyph(g, G_FOLDERF, lx + 7, ly + 11, C(T_sec));
        glyph(g, G_FOLDERF, lx + 20, ly + 11, C(T_sec));
        glyph(g, G_FOLDERF, lx + 33, ly + 11, C(T_sec));
        text(g, F_MICRO, "LAUNCHER SD", lx + 25, ly + 21, C(T_dim), TA(AL_C));
    } else spinner(g, lx + 23, ly + 15, C(T_acc));
    g.fill(lx - 5, ly + 32, 60, 3, C(T_line2));
    g.hline(lx - 5, ly + 32, 60, C(T_dim));
    g.fill(lx + 20, ly + 33, 10, 1, C(T_line));
    // the cable
    const float x0 = dx + dw + 2, y0 = dy + 19, cx = 120, cy = 92, x1 = lx - 5, y1 = ly + 33;
    auto P = [&](float u, float &x, float &y) {
        float a_ = (1 - u) * (1 - u), b = 2 * u * (1 - u), c = u * u;
        x = a_ * x0 + b * cx + c * x1;
        y = a_ * y0 + b * cy + c * y1;
    };
    for (int i = 0; i <= 90; i++) {
        float x, y;
        P(i / 90.0f, x, y);
        if (state == S_WAIT ? i % 3 != 2 : true) {
            g.px(jsround(x), jsround(y), mounted ? C(T_dim) : C(T_line2));
            g.px(jsround(x), jsround(y) + 1, C(T_line));
        }
    }
    if (mounted && act)
        for (int k = 0; k < 3; k++) {
            float u = fmodp(tt * 1.6f + k / 3.0f, 1), x, y;
            bool back = writing && k == 2;
            P(back ? 1 - u : u, x, y);
            g.fill(jsround(x) - 1, jsround(y) - 1, 3, 3, back ? C(T_sec) : C(T_acc));
        }
    // status
    const char *st = state == S_WAIT ? "WAITING" : state == S_EJECT ? "EJECTING" : "MOUNTED";
    uint16_t led = state == S_WAIT    ? (ifloor(tt * 3) % 2 ? C(T_warn) : C(T_bg3))
                   : state == S_EJECT ? C(T_warn)
                                      : (act && ifloor(tt * 14) % 2 ? C(T_okL) : C(T_ok));
    g.fill(16, 88, 5, 5, led);
    g.rect(15, 87, 7, 7, C(T_line2));
    text(g, F_ORBIT, st, 26, 86, state == S_MOUNTED ? C(T_ok) : C(T_hi));
    text(
        g,
        F_MICRO,
        state == S_WAIT ? "PLUG THE USB-C PORT INTO A COMPUTER" : card,
        16,
        100,
        C(T_dim),
        TA(AL_L, 220)
    );
    fmtSize(a, rd);
    snprintf(s, sizeof(s), "READ %s", a);
    text(g, F_MICRO, s, 16, 110, act && !writing ? C(T_acc) : C(T_line2));
    fmtSize(a, wr);
    snprintf(s, sizeof(s), "WRITE %s", a);
    text(g, F_MICRO, s, 84, 110, act && writing ? C(T_sec) : C(T_line2));
    hints(g, "\xE2\x8F\x8E|EJECT AND EXIT", "EJECT ON THE COMPUTER FIRST", C(T_line2));
}

} // namespace orb
#endif
