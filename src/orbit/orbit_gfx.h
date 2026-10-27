// Orbit UI — raster core. Solid RGB565 into a band buffer (the whole screen
// when there is RAM for it), no alpha: fades and glows are 4x4 Bayer dithers.
// Plain C++ so the same code runs on the Cardputer and in a desktop test.
#pragma once
#if defined(ORBIT_UI) || defined(ORBIT_NATIVE)
#include "orbit_assets.h"
#include <stdint.h>
#include <string.h>

namespace orb {

constexpr int W = 240, H = 135;

extern uint16_t PAL[T_COUNT];
extern uint8_t themeIdx;
void setTheme(uint8_t idx);
inline uint16_t C(Tok t) { return PAL[t]; }

extern const uint8_t BAYER[16];
inline int bayer(int x, int y) { return BAYER[((y & 3) << 2) | (x & 3)]; }

// floor/ceil to int on the FPU (FLOOR.S/CEIL.S); libm's are a call plus a convert
inline int ifloor(float v) {
#if defined(__XTENSA__)
    int i;
    asm("floor.s %0, %1, 0" : "=a"(i) : "f"(v));
    return i;
#else
    int i = (int)v;
    return i - (v < i);
#endif
}
inline int iceil(float v) {
#if defined(__XTENSA__)
    int i;
    asm("ceil.s %0, %1, 0" : "=a"(i) : "f"(v));
    return i;
#else
    int i = (int)v;
    return i + (v > i);
#endif
}
float clampf(float v, float a, float b);
float expNeg(float x);          // e^-x, x >= 0
float atan2_(float y, float x); // atan2f without libm
float fsin(float x);            // sinf/cosf without libm's ~2 KB argument reduction
float fcos(float x);
inline float fmodp(float x, float y) { return x - y * ifloor(x / y); } // fmodf for x >= 0
float easeOut(float t);
float easeIn(float t);
float easeInOut(float t);
float easeBack(float t);
uint32_t fnv(const char *s);
float hash2(int x, int y);
float vnoise(float x, float y);
uint32_t rnd(); // small LCG for particles

enum Align : uint8_t { AL_L, AL_C, AL_R };
// Text options packed in one word (a struct per call site costs flash):
// bits 0-1 align, 2-9 max width in px (ellipsis when cut, 0 = none),
// 10 double scale, 16-21 outline colour token + 1 (0 = none)
typedef uint32_t TextOpt;
inline TextOpt TA(Align a, int max = 0) { return a | (uint32_t)(max < 0 ? 0 : max > 255 ? 255 : max) << 2; }
constexpr TextOpt T_BIG = 1u << 10;
inline TextOpt TOUT(TextOpt o, Tok outline) { return o | (uint32_t)(outline + 1) << 16; }

class Gfx {
public:
    uint16_t *buf = nullptr;
    int by0 = 0, bh = H;                    // band of rows held in buf
    int cx0 = 0, cy0 = 0, cx1 = W, cy1 = H; // clip, screen coords
    int bx0 = 0, bx1 = W;                   // base clip (a transition's half); clip() stays inside it
    int ox = 0, oy = 0;                     // draw offset
    uint8_t mask = 16;                      // dissolve mask: write only where bayer < mask

    void band(int y0, int h) {
        by0 = y0;
        bh = h;
    }
    void base(int x0, int x1) {
        bx0 = x0;
        bx1 = x1;
        resetClip();
    }
    void resetClip() {
        cx0 = bx0;
        cy0 = 0;
        cx1 = bx1;
        cy1 = H;
    }
    // clip to a rect (draw coords), inside the base clip
    void clip(int x, int y, int w, int h);
    // nested clips: save, clip, restore
    uint32_t saveClip() const {
        return (uint32_t)cx0 | (uint32_t)cy0 << 8 | (uint32_t)cx1 << 16 | (uint32_t)cy1 << 24;
    }
    void restoreClip(uint32_t v) {
        cx0 = v & 255;
        cy0 = (v >> 8) & 255;
        cx1 = (v >> 16) & 255;
        cy1 = v >> 24;
    }
    void px(int x, int y, uint16_t c);
    void fill(int x, int y, int w, int h, uint16_t c);
    void clear(uint16_t c) { fill(-ox, -oy, W, H, c); }
    void dfill(int x, int y, int w, int h, uint16_t c, int level);
    void checker(int x, int y, int w, int h, uint16_t c, int phase);
    void hline(int x, int y, int w, uint16_t c) { fill(x, y, w, 1, c); }
    void vline(int x, int y, int h, uint16_t c) { fill(x, y, 1, h, c); }
    void rect(int x, int y, int w, int h, uint16_t c);
    void panel(int x, int y, int w, int h, int32_t fillC, int32_t border, int cut = 2);
    void rrect(int x, int y, int w, int h, uint16_t c, int r = 1, bool filled = true);
    void line(int x0, int y0, int x1, int y1, uint16_t c);
    void dline(int x0, int y0, int x1, int y1, uint16_t c, int on = 1, int off = 1, int phase = 0);
    void circle(int cx, int cy, int r, uint16_t c);
    void fillCircle(int cx, int cy, int r, uint16_t c);
    // ring segment, angles in turns (0 = up, clockwise)
    void ring(float cx, float cy, float r0, float r1, float a0, float a1, uint16_t c, int level = 16);
    // visible rows for per-pixel effects (screen coords, already band+clip limited)
    int rowA(int y) const {
        int a = cy0 > by0 ? cy0 : by0;
        a -= oy;
        return y > a ? y : a;
    }
    int rowB(int y) const {
        int b = by0 + bh;
        if (cy1 < b) b = cy1;
        b -= oy;
        return y < b ? y : b;
    }
};
extern Gfx G;

// Sprites and glyph icons
enum PalMode : uint8_t { PM_FULL, PM_DIM, PM_FAR };
void blit(Gfx &g, const SpriteDef &s, int x, int y, PalMode m = PM_FULL, const uint16_t *xXZ = nullptr);
void glyph(Gfx &g, uint8_t id, int x, int y, uint16_t c, int32_t c2 = -1);
inline int glyphW(uint8_t id) { return glyph_w[id]; }
inline int glyphH(uint8_t id) { return glyph_h[id]; }

// Text (UTF-8)
int textW(uint8_t font, const char *s, int sp = -1);
int text(Gfx &g, uint8_t font, const char *s, int x, int y, uint16_t c, TextOpt o = 0);
// copies s into out, cut with an ellipsis to fit maxW
void fitText(uint8_t font, const char *s, int maxW, char *out, int outLen);
// word wrap: returns number of lines written into lines[] (each up to 63 chars)
int wrapText(uint8_t font, const char *s, int maxW, char lines[][64], int maxLines);

// Procedural emblem (identicon) for a firmware or app
bool emblemBit(const char *seed, int i, int j);
void emblem(Gfx &g, const char *seed, int x, int y, int cell, uint16_t on, int32_t off);

} // namespace orb
#endif
