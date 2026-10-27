#if defined(ORBIT_UI) || defined(ORBIT_NATIVE)
#include "orbit_screens.h"
#include <math.h>
#include <stdio.h>

// BOOT — "T-minus". The rocket on the pad is the app that will boot. Enter holds
// the launch and opens Launcher, any key boots now (asks which app if several are
// installed), 1-9/0 boots that slot, and at T-0 the rocket lifts off.
namespace orb {

enum PtKind : uint8_t { P_STEAM, P_FIRE, P_SMOKE, P_TRAIL };

void BootScene::start(Particle *pool, int poolSize) {
    parts = pool;
    maxParts = poolSize;
    nparts = 0;
    if (!bootToApp || app < 0) total = 1.2f;
}
void BootScene::launch(int i) {
    app = i;
    state = S_LAUNCH;
    st = 0;
}
void BootScene::hold() {
    state = S_HOLD;
    st = 0;
}
float BootScene::remaining() const { return total - t > 0 ? total - t : 0; }

void BootScene::key(uint8_t k, char ch) {
    if (state != S_COUNT) return;
    if (k == K_OK) return hold();
    if (k == K_CHAR && ch >= '0' && ch <= '9') {
        int i = ch == '0' ? 9 : ch - '1';
        if (i < napps) launch(i);
        return;
    }
    if (k == K_BACK || k == K_CHAR || k == K_LEFT || k == K_RIGHT || k == K_UP || k == K_DOWN || k == K_DEL) {
        if (!bootToApp || !napps) return hold();
        if (napps == 1) return launch(0);
        state = S_PICK;
        res = 100;
    }
}

void BootScene::emit(uint8_t kind, float x, float y, float vx, float vy, float max, float s) {
    if (nparts >= maxParts) return;
    parts[nparts++] = {x, y, vx, vy, 0, max, s, kind};
}

void BootScene::update(float dt, bool) {
    st += dt;
    if (state == S_COUNT && t >= total) {
        if (bootToApp && app >= 0) {
            timedOut = true;
            launch(app);
        } else {
            state = S_HOLD;
            st = 0.3f;
        }
    }
    if (state == S_PICK) t -= dt; // the countdown waits while the picker is open
    if (state == S_HOLD && st > 0.75f) {
        state = S_DONE;
        res = -1;
    }
    if (state == S_LAUNCH) {
        if (st > 0.85f) {
            vy += dt * (40 + st * 90);
            lift += vy * dt * 2.2f;
        }
        if (st > 0.5f && st < 2.2f && shake < 0.05f) shake = 0.05f;
        if (st > 2.7f) {
            state = S_DONE;
            res = timedOut ? 200 : app;
        }
    }
    Mulberry &r = rng;
    if (state == S_COUNT || state == S_HOLD || state == S_PICK) {
        if (r.next() < dt * 3) {
            float x = 70 + r.next() * 3, vx = -4 - r.next() * 6, vy_ = -3 - r.next() * 3,
                  mx = 1.6f + r.next();
            emit(P_STEAM, x, 108, vx, vy_, mx, 1);
        }
        if (r.next() < dt * 2) {
            float vx = 4 + r.next() * 6, vy_ = -2 - r.next() * 3, mx = 1.4f + r.next();
            emit(P_STEAM, 104, 108, vx, vy_, mx, 1);
        }
    }
    if (state == S_LAUNCH) {
        float ny = 104 - lift, rate = st < 0.85f ? 60 : 110;
        for (int i = 0; i < rate * dt; i++) {
            float x = 86 + (r.next() - .5f) * 6, vx = (r.next() - .5f) * 14, vy_ = 30 + r.next() * 40,
                  mx = 0.25f + r.next() * 0.3f;
            emit(P_FIRE, x, ny, vx, vy_, mx, 0);
        }
        if (ny > 60)
            for (int i = 0; i < 26 * dt; i++) {
                float side = r.next() < .5f ? -1 : 1;
                float x = 86 + side * (4 + r.next() * 6), y = 110 + r.next() * 2,
                      vx = side * (25 + r.next() * 45), vy_ = -4 - r.next() * 10, mx = 1.6f + r.next() * 1.2f,
                      s = 2 + r.next() * 2;
                emit(P_SMOKE, x, y, vx, vy_, mx, s);
            }
        if (lift > 4)
            for (int i = 0; i < 30 * dt; i++) {
                float x = 86 + (r.next() - .5f) * 4, vx = (r.next() - .5f) * 6, vy_ = 4 + r.next() * 6,
                      mx = 1.2f + r.next(), s = 1 + r.next() * 1.5f;
                emit(P_TRAIL, x, ny + 6, vx, vy_, mx, s);
            }
    }
    int k = 0;
    for (int i = 0; i < nparts; i++) {
        Particle &p = parts[i];
        p.life += dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        if (p.kind == P_SMOKE || p.kind == P_STEAM) {
            p.vx *= 0.97f;
            p.vy -= dt * 2;
            p.s += dt * (p.kind == P_SMOKE ? 3 : 1.4f);
        }
        if (p.kind == P_FIRE && p.y > 111) {
            p.y = 111;
            p.vy = 0;
            p.vx *= 3;
        }
        if (p.life < p.max) parts[k++] = p;
    }
    nparts = k;
}

void BootScene::drawScene(Gfx &g, float tt) {
    g.clear(C(T_sky0));
    for (int y = 18; y < 112; y++)
        g.dfill(0, y, W, 1, C(T_sky1), jsround(clampf((y - 18) / 60.0f, 0, 1) * 16));
    for (int y = 70; y < 112; y++)
        g.dfill(0, y, W, 1, C(T_sky2), jsround(clampf((y - 70) / 42.0f, 0, 1) * 13));
    drawStars(g, tt, 66);
    // ridge and horizon lights
    for (int x = 0; x < W; x++) {
        int ry = jsround(103 - 5 * vnoise(x * 0.045f, 3.1f) - 3 * vnoise(x * 0.16f, 8.4f));
        g.vline(x, ry, 112 - ry, C(T_pl1));
        g.px(x, ry, C(T_pl2));
    }
    static const float lights[6][2] = {
        {8,   0   },
        {21,  2   },
        {150, 1   },
        {168, 3   },
        {201, 1.5f},
        {229, 4   }
    };
    for (auto &l : lights)
        if (fsin(tt * 1.3f + l[1]) > -0.4f) g.px((int)l[0], 109, C(T_city));
    // ground and perspective lines
    g.fill(0, 112, W, H - 112, C(T_pl0));
    g.hline(0, 112, W, C(T_pl2));
    for (int k = -6; k <= 8; k++) g.dline(87, 113, 87 + k * 46, H, C(T_pl1), 1, 2);
    // floodlights
    static const int fl[2][2] = {
        {26,  1 },
        {120, -1}
    };
    for (auto &f : fl) {
        int lx = f[0], aim = f[1];
        g.vline(lx, 70, 42, C(T_line2));
        g.fill(lx - 1, 68, 3, 2, C(T_hi));
        g.px(lx, 67, C(T_atm));
        for (int y = 70; y < 112; y++) {
            float span = (y - 70) / 42.0f;
            int xa = jsround(lx + aim * (span * 50)), xb = jsround(lx + aim * (10 + span * 62));
            int x0 = xa < xb ? xa : xb, x1 = xa < xb ? xb : xa;
            g.dfill(x0, y, x1 - x0, 1, C(T_line2), 4 - jsround(span * 2));
        }
    }
    // pad
    g.fill(62, 106, 50, 4, C(T_line2));
    g.hline(62, 106, 50, C(T_dim));
    g.fill(77, 110, 20, 2, C(T_ink));
    g.fill(64, 110, 3, 2, C(T_line2));
    g.fill(107, 110, 3, 2, C(T_line2));
    // gantry
    const int gx = 100, top = 44;
    for (int y = top; y < 106; y += 6) {
        g.line(gx, y, gx + 9, y + 6, C(T_line2));
        g.line(gx + 9, y, gx, y + 6, C(T_line2));
        g.hline(gx, y, 10, C(T_line2));
    }
    g.vline(gx, top, 62, C(T_dim));
    g.vline(gx + 9, top, 62, C(T_line2));
    g.fill(gx - 2, top - 2, 14, 2, C(T_dim));
    g.vline(gx + 4, top - 11, 9, C(T_dim));
    if (ifloor(tt * 1.6f) % 2 == 0) {
        g.fill(gx + 3, top - 13, 2, 2, C(T_err));
        g.px(gx + 2, top - 12, C(T_errD));
        g.px(gx + 5, top - 12, C(T_errD));
    }
    float retract = state == S_LAUNCH ? clampf(st / 0.5f, 0, 1) : 0;
    int armL = jsround(6 * (1 - retract));
    if (armL > 0) {
        g.hline(gx - armL, 66, armL, C(T_dim));
        g.hline(gx - armL, 84, armL, C(T_dim));
        g.px(gx - armL, 67, C(T_dim));
    }
}

void BootScene::drawParts(Gfx &g, uint8_t kind) {
    for (int n = 0; n < nparts; n++) {
        const Particle &p = parts[n];
        if (p.kind != kind) continue;
        float f = p.life / p.max;
        if (kind == P_FIRE) {
            uint16_t c = f < 0.25f  ? C(T_flame0)
                         : f < 0.5f ? C(T_flame1)
                         : f < 0.8f ? C(T_flame2)
                                    : C(T_flame3);
            g.px(jsround(p.x), jsround(p.y), c);
            g.px(jsround(p.x), jsround(p.y) + 1, c);
            continue;
        }
        int r = jsround(p.s), lvl = jsround((1 - f) * (kind == P_SMOKE ? 14 : 7));
        uint16_t c = kind == P_SMOKE   ? (f < 0.3f   ? C(T_text)
                                          : f < 0.6f ? C(T_dim)
                                                     : C(T_line2))
                     : kind == P_TRAIL ? (f < 0.2f   ? C(T_flame1)
                                          : f < 0.5f ? C(T_dim)
                                                     : C(T_line2))
                                       : C(T_line2);
        for (int j = -r; j <= r; j++)
            for (int i = -r; i <= r; i++)
                if (i * i + j * j <= r * r + r) {
                    int x = jsround(p.x + i), y = jsround(p.y + j);
                    if (bayer(x, y) < lvl) g.px(x, y, c);
                }
    }
}

void BootScene::draw(Gfx &g) {
    const float tt = now;
    char s[48], sz[12];
    drawScene(g, tt);
    drawParts(g, P_STEAM);
    drawParts(g, P_TRAIL);
    if (app >= 0) {
        int ry = jsround(58 - lift);
        if (state == S_LAUNCH) { // engine glow and plume
            int fy = ry + 46, len = st < 0.85f ? 3 + jsround(st * 6) : 10 + jsround(fsin(tt * 40) * 2);
            for (int j = 0; j < len; j++) {
                int w = jsround(4 - j * 0.25f + fsin(tt * 50 + j) * 0.8f);
                if (w < 1) w = 1;
                g.hline(
                    86 - w,
                    fy + j,
                    2 * w + 1,
                    j < 2            ? C(T_flame0)
                    : j < len * 0.5f ? C(T_flame1)
                                     : C(T_flame2)
                );
            }
            g.hline(84, fy, 6, C(T_hi));
        }
        blit(g, SPR_ROCKET, 78, ry);
    }
    drawParts(g, P_FIRE);
    drawParts(g, P_SMOKE);
    // UI
    text(g, F_ORBIT, "LAUNCHER", 6, 5, C(T_hi));
    snprintf(s, sizeof(s), "V%s \xC2\xB7 GH/BMORCELLI", version);
    text(g, F_MICRO, s, 6, 17, C(T_dim));
    statusCluster(g, W - 3, 2);
    const int px = 132;
    if (state == S_LAUNCH || state == S_DONE) {
        const BootApp &a = apps[app];
        text(g, F_MICRO, st < 0.85f ? "MAIN ENGINE START" : "LIFTOFF", px, 28, C(T_warn));
        text(g, F_ORBIT, st < 0.85f ? "IGNITION" : "BOOTING", px, 36, C(T_acc));
        text(g, F_BODY, a.name, px, 52, C(T_hi), TA(AL_L, 100));
        fmtSize(sz, a.size);
        char lab[18];
        int i = 0;
        for (; a.label[i] && i < 17; i++)
            lab[i] = a.label[i] >= 'a' && a.label[i] <= 'z' ? a.label[i] - 32 : a.label[i];
        lab[i] = 0;
        snprintf(s, sizeof(s), "SLOT %s \xC2\xB7 %s", lab, sz);
        text(g, F_MICRO, s, px, 63, C(T_dim));
        segbar(g, px, 72, 100, 3, clampf(st / 2.6f, 0, 1), C(T_warn), C(T_flame0));
    } else {
        bool noApp = app < 0 || !bootToApp;
        text(
            g,
            F_MICRO,
            noApp ? (app >= 0 ? "BOOT TO LAUNCHER IS ON" : "NO APP INSTALLED") : "AUTO-BOOT IN",
            px,
            26,
            C(T_dim)
        );
        bool hold_ = state == S_HOLD;
        float rem = hold_ ? (total - (t - st) > 0 ? total - (t - st) : 0) : remaining();
        int secs = iceil(rem - 1e-6f), tenth = ifloor(fmodp(rem, 1) * 10);
        int tx = px + text(g, F_ORBIT, "T-", px, 34, hold_ ? C(T_warn) : C(T_line2), T_BIG);
        snprintf(s, sizeof(s), "%02d", secs);
        text(g, F_ORBIT, s, tx + 4, 34, hold_ ? C(T_warn) : C(T_acc), T_BIG);
        snprintf(s, sizeof(s), ".%d", tenth);
        text(g, F_MICRO, s, px + 98, 47, C(T_dim));
        segbar(g, px, 56, 100, 3, clampf(rem / total, 0, 1), hold_ ? C(T_warn) : C(T_acc));
        if (hold_ && ifloor(st * 8) % 2 == 0) g.panel(px - 2, 30, 74, 22, -1, C(T_warn), 2);
        if (app >= 0 && bootToApp) {
            text(g, F_MICRO, "PAYLOAD", px, 64, C(T_dim));
            text(g, F_BODY, apps[app].name, px + 30, 62, C(T_hi), TA(AL_L, 70));
            for (int i = 0; i < napps && i < 4; i++) {
                int y = 75 + i * 9;
                bool cur = i == app;
                char k[2] = {(char)('0' + (i + 1) % 10), 0};
                int kw = keycap(g, k, px, y, cur);
                char up[22];
                int j = 0;
                for (; apps[i].name[j] && j < 21; j++)
                    up[j] = apps[i].name[j] >= 'a' && apps[i].name[j] <= 'z' ? apps[i].name[j] - 32
                                                                             : apps[i].name[j];
                up[j] = 0;
                text(g, F_MICRO, up, px + kw + 3, y + 1, cur ? C(T_acc) : C(T_dim), TA(AL_L, 64));
                fmtSize(sz, apps[i].size);
                text(g, F_MICRO, sz, px + 100, y + 1, C(T_line2), TA(AL_R));
            }
        } else text(g, F_BODY, "Opening Launcher", px, 64, C(T_text));
        if (!hold_) {
            int gx = px;
            const char *K[3] = {"SD ", "BAT ", "FLASH "};
            bool ok[3] = {ST.sd, !ST.hasBattery || ST.battery > 15, true};
            for (int i = 0; i < 3; i++) {
                gx += text(g, F_MICRO, K[i], gx, 112, C(T_line2));
                gx += text(g, F_MICRO, ok[i] ? "GO" : "NO", gx, 112, ok[i] ? C(T_ok) : C(T_err)) + 8;
            }
        } else text(g, F_MICRO, "HOLD \xC2\xB7 OPENING LAUNCHER", px, 112, C(T_warn));
    }
    // hints on the ground
    if (app >= 0 && bootToApp) {
        snprintf(s, sizeof(s), "\xE2\x8F\x8E|LAUNCHER|ANY|BOOT NOW|1-%d|PICK APP", napps < 9 ? napps : 9);
        hintsOver(g, s, 126, -1);
    } else hintsOver(g, "\xE2\x8F\x8E|LAUNCHER", 126, -1);
}

} // namespace orb
#endif
