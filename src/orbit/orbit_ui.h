// Orbit UI — UI kit (header, hints, widgets, list view), the scene stack that
// runs every screen, and the shared overlays: action sheet, dialog, text entry,
// status steps, ring progress and toasts.
#pragma once
#if defined(ORBIT_UI) || defined(ORBIT_NATIVE)
#include "orbit_gfx.h"

namespace orb {

constexpr int HDR_H = 11, FOOT_Y = 125;

// ── platform hooks: the bridge implements them on the device, the test harness on a PC ──
struct Status {
    uint8_t battery = 100;
    bool hasBattery = true, charging = false, wifi = false, sd = false;
};
extern Status ST;

enum Key : uint8_t { K_NONE, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_OK, K_BACK, K_HOLD, K_CHAR, K_DEL };
struct Input {
    uint8_t k = K_NONE;
    char ch = 0;
};
namespace plat {
uint32_t ms();
// One input event. text: navigation keys arrive as the characters they print;
// hold: a held Enter is reported as K_HOLD (released early: K_OK).
Input poll(bool text, bool hold);
void push(int y0, int h, const uint16_t *px); // rows y0..y0+h of the frame
void idle(uint32_t untilMs);                  // wait for the next frame, yielding
bool screenOff();                             // backlight off: skip drawing
void status(Status &s);
} // namespace plat

// ── kit ─────────────────────────────────────────────────────────────────────
int statusCluster(Gfx &g, int xr, int y); // returns the left edge
struct Hdr {
    uint8_t icon = G_ROCKET;
    const char *title = "";
    const char *crumb = nullptr;
    int crumbMax = 120;
    const char *right = nullptr;
    int32_t rightC = -1;
    bool status = true;
    int32_t bg = -1;
};
void header(Gfx &g, const Hdr &h);
int keycap(Gfx &g, const char *k, int x, int y, bool hot = false);
// items: "KEY|LABEL|KEY|LABEL..."
void hints(
    Gfx &g, const char *items, const char *right = nullptr, int32_t rightC = -1, int y = FOOT_Y + 2,
    bool bg = true
);
void hintsOver(
    Gfx &g, const char *items, int y, int outline, int x = 4
); // over art, no bar; outline: a Tok or -1
void toggle(Gfx &g, int x, int y, bool on, bool sel, float anim);
void miniBar(Gfx &g, int x, int y, int w, float frac, bool sel);
void segbar(
    Gfx &g, int x, int y, int w, int h, float frac, int32_t color = -1, int32_t lead = -1, int32_t empty = -1,
    int seg = 3
);
void spinner(Gfx &g, int x, int y, uint16_t c);
void corners(Gfx &g, int x, int y, int w, int h, uint16_t c, int len = 3);
void backdrop(Gfx &g, int strength = 1);
void fmtSize(char *out, uint32_t b);
void hex32(char *out, uint32_t v);

// ── list view ───────────────────────────────────────────────────────────────
enum RowKind : uint8_t { RK_ITEM, RK_SECTION, RK_DANGER, RK_OP };
struct Row {
    const char *label = "";
    char right[28] = "";
    const char *sub = nullptr;
    uint8_t icon = 255;
    uint8_t kind = RK_ITEM;
    bool disabled = false;
    int32_t iconC = -1, rightC = -1, color = -1;
};
class ListSrc {
public:
    virtual int count() = 0;
    virtual uint8_t kind(int i) { return RK_ITEM; }
    virtual void row(int i, Row &r) = 0;
    // optional inline widget at the right edge: returns the new right edge
    virtual int widget(int i, Gfx &g, int rx, int y, bool sel) { return rx; }
    // optional custom icon: return true when drawn
    virtual bool icon(int i, Gfx &g, int x, int y, bool sel) { return false; }
};
class ListView {
public:
    ListSrc *src = nullptr;
    int x = 0, y = HDR_H + 1, w = W, h = FOOT_Y - HDR_H - 2;
    int rowH = 11, expand = 0, iconW = 10, pad = 4;
    int sel = 0;
    float scroll = 0, scrollTarget = -1, selT = 0;
    bool wrap = true;

