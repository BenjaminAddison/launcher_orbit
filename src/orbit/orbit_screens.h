// Orbit UI — the screens. Each takes a small view model filled by the bridge
// (from Launcher state on the device, from sample data in the desktop harness)
// and reports what the user chose through Scene::res.
#pragma once
#if defined(ORBIT_UI) || defined(ORBIT_NATIVE)
#include "orbit_ui.h"

namespace orb {

int jsround(float v); // Math.round
struct Mulberry {     // the mockup's PRNG, so starfields match it exactly
    uint32_t a;
    float next(); // [0, 1): the top 24 bits, exact in a float
};

// App colour families: installed apps take them in install order
enum Fam : uint8_t { FAM_SEC, FAM_GOLD, FAM_OK, FAM_ACC };
extern char installed[10][18]; // labels, in install order
extern int ninstalled;
uint8_t famOf(const char *label);
uint16_t famC(uint8_t fam, int shade); // shade 0 base, 1 light, 2 dark

// ── shared art ──────────────────────────────────────────────────────────────
void drawSky(Gfx &g, int y0 = 20, int y1 = 112);
void drawStars(Gfx &g, float t, int maxY = H, float drift = 0, int count = 70);
void drawStarsIn(Gfx &g, int x, int y, int w, int h); // static, folded into a window
struct PlanetP {
    float cx, cy, R, sunX;
};
void drawPlanet(Gfx &g, float t, const PlanetP &p);
void drawEmblem(Gfx &g, const char *seed, int x, int y, int cell, uint16_t on, int32_t off);

// ── home ────────────────────────────────────────────────────────────────────
enum TileId : uint8_t { TL_SD, TL_OTA, TL_WUI, TL_USB, TL_PMAN, TL_CFG, TL_APP, TL_CHG, TL_OFF };
struct Tile { // all-zero is a valid empty tile, so a static table of them sits in .bss
    uint8_t id, group, fam;
    bool bootsNext;
    char title[22], desc[40], meta[44], ini[3], label[18];
};
class HomeScene : public Scene {
public:
    Tile *tiles = nullptr;
    int n = 0, sel = 0, passBy = 0;
    float pos = 0, lockT = 9, denyT = 9;
    bool shortcuts = false, devMode = false;
    const char *version = "";
    float metBase = 0; // mission clock = uptime + this
    void (*onDevMode)() = nullptr;
    // res: tile index, or 1000 + a letter (SD shortcut)
    void setSel(int i);
    bool disabled(int i) const;
    void key(uint8_t k, char ch) override;
    void update(float dt, bool top) override;
    void draw(Gfx &g) override;

private:
    void select(int i);
    void drawIcon(Gfx &g, int i, int x, int y, int mode);
    void meta(int i, char *out, uint16_t &c);
};

class ChargeScene : public Scene {
public:
    float volts = 0; // 0 = unknown
    int fps = 6;
    void key(uint8_t k, char ch) override;
    void draw(Gfx &g) override;
};

class PowerOffScene : public Scene {
public:
    uint16_t *snap = nullptr; // full frame of the screen below (optional): the CRT squash
    bool overlay() const override { return true; }
    void draw(Gfx &g) override;
    void update(float dt, bool top) override;
};

// ── boot ────────────────────────────────────────────────────────────────────
struct BootApp {
    char name[22], label[18];
    uint32_t size;
};
struct Particle {
    float x, y, vx, vy, life, max, s;
    uint8_t kind;
};
class BootScene : public Scene {
public:
    BootApp *apps = nullptr;
    int napps = 0, app = -1; // app = the one on the pad
    bool bootToApp = true;
    float total = 5;
    const char *version = "";
    // res: -1 open Launcher, 0..n-1 boot that app, 100 pick (any key with several
    // apps: the bridge shows the picker), 200 countdown ran out: boot the pad app
    enum State : uint8_t { S_COUNT, S_HOLD, S_PICK, S_LAUNCH, S_DONE };
    uint8_t state = S_COUNT;
    float st = 0, lift = 0, vy = 0;
    bool timedOut = false;
    Particle *parts = nullptr;
    int nparts = 0, maxParts = 0;
    Mulberry rng{42};
    void start(Particle *pool, int poolSize); // after apps/app/bootToApp are set
    void launch(int i);
    void hold();
    void resumeCount() { state = S_COUNT; }
    void key(uint8_t k, char ch) override;
    void update(float dt, bool top) override;
    void draw(Gfx &g) override;

private:
    void drawScene(Gfx &g, float t);
    void drawParts(Gfx &g, uint8_t kind);
    void emit(uint8_t kind, float x, float y, float vx, float vy, float max, float s);
    float remaining() const;
};

// ── SD browser ──────────────────────────────────────────────────────────────
constexpr uint32_t SIZE_UNKNOWN = 0xFFFFFFFF;
enum SdFlag : uint8_t { SDF_DIR = 1, SDF_BIN = 2 };
struct SdEntry {
    uint16_t name; // offset into SDScene::names
    uint8_t flags;
    char key; // bound shortcut or 0
    uint32_t size;
};
// File system access, so the browser runs on the SD card and in the harness alike
class SdFs {
public:
    // appends the folder's entries (unsorted); returns false if it cannot be read
    virtual bool list(const char *path, class SDScene &sc) = 0;
    virtual uint32_t size(const char *path) = 0; // SIZE_UNKNOWN if not found
    virtual char boundKey(const char *path) { return 0; }
};
enum SdRes : int16_t { SD_EXIT = -1, SD_FILE = 1, SD_FOLDER = 2, SD_PICK = 3 };
class SDScene : public Scene, public ListSrc {
public:
    SdFs *fs = nullptr;
    char path[160] = "/";
    char clip[48] = ""; // name of the copied file, shown in the hint bar
    bool onlyBins = true, showDot = false, picker = false, readFailed = false;
    SdEntry *ents = nullptr;
    int nents = 0, cap = 0;
    char *names = nullptr;
    int namesLen = 0, namesCap = 0;
    ListView list;
    int enterAnim = 0;
    float at = 0;
    // res: SD_EXIT (back at the root), SD_FILE (actions for the selected file),
    // SD_FOLDER (hold: folder actions; on ".." for this folder), SD_PICK (picker mode)
    ~SDScene();
    void open(const char *p, const char *selName = nullptr);
    void reload(); // same folder, keep the selection by name
    bool add(const char *name, bool dir, uint32_t size);
    const char *name(int i) const { return names + ents[i].name; }
    int selEntry() const { return list.sel - 1; } // -1 = ".."
    void join(char *out, int outLen, const char *nm) const;
    void up();
    // ListSrc
    int count() override { return nents + 1; }
    uint8_t kind(int i) override { return i == 0 ? RK_OP : RK_ITEM; }
    void row(int i, Row &r) override;
    // Scene
    bool holdMode() const override { return true; }
    void key(uint8_t k, char ch) override;
    void update(float dt, bool top) override;
    void draw(Gfx &g) override;
};

// ── install / download: "launch sequence" ──────────────────────────────────
class JobScene : public Scene {
public:
    struct Stage {
        char label[20];
        uint32_t size, ofs; // bytes, flash address (0 = none)
        bool app;
    };
    static constexpr int MAXST = 10;
    bool download = false, finished = false, failed = false;
    char name[64] = "", appName[24] = "", doneLine[48] = "", doneSub[40] = "";
    float doneWait = 0; // > 0: the sub line counts down "IN n · ANY KEY NOW"
    Stage st[MAXST];
    int nst = 0, si = 0;
    float sp = 0, speed = 0, doneT = 0, shownP = 0;
    uint32_t bytes = 0; // into the current stage
    Particle parts[64];
    int np = 0;
    Mulberry rng{5};
    void reset(bool dl, const char *nm);
    int addStage(const char *label, uint32_t size, uint32_t ofs = 0, bool app = false);
    int findStage(const char *label) const;
    void goStage(int i); // earlier stages are done
    void progress(uint32_t done, uint32_t total);
    void finish(const char *line, const char *sub);
    float overall() const;
    uint32_t total() const;
    virtual uint32_t flashAddr() const;
    bool anyKey = false;
    void key(uint8_t k, char ch) override;
    void update(float dt, bool top) override;
    void draw(Gfx &g) override;

protected:
    void curve(float u, float &x, float &y) const;
    void rocketPos(float &x, float &y, float &dx, float &dy) const;
};

// ── Wi-Fi: radar scan, network list, handshake ─────────────────────────────
struct Net {
    char ssid[33];
    int8_t rssi;
    uint8_t authmode;
    bool auth, saved;
};
void drawDevice(Gfx &g, int x, int y, uint16_t c);
void drawRouter(Gfx &g, int x, int y, uint16_t c, float t);
void wifiBars(Gfx &g, int x, int y, int rssi, uint16_t c, uint16_t cOff);
enum WifiRes : int16_t { WF_BACK = -1, WF_HIDDEN = -3, WF_HOME = -4 };
class WifiScene : public Scene, public ListSrc {
public:
    static constexpr int MAXN = 24;
    Net nets[MAXN];
    int n = 0, autoNet = -1;
    enum State : uint8_t { S_SCAN, S_AUTO, S_LIST };
    uint8_t state = S_SCAN;
    bool scanDone = false;
    float listAt = 2.75f, at = 0, sweep0 = 0, t0 = 0;
    uint32_t found = 0; // bit per network: the sweep has passed it
    ListView list;
    // res: network index (connect), WF_HIDDEN, WF_HOME, WF_BACK
    void results(float delay); // after nets/n/autoNet are set: reveal, then the list
    void showList(int sel = 0);
    int count() override { return n + 2; }
    uint8_t kind(int i) override { return i >= n ? RK_OP : RK_ITEM; }
    void row(int i, Row &r) override;
    int widget(int i, Gfx &g, int rx, int y, bool sel) override;
    void key(uint8_t k, char ch) override;
    void update(float dt, bool top) override;
    void draw(Gfx &g) override;
};
class ConnectScene : public Scene {
public:
    char ssid[33] = "", sub[48] = "";
    enum State : uint8_t { S_TRY, S_LINKED, S_BROKEN };
    uint8_t state = S_TRY;
    bool failPassword = true;
    int tries = 1;
    float lt = 0;
    void key(uint8_t k, char ch) override;
    void update(float dt, bool top) override;
    void draw(Gfx &g) override;
};

// ── generic full-screen list (catalog, favorites, shortcuts, pickers) ─────
enum ListRes : int16_t { LS_BACK = -1, LS_PREV = -10, LS_NEXT = -11 };
class ListScene : public Scene {
public:
    Hdr hdr;
    ListView list;
    const char *hk = "\xE2\x8F\x8E|OPEN|`|BACK";
    char right[48] = "";
    bool pages = false; // left/right flip pages (LS_PREV / LS_NEXT)
    // res: the row picked (Enter or hold), LS_BACK, LS_PREV, LS_NEXT
    void setup(ListSrc *src, int sel = 0, int expand = 0);
    void key(uint8_t k, char ch) override;
    void update(float dt, bool top) override { list.update(dt); }
    void draw(Gfx &g) override;
};

// ── online ──────────────────────────────────────────────────────────────────
struct HubTile {
    uint8_t id, icon; // id: what the bridge does with it
    const char *label, *sub, *foot;
    char big[8];
    int badge; // > 0: "N NEW"
};
class OtaMenuScene : public Scene {
public:
    HubTile tiles[3];
    int n = 0, sel = 0;
    float ct = 9;
    const char *crumb = "";
    void key(uint8_t k, char ch) override;
    void update(float dt, bool) override { ct += dt; }
    void draw(Gfx &g) override;
};
class FetchScene : public Scene {
public:
    const char *label = "GETTING CATALOG", *path = "GET /FIRMWARES?CATEGORY=CARDPUTER";
    char tries[16] = "TRY 1/5";
    void draw(Gfx &g) override;
};
struct FwVersion {
    char v[24], date[12];
};
class FirmwareScene : public Scene {
public:
    char name[48] = "", author[32] = "", fid[40] = "", file[48] = "", onDevice[24] = "";
    bool star = false;
    FwVersion *vers = nullptr;
    int nv = 0, vi = 0, dir = 0;
    float vt = 9;
    // res: 1 = options for version vi, -1 = back
    void key(uint8_t k, char ch) override;
    void update(float dt, bool) override { vt += dt; }
    void draw(Gfx &g) override;
};

// ── web UI, USB drive ───────────────────────────────────────────────────────
// QR code, version 2-M (25x25, up to 26 bytes): one bit per module, row-major
struct QR {
    uint8_t m[79];
    bool get(int x, int y) const {
        int i = y * 25 + x;
        return m[i >> 3] >> (i & 7) & 1;
    }
};
bool qrEncode(const char *text, QR &out);
class WebModeScene : public Scene {
public:
    int sel = 0;
    float ct = 9;
    char ssid[33] = ""; // connected network, empty if none
    const char *apUrl = "HTTP://172.0.0.1";
    void key(uint8_t k, char ch) override;
    void update(float dt, bool) override { ct += dt; }
    void draw(Gfx &g) override;
};
class WebRunScene : public Scene {
public:
    bool ap = true;
    char ip[16] = "", ssid[33] = "", user[33] = "", pass[33] = "";
    QR qr;
    bool qrOk = false;
    // live request log, filled from the web server task
    static volatile uint32_t reqCount;
    static char reqLog[4][40];
    static float reqAt[4];
    static void logRequest(const char *line);
    void begin();
    void key(uint8_t k, char ch) override;
    void draw(Gfx &g) override;
};
class UsbScene : public Scene {
public:
    enum State : uint8_t { S_WAIT, S_MOUNTED, S_EJECT };
    uint8_t state = S_WAIT;
    float et = 0, lastAct = -9;
    bool writing = false;
    uint32_t rd = 0, wr = 0, lastRd = 0, lastWr = 0;
    char card[48] = "";
    void key(uint8_t k, char ch) override;
    void update(float dt, bool top) override;
    void draw(Gfx &g) override;
};

// ── partitions ──────────────────────────────────────────────────────────────
enum PartKind : uint8_t { PK_FREE, PK_LAUNCHER, PK_SYS, PK_APP, PK_OWNED, PK_DATA };
struct PartRow {
    char label[17], right[28];
    uint32_t ofs, size;
    uint8_t kind, fam;
    bool prot;
    int16_t opt; // index into the caller's option list
};
void drawPartSwatch(Gfx &g, const PartRow &p, int x, int y, int w, int h, bool sel);
class PmanScene : public Scene, public ListSrc {
public:
    static constexpr int MAXR = 24;
    PartRow rows[MAXR]; // partitions and free ranges, by offset
    int nrows = 0;
    struct Op {
        const char *label;
        uint8_t icon, kind;
        int16_t opt;
    } ops[8]; // table actions after the TABLE section
    int nops = 0;
    uint32_t flash = 0x800000;
    bool dirty = false;
    ListView list;
    void build(int sel = -1); // after rows/ops are set
    int count() override { return nrows + 1 + nops; }
    uint8_t kind(int i) override;
    void row(int i, Row &r) override;
    bool icon(int i, Gfx &g, int x, int y, bool sel) override;
    int widget(int i, Gfx &g, int rx, int y, bool sel) override;
    int optAt(int i) const; // option index for row i, -1 for the section
    // res: option index; -1 back
    void key(uint8_t k, char ch) override;
    void update(float dt, bool) override { list.update(dt); }
    void draw(Gfx &g) override;
};
// key/value panel over the screen (partition details, about)
class InfoScene : public Scene {
public:
    const char *title = "", *status = "";
    int32_t statusC = -1;
    const char *keys[8], *vals[8];
    int n = 0;
    bool overlay() const override { return true; }
    void key(uint8_t k, char ch) override {
        if (t > 0.15f) res = 0;
    }
    void draw(Gfx &g) override;
};
class RangeScene : public Scene {
public:
    char label[20] = "";
    uint32_t minO = 0, maxO = 0, start = 0, end = 0, step = 0x10000, minSize = 0x10000;
    uint8_t fam = 0;
    int h = 1; // handle: 0 start, 1 end
    // res: 1 keep, 0 cancel
    void key(uint8_t k, char ch) override;
    void draw(Gfx &g) override;
};

// ── settings ────────────────────────────────────────────────────────────────
enum SetKind : uint8_t { SK_SECTION, SK_TOGGLE, SK_VALUE, SK_BAR, SK_THEME, SK_ACTION, SK_DANGER, SK_OP };
struct SetItem {
    const char *label;
    uint8_t icon, kind;
    bool on, adj;    // toggle state; value adjusts with left/right
    float bar, anim; // SK_BAR fraction; toggle animation
    char val[20];
};
enum SetAct : uint8_t { SA_OPEN, SA_LEFT, SA_RIGHT };
class SettingsScene : public Scene, public ListSrc {
public:
    static constexpr int MAXI = 32;
    SetItem items[MAXI];
    int n = 0;
    const char *version = "";
    ListView list;
    void begin(int sel);
    int count() override { return n; }
    uint8_t kind(int i) override;
    void row(int i, Row &r) override;
    int widget(int i, Gfx &g, int rx, int y, bool sel) override;
    // res: item * 4 + SetAct
    void key(uint8_t k, char ch) override;
    void update(float dt, bool top) override;
    void draw(Gfx &g) override;
};
void drawSwatches(Gfx &g, int x, int y, uint8_t theme, bool sel);
class BrightnessScene : public Scene {
public:
    int val = 80;
    void (*preview)(int) = nullptr;
    bool overlay() const override { return true; }
    // res: 1 keep, 0 undo
    void key(uint8_t k, char ch) override;
    void draw(Gfx &g) override;
};
class ColorScene : public Scene {
public:
    int sel = 0;
    float ct = 9;
    bool overlay() const override { return true; }
    // res: 1 keep (theme already applied), 0 undo
    void key(uint8_t k, char ch) override;
    void update(float dt, bool) override { ct += dt; }
    void draw(Gfx &g) override;
};
class BindScene : public Scene {
public:
    const char *name = "";
    char bound[40] = ""; // keys already in use
    char got = 0;
    float mt = 9, bt = 0;
    bool overlay() const override { return true; }
    bool textMode() const override { return true; }
    // res: the key, or 0 on cancel
    void key(uint8_t k, char ch) override;
    void update(float dt, bool top) override;
    void draw(Gfx &g) override;
};
class AppScene : public Scene {
public:
    char name[24] = "", label[18] = "", ini[3] = "";
    uint8_t fam = 0;
    bool bootsNext = false;
    const char *keys[6], *vals[6];
    int nrows = 0;
    MenuSrc menu;
    ListView list;
    void begin();
    // res: action index, -1 back
    void key(uint8_t k, char ch) override;
    void update(float dt, bool) override { list.update(dt); }
    void draw(Gfx &g) override;
};

} // namespace orb
#endif
