#include "harness.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace orb;
static uint32_t clk = 0;
static int frameNo = 0, captureAt = 0;
static std::vector<Ev> evs;
static int filmStep = 0, filmN = 0; // --film: dump every filmStep-th frame
static std::string filmDir;
static size_t evi = 0;
static uint16_t fb[W * H];
struct Captured {};

namespace orb {
namespace plat {
uint32_t ms() { return clk; }
Input poll(bool, bool) {
    Input in;
    if (evi < evs.size() && evs[evi].t <= frameNo / 60.0f + 1e-6f) {
        in.k = evs[evi].k;
        in.ch = evs[evi].ch;
        evi++;
    }
    return in;
}
void push(int y0, int h, const uint16_t *px) { memcpy(fb + y0 * W, px, (size_t)h * W * 2); }
void idle(uint32_t) {
    clk += 17;
    frameNo++;
    if (filmStep && frameNo % filmStep == 0) {
        char p[512];
        snprintf(p, sizeof(p), "%s/f%04d.rgb565", filmDir.c_str(), filmN++);
        FILE *f = fopen(p, "wb");
        fwrite(fb, 2, W * H, f);
        fclose(f);
    }
    if (frameNo >= captureAt) {
        // capture before unwinding: scenes on the stack live in the frames we leave
        testSettle();
        redraw();
        throw Captured{};
    }
}
bool screenOff() { return false; }
void status(Status &s) { (void)s; }
} // namespace plat
} // namespace orb

std::vector<Ev> K(float t0, std::vector<std::pair<uint8_t, char>> keys, float gap) {
    std::vector<Ev> v;
    for (size_t i = 0; i < keys.size(); i++) v.push_back({t0 + i * gap, keys[i].first, keys[i].second});
    return v;
}
std::vector<Ev> downs(float t0, int n, float gap) {
    std::vector<Ev> v;
    for (int i = 0; i < n; i++) v.push_back({t0 + i * gap, K_DOWN, 0});
    return v;
}
std::vector<Ev> cat(std::vector<Ev> a, const std::vector<Ev> &b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}
void runForever() {
    for (;;) frame();
}

std::vector<Shot> SHOTS;
static uint16_t band[W * H];

namespace orb {
extern int qrForceMask;
}
int main(int argc, char **argv) {
    if (argc > 2 && !strcmp(argv[1], "--qr")) { // print all 8 masks for a reference check
        for (int m = 0; m < 8; m++) {
            qrForceMask = m;
            QR q;
            qrEncode(argv[2], q);
            for (int y = 0; y < 25; y++) {
                for (int x = 0; x < 25; x++) putchar(q.get(x, y) ? '#' : '.');
                putchar('\n');
            }
            putchar('\n');
        }
        return 0;
    }
    // --gallery <out> [ids]: README screens. --film <out> <step> <id>: frame sequence.
    bool gallery = argc > 1 && (!strcmp(argv[1], "--gallery") || !strcmp(argv[1], "--film"));
    if (argc > 1 && !strcmp(argv[1], "--film")) {
        filmDir = argv[2];
        filmStep = atoi(argv[3]);
        argv[3] = argv[2];
        argv += 2;
        argc -= 2;
    } else if (gallery) {
        argv++;
        argc--;
    }
    const char *out = argc > 1 ? argv[1] : ".";
    if (gallery) addGallery();
    else addShots();
    int done = 0;
    for (auto &s : SHOTS) {
        bool want = argc <= 2;
        for (int i = 2; i < argc; i++) want |= s.id == argv[i];
        if (!want) continue;
        evs = s.keys;
        evi = 0;
        frameNo = 0;
        captureAt = (int)(s.t * 60 + 0.5f);
        testReset();
        testDt = 1 / 60.0f;
        begin(band, H);
        memset(fb, 0, sizeof(fb));
        try {
            s.run();
        } catch (Captured &) {
        }
        std::string p = std::string(out) + "/" + s.id + ".rgb565";
        FILE *f = fopen(p.c_str(), "wb");
        fwrite(fb, 2, W * H, f);
        fclose(f);
        done++;
    }
    printf("rendered %d\n", done);
}