    void init(ListSrc *s, int selIdx = 0);
    void refresh(bool keepSel = true); // after the source changed
    bool selectable(int i);
    int firstSelectable(int i);
    int rowHeight(int i);
    int rowTop(int i);
    int contentH() { return rowTop(src->count()); }
    float targetScroll();
    void snap(); // jump the scroll to the selection
    bool move(int d);
    bool key(uint8_t k);
    void update(float dt);
    void draw(Gfx &g);

private:
    void drawRow(Gfx &g, int i, int ry, int rw, int rh, bool sel);
};

// Simple static list for sheets and pickers
struct MenuItem {
    const char *label;
    uint8_t icon;
    uint8_t kind;
    const char *right;
};
class MenuSrc : public ListSrc {
public:
    const MenuItem *items = nullptr;
    int n = 0;
    int count() override { return n; }
    uint8_t kind(int i) override { return items[i].kind; }
    void row(int i, Row &r) override;
};

// ── scenes ──────────────────────────────────────────────────────────────────
constexpr int16_t RUNNING = -32768;
class Scene {
public:
    float t = 0;
    int16_t res = RUNNING; // set to end run()
    virtual bool overlay() const { return false; }
    virtual bool textMode() const { return false; }
    virtual bool holdMode() const { return false; }
    virtual void update(float dt, bool top) {}
    virtual void draw(Gfx &g) = 0;
    virtual void key(uint8_t k, char ch) {}
};

enum Trans : uint8_t { TR_AUTO, TR_NONE, TR_WIPE, TR_FADE };
extern float now;   // seconds since boot (drives ambient animation)
extern float shake; // screen shake time left
void push(Scene &s, uint8_t tr = TR_AUTO);
void pop(uint8_t tr = TR_AUTO);       // pop the top scene; full-screen pops wipe back
void remove(Scene &s);                // unlink without a transition
void removeAfterTransition(Scene &s); // unlink once the running transition is over
bool onStack(const Scene &s);
Scene *top();
bool transitioning();
int depth();
Scene *at(int i);
void frame(bool input = true);             // one frame: input, update, draw, present, pace
int run(Scene &s);                         // push if needed; frames until s.res is set; returns it (s stays)
int modal(Scene &s, uint8_t tr = TR_AUTO); // push + run + pop
// draw now if a frame is due (for long jobs that report progress); no input
void tick(uint32_t minGapMs = 66);
enum ToastKind : uint8_t { TK_INFO, TK_OK, TK_ERR };
void toast(const char *msg, uint8_t kind = TK_INFO, float dur = 1.8f);
void begin(uint16_t *buf, int bandRows); // frame buffer: W x bandRows pixels
void redraw();                           // draw the current state now
// renders the scenes under the top one into a full W x H frame (for effects)
void snapshotBelow(uint16_t *dst);
#if defined(ORBIT_NATIVE)
extern float testDt; // fixed frame step for the desktop harness (0 = measured)
void testSettle();   // drop transitions and shake, like the mockup's jump()
void testReset();    // empty the stack, clock to 0
#endif

// ── shared overlays ─────────────────────────────────────────────────────────
class SheetScene : public Scene {
public:
    ListView list;
    const char *title = "ACTIONS", *sub = nullptr;
    int pw = 118;
    bool closing = false;
    float ct = 0;
    int picked = -1;
    void setup(ListSrc *src, const char *title_, const char *sub_ = nullptr, int sel = 0);
    bool overlay() const override { return true; }
    void key(uint8_t k, char ch) override;
    void update(float dt, bool top) override;
    void draw(Gfx &g) override;
};
// returns the picked index or -1
int sheet(ListSrc *src, const char *title, const char *sub = nullptr, int sel = 0);
int sheet(const MenuItem *items, int n, const char *title, const char *sub = nullptr, int sel = 0);

enum DlgKind : uint8_t { DK_INFO, DK_ERROR, DK_OK, DK_CONFIRM };
struct DialogOpts {
    uint8_t kind = DK_INFO;
    const char *title = nullptr, *msg = "", *detail = nullptr, *code = nullptr;
    const char *const *buttons = nullptr;  // left to right
    uint8_t nbtn = 0, sel = 0, danger = 0; // danger: bit mask of red buttons
    float autoClose = 0;
    int w = 200;
    bool dim2 = false;
};
class DialogScene : public Scene {
public:
    DialogOpts o;
    int sel = 0;
    bool closing = false;
    float ct = 0;
    int pick = -1;
    bool overlay() const override { return true; }
    void key(uint8_t k, char ch) override;
    void update(float dt, bool top) override;
    void draw(Gfx &g) override;
};
// returns the button index, -1 on cancel / auto close / any key without buttons
int dialog(const DialogOpts &o);
bool confirm(
    const char *title, const char *msg, const char *yes, bool danger = false, const char *detail = nullptr
);
void errorBox(const char *msg, const char *title = nullptr, const char *detail = nullptr);

class KeyboardScene : public Scene {
public:
    const char *title = "", *sub = nullptr;
    char *val = nullptr;
    int max = 76, len = 0;
    bool mask = false, done = false;
    float lastKeyT = 9, revealT = 9, doneT = 0;
    char lastKey = 0;
    int ok = -1;
    bool overlay() const override { return true; }
    bool textMode() const override { return true; }
    void key(uint8_t k, char ch) override;
    void update(float dt, bool top) override;
    void draw(Gfx &g) override;
};
// edits buf in place (size >= max+1); returns false on cancel
bool askText(const char *title, const char *sub, char *buf, int max, bool mask = false);

// Steps panel: a job told in stages (replaces the old red stripes)
class StepsScene : public Scene {
public:
    static constexpr int MAXS = 6;
    char title[24] = "WORKING";
    char steps[MAXS][32];
    int n = 0, first = 0; // shows the last MAXS
    bool failed = false, finished = false;
    float frac = -1; // current step progress, -1 = none
    bool overlay() const override { return true; }
    void add(const char *s);
    void draw(Gfx &g) override;
};

// Ring progress (copy, backup)
class ArcScene : public Scene {
public:
    const char *title = "", *sub = "";
    float p = 0;
    bool overlay() const override { return true; }
    void draw(Gfx &g) override;
};

// Keyboard map shared by text entry and key binding
extern const char *const KB_ROWS[4];
const char *kbLabel(char k);
void drawKeyMap(Gfx &g, int kx0, int ky, char lit, bool bindMode, const char *bound);

} // namespace orb
#endif
