// Orbit UI — device side: input, display output and theme, plus the Launcher's
// generic UI calls (loopOptions, displayRedStripe, displayError/Msg,
// progressHandler, keyboard) and the boot screen, home orbit, charge mode,
// power off and LauncherHub catalog.
#if defined(ORBIT_UI)
#include "orbit_bridge.h"
#include "../app_registry.h"
#include "../display.h"
#include "../idf/idf_wifi.h"
#include "../idf/launcher_platform.h"
#include "../massStorage.h"
#include "../mykeyboard.h"
#include "../nvs_helpers.h"
#include "../onlineLauncher.h"
#include "../partition_table_model.h"
#include "../partitioner.h"
#include "../powerSave.h"
#include "../sd_functions.h"
#include "../settings.h"
#include "../utils.h"
#include "../webInterface.h"
#include <SPI.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>
#include <globals.h>
#include <nvs.h>

using namespace orb;

extern bool fn_key_pressed; // board interface: Fn held (TCA8418 keyboard)
float launcherBatteryVolts();

// ── panel ───────────────────────────────────────────────────────────────────
// An SPI ST7789 (the Cardputer) is driven straight through SPIClass: the UI only
// ever sends whole-width bands, and leaving tft->begin() uncalled keeps the
// Arduino_GFX panel and bus classes out of the binary -- their vtables pull in
// ~11 KB of drawing code and 7 KB of YCbCr tables. tft stays as a shell whose
// calls do nothing. Other panels go through tft as before.
#if TFT_DATABUS_N == 0 && TFT_DISPLAY_DRIVER_N == 1 && !defined(TFT_DISPLAY_DRIVER_N_ALT1) &&                \
    !defined(USE_CANVAS)
#define ORB_OWN_PANEL
static const SPISettings panelSpi(SPI_DEFAULT_FREQ, MSBFIRST, SPI_MODE3); // as Arduino_HWSPI + Arduino_ST7789
static bool panelUp = false;
static int16_t panelX0 = 0, panelY0 = 0;
static void panelOpen() {
    SPI.beginTransaction(panelSpi);
    digitalWrite(displayConfig.cs, LOW);
}
static void panelClose() {
    digitalWrite(displayConfig.cs, HIGH);
    SPI.endTransaction();
}
static void panelCmd(uint8_t c) {
    digitalWrite(displayConfig.dc, LOW);
    SPI.write(c);
    digitalWrite(displayConfig.dc, HIGH);
}
static void panelWin(uint8_t c, uint16_t a, uint16_t b) {
    const uint8_t d[4] = {(uint8_t)(a >> 8), (uint8_t)a, (uint8_t)(b >> 8), (uint8_t)b};
    panelCmd(c);
    SPI.writeBytes(d, 4);
}
void orbSetRotation(uint8_t r) {
    // Arduino_ST7789::setRotation: MADCTL, and Arduino_TFT's window offsets
    static const uint8_t MAD[8] = {0x00, 0x60, 0xC0, 0xA0, 0x40, 0xE0, 0x80, 0x20};
    const DisplayConfig &c = displayConfig;
    const int16_t xs[8] = {
        c.colOfs1, c.rowOfs1, c.colOfs2, c.rowOfs2, c.colOfs2, c.rowOfs2, c.colOfs1, c.rowOfs1
    };
    const int16_t ys[8] = {
        c.rowOfs1, c.colOfs2, c.rowOfs2, c.colOfs1, c.rowOfs1, c.colOfs2, c.rowOfs2, c.colOfs1
    };
    r &= 7;
    panelX0 = xs[r];
    panelY0 = ys[r];
    if (!panelUp) return;
    panelOpen();
    panelCmd(ST7789_MADCTL);
    SPI.write(MAD[r]);
    panelClose();
}
void orbPanelBegin() { // Arduino_HWSPI::begin, Arduino_ST7789::tftInit
    const DisplayConfig &c = displayConfig;
    pinMode(c.dc, OUTPUT);
    digitalWrite(c.dc, HIGH);
    pinMode(c.cs, OUTPUT);
    digitalWrite(c.cs, HIGH);
    SPI.begin(c.sclk, c.miso, c.mosi);
    if (c.rst >= 0) {
        pinMode(c.rst, OUTPUT);
        digitalWrite(c.rst, HIGH);
        delay(100);
        digitalWrite(c.rst, LOW);
        delay(ST7789_RST_DELAY);
        digitalWrite(c.rst, HIGH);
        delay(ST7789_RST_DELAY);
    }
    panelOpen();
    panelCmd(ST7789_SWRESET);
    panelClose();
    delay(ST7789_RST_DELAY);
    // the init table, through the subset of Arduino_DataBus::batchOperation it uses
    const uint8_t *op = st7789_type1_init_operations, *end = op + sizeof(st7789_type1_init_operations);
    while (op < end) {
        uint8_t o = *op++, n = 0;
        if (o == BEGIN_WRITE) panelOpen();
        else if (o == END_WRITE) panelClose();
        else if (o == DELAY) delay(*op++);
        else if (o == WRITE_COMMAND_8 || o == WRITE_C8_D8 || o == WRITE_C8_D16 || o == WRITE_C8_BYTES) {
            panelCmd(*op++);
            n = o == WRITE_C8_D8 ? 1 : o == WRITE_C8_D16 ? 2 : o == WRITE_C8_BYTES ? *op++ : 0;
        } else break; // not in this table
        if (n) SPI.writeBytes(op, n);
        op += n;
    }
    bool inv = c.ips;
#ifdef TFT_INVERSION_ON
    inv = !inv;
#endif
    panelOpen();
    panelCmd(inv ? ST7789_INVON : ST7789_INVOFF);
    panelClose();
    panelUp = true;
    orbSetRotation(rotation);
}
#else
void orbPanelBegin() { tft->begin(); }
void orbSetRotation(uint8_t r) { tft->setRotation(r); }
#endif

// ── platform ────────────────────────────────────────────────────────────────
namespace orb {
namespace plat {
uint32_t ms() { return launcherMillis(); }

// Enter can be held for a menu (SD browser): track it until released or 300 ms.
static uint8_t holdState = 0;
static uint32_t holdT0 = 0;
Input poll(bool text, bool hold) {
    Input in;
    uint32_t t = launcherMillis();
    launcherInputLock();
    if (holdState) {
        AnyKeyPress = false; // keeps the input task on its fast cadence
        bool held = SelPress;
        if (holdState == 1) {
            if (!held) {
                holdState = 0;
                in.k = K_OK;
            } else if (t - holdT0 >= 300) {
                holdState = 2;
                in.k = K_HOLD;
            }
        } else if (!held || t - holdT0 > 4000) holdState = 0;
        if (!holdState) {
            LongPress = false;
            SelPress = false;
            KeyStroke.Clear();
        }
        UpPress = DownPress = PrevPress = NextPress = EscPress = false;
        launcherInputUnlock();
        return in;
    }
    bool up = UpPress, dn = DownPress, pv = PrevPress, nx = NextPress, sel = SelPress, esc = EscPress;
    bool pressed = KeyStroke.pressed, enter = false, del = false, fn = false, exitk = false;
    char ch = 0;
    if (pressed) {
        if (!KeyStroke.word.empty()) ch = KeyStroke.word[0];
        enter = KeyStroke.enter;
        del = KeyStroke.del;
        fn = KeyStroke.fn || fn_key_pressed;
        exitk = KeyStroke.exit_key;
        KeyStroke.Clear();
    }
    if (hold && sel && !text) { // the input task reports Enter every cycle while LongPress is set
        holdState = 1;
        holdT0 = t;
        LongPress = true;
        UpPress = DownPress = PrevPress = NextPress = EscPress = AnyKeyPress = false;
        launcherInputUnlock();
        return in;
    }
    UpPress = DownPress = PrevPress = NextPress = SelPress = EscPress = AnyKeyPress = false;
    launcherInputUnlock();
    if (text) {
        if (pressed) {
            if (fn && (exitk || ch == '`' || del)) in.k = K_BACK; // Fn+` is Esc on the Cardputer
            else if (enter) in.k = K_OK;
            else if (del) in.k = K_DEL;
            else if (ch >= ' ' && ch < 127) {
                in.k = K_CHAR;
                in.ch = ch;
            }
        } else if (sel) in.k = K_OK; // G0 button
        return in;
    }
    if (up) in.k = K_UP;
    else if (dn) in.k = K_DOWN;
    else if (pv) in.k = K_LEFT;
    else if (nx) in.k = K_RIGHT;
    else if (sel) in.k = K_OK;
    else if (esc) in.k = K_BACK;
    else if (pressed && !enter && ch > ' ' && ch < 127 && !strchr(",;/.`", ch)) {
        in.k = K_CHAR;
        in.ch = ch;
    }
    return in;
}
void push(int y0, int h, const uint16_t *px) {
#if defined(ORB_OWN_PANEL)
    if (!panelUp) return;
    panelOpen();
    panelWin(ST7789_CASET, panelX0, panelX0 + W - 1);
    panelWin(ST7789_RASET, panelY0 + y0, panelY0 + y0 + h - 1);
    panelCmd(ST7789_RAMWR);
    SPI.writePixels(px, (uint32_t)W * h * 2);
    panelClose();
#else
    Arduino_GFX *gfx = tft->native();
    if (gfx) gfx->draw16bitRGBBitmap(0, y0, const_cast<uint16_t *>(px), W, h);
#endif
}
void idle(uint32_t until) {
    int32_t d = (int32_t)(until - launcherMillis());
    vTaskDelay(pdMS_TO_TICKS(d > 1 ? d : 1));
}
bool screenOff() { return isScreenOff; }
void status(Status &s) {
    static uint32_t last = 0;
    static bool first = true;
    uint32_t t = launcherMillis();
    if (first || t - last > 1500) {
        first = false;
        last = t;
        int b = getBattery();
        s.hasBattery = b > 0;
        s.battery = b < 0 ? 0 : b > 100 ? 100 : b;
        s.charging = isCharging();
        s.wifi = launcherWifiIsConnected();
    }
    s.sd = sdcardMounted;
}
} // namespace plat
} // namespace orb

// ── calls from other tasks (web uploads run in the HTTP server task) ───────
// They only leave state here; the UI task draws it (orbSyncRemote).
static TaskHandle_t uiTask = nullptr;
static bool foreign() { return uiTask && xTaskGetCurrentTaskHandle() != uiTask; }
static struct {
    volatile bool progress, failed;
    volatile uint32_t done, total, stripeSeq, msgSeq;
    char stripe[48], msg[64];
} remote;

// ── init, theme ─────────────────────────────────────────────────────────────
static const int BAND = 45;          // 3 bands per frame
static uint16_t frameBand[W * BAND]; // 21.6 KB
static bool ready = false;
static char versionUp[16];

uint8_t orbThemeIndex() {
    uint8_t v = 0xFF;
    lnvs::Handle h("launcher", false);
    if (h) nvs_get_u8(h.raw(), "orbitTheme", &v);
    if (v < THEME_COUNT) return v;
    switch (FGCOLOR) { // first run: carry the old colour choice over
        case 0x94BF: return 3;
        case 0xFFE0: return 2;
        case 0xE01F: return 4;
        case 0xFFFF: return 5;
        case 0x0000: return 6;
    }
    return 0;
}
void orbApplyTheme(uint8_t idx) {
    setTheme(idx);
    FGCOLOR = C(T_acc);
    ALCOLOR = C(T_sec);
    BGCOLOR = C(T_bg);
}
void orbSaveTheme(uint8_t idx) {
    lnvs::Handle h("launcher", true);
    if (!h) return;
    nvs_set_u8(h.raw(), "orbitTheme", idx);
    h.commit();
}
void orbInit() {
    if (ready) return;
    ready = true;
    uiTask = xTaskGetCurrentTaskHandle();
    int i = 0;
    for (const char *p = LAUNCHER; *p && i < 15; p++) versionUp[i++] = *p >= 'a' && *p <= 'z' ? *p - 32 : *p;
    versionUp[i] = 0;
    uint8_t th = orbThemeIndex();
    orbApplyTheme(th);
    orbSaveTheme(th);
    begin(frameBand, BAND);
}

// ── transient UI: stripes, jobs, ring progress ─────────────────────────────
static StepsScene steps;
static JobScene job;
static ArcScene arc;
static FetchScene fetchSc;
static bool stepsOn = false, jobOn = false, arcOn = false, jobPlanned = false, fetchOn = false;
enum : uint8_t { BY_PLAN, BY_STRIPE, BY_PROGRESS };
static uint8_t stageBy = BY_PLAN;
static char lastStripe[96];
static bool stripeFresh = false;
static String jobName, jobApp;
static bool jobDownload = false;
static uint32_t lastBytes = 0, lastBytesAt = 0;

void orbEndTransient() {
    if (fetchOn) { // the next screen wipes in over it
        removeAfterTransition(fetchSc);
        fetchOn = false;
    }
    if (arcOn) {
        remove(arc);
        arcOn = false;
    }
    if (jobOn) {
        if (job.finished && !job.failed) { // let the result be seen
            uint32_t t0 = launcherMillis();
            while (launcherMillis() - t0 < 900 && !job.anyKey) frame();
        }
        jobOn = false;
        jobPlanned = false;
        jobName = "";
        jobApp = "";
        if (top() == &job) pop(TR_WIPE);
        else remove(job);
    }
    if (stepsOn) {
        remove(steps);
        stepsOn = false;
    }
    stripeFresh = false;
}
int orbRun(Scene &s) {
    orbEndTransient();
    return run(s);
}

static void startJob(bool planned) {
    if (stepsOn) {
        remove(steps);
        stepsOn = false;
    }
    if (arcOn) {
        remove(arc);
        arcOn = false;
    }
    if (!jobOn) {
        const char *nm = jobName.length() ? jobName.c_str() : "";
        job.reset(jobDownload || prog_handler == 2, nm);
        job.t = 0;
        push(job, TR_WIPE);
        jobOn = true;
    }
    jobPlanned = planned;
    stageBy = BY_PLAN;
    lastBytes = 0;
    lastBytesAt = launcherMillis();
}
void orbJobBegin(const String &name, bool download) {
    jobName = name;
    jobDownload = download;
    if (jobOn) {
        strncpy(job.name, name.c_str(), sizeof(job.name) - 1);
        job.download = download;
    }
}
void orbJobStage(const char *label, uint32_t size, uint32_t ofs, bool app) {
    if (!jobOn) startJob(true);
    jobPlanned = true;
    job.addStage(label, size, ofs, app);
}
void orbJobAppName(const String &name) { jobApp = name; }

static const char *progLabel() {
    return prog_handler == 2 ? "DOWNLOADING" : prog_handler == 1 ? "COPYING DATA" : "INSTALLING FW";
}

static bool contains(const String &s, const char *w) {
    String a = s;
    a.toLowerCase();
    return a.indexOf(w) >= 0;
}

void displayRedStripe(const String &text, uint16_t, uint16_t, bool keepAwake) {
    Serial.println(String("Display Red Stripe: ") + text);
    if (keepAwake) wakeUpScreen();
    if (!ready) return;
    if (foreign()) {
        strncpy(remote.stripe, text.c_str(), sizeof(remote.stripe) - 1);
        remote.stripeSeq++;
        return;
    }
    String t = text;
    t.trim();
    if (jobOn && !job.finished) {
        if (contains(t, "downloaded")) {
            String line = String("Saved to ") + dwn_path;
            job.finish(line.c_str(), "ANY KEY TO RETURN");
        } else if (contains(t, "restarting")) {
            String line = String("Restarting into ") + (jobApp.length() ? jobApp : jobName);
            job.finish(line.c_str(), "SLOT SET \xC2\xB7 SEE YOU ON THE PAD");
        } else if (jobPlanned) {
            bool fresh = job.si < job.nst && job.bytes == 0;
            if (!(stageBy == BY_PROGRESS && fresh) && job.si + 1 < job.nst) job.goStage(job.si + 1);
            else if (!(stageBy == BY_PROGRESS && fresh) && job.si + 1 >= job.nst)
                job.goStage(job.addStage(t.c_str(), 0x4000));
            stageBy = BY_STRIPE;
        } else {
            bool fresh = job.si < job.nst && job.bytes == 0;
            if (stageBy == BY_PROGRESS && fresh && job.si < job.nst) {
                int i = 0; // name the stage that has just started
                for (; t[i] && i < 19; i++)
                    job.st[job.si].label[i] = t[i] >= 'a' && t[i] <= 'z' ? t[i] - 32 : t[i];
                while (i > 0 && (job.st[job.si].label[i - 1] == '.' || job.st[job.si].label[i - 1] == ' '))
                    i--;
                job.st[job.si].label[i] = 0;
            } else job.goStage(job.addStage(t.c_str(), 0x4000));
            stageBy = BY_STRIPE;
        }
        tick(0);
        return;
    }
    if (jobOn) return; // finished: the job screen says it already
    if (!stepsOn) {
        steps = StepsScene();
        push(steps, TR_NONE);
        stepsOn = true;
    }
    steps.add(t.c_str());
    strncpy(lastStripe, t.c_str(), sizeof(lastStripe) - 1);
    lastStripe[sizeof(lastStripe) - 1] = 0;
    stripeFresh = true;
    tick(0);
}

void progressHandler(size_t progress, size_t total) {
    wakeUpScreen();
    if (!ready) return;
    if (foreign()) {
        remote.done = progress;
        remote.total = total;
        remote.progress = true;
        return;
    }
    uint32_t now_ = launcherMillis();
    if (!jobOn) {
        // carry the steps shown so far into the job as finished stages
        char carried[StepsScene::MAXS][32];
        int nc = 0;
        if (stepsOn)
            for (int i = steps.first; i < steps.n; i++)
                strcpy(carried[nc++], steps.steps[i % StepsScene::MAXS]);
        bool lastFresh = stripeFresh && nc > 0;
        startJob(false);
        for (int i = 0; i < nc; i++) job.addStage(carried[i], 0x4000);
        if (lastFresh) {
            job.goStage(nc - 1);
            stageBy = BY_STRIPE;
        } else {
            job.goStage(job.addStage(progLabel(), total));
            stageBy = BY_PROGRESS;
        }
        stripeFresh = false;
    } else if (job.finished) {
        if (progress != 0) return;
        // a new transfer after a finished one ("update all"): start over
        job.reset(jobDownload || prog_handler == 2, jobName.c_str());
        job.goStage(job.addStage(progLabel(), total));
        stageBy = BY_PROGRESS;
    } else if (progress == 0) {
        bool fresh = job.si < job.nst && job.bytes == 0;
        if (!(stageBy != BY_PROGRESS && fresh) && !(job.nst == 0)) {
            if (jobPlanned && job.si + 1 < job.nst) job.goStage(job.si + 1);
            else job.goStage(job.addStage(progLabel(), total));
            stageBy = BY_PROGRESS;
        } else if (job.nst == 0) {
            job.goStage(job.addStage(progLabel(), total));
            stageBy = BY_PROGRESS;
        }
        lastBytes = 0;
        lastBytesAt = now_;
    }
    if (progress > lastBytes && now_ > lastBytesAt) {
        float inst = (progress - lastBytes) * 1000.0f / (now_ - lastBytesAt);
        job.speed = job.speed > 0 ? job.speed * 0.85f + inst * 0.15f : inst;
        lastBytes = progress;
        lastBytesAt = now_;
    }
    job.progress(progress, total);
    uint32_t before = now_;
    tick(66);
    if (launcherMillis() != before) vTaskDelay(1); // a frame went out: let the idle task run
}

static bool looksOk(const String &s) {
    static const char *const W[] = {
        "complete",
        "saved",
        "restored",
        "success",
        "done",
        "formatted",
        "bound",
        "renamed",
        "erased",
        "linked",
        "activated",
        "installed",
        "removed"
    };
    for (const char *w : W)
        if (contains(s, w)) return true;
    return false;
}

static void message(const String &txt, bool waitKeyPress, uint8_t kind) {
    if (!ready) return;
    if (foreign()) {
        strncpy(remote.msg, txt.c_str(), sizeof(remote.msg) - 1);
        remote.failed = kind == DK_ERROR;
        remote.msgSeq++;
        return;
    }
    if (jobOn) {
        if (kind == DK_ERROR) {
            job.failed = true;
            if (!job.finished) job.finish("", "");
            strncpy(job.doneLine, txt.c_str(), sizeof(job.doneLine) - 1);
            job.doneLine[sizeof(job.doneLine) - 1] = 0;
        } else {
            String line = txt;
            line.trim();
            if (!job.download && (contains(line, "complete") || contains(line, "installed"))) {
                line = String("Restarting into ") + (jobApp.length() ? jobApp : jobName);
                job.doneWait = 2;
            }
            if (!job.finished) job.finish(line.c_str(), "");
            else strncpy(job.doneLine, line.c_str(), sizeof(job.doneLine) - 1);
            job.doneLine[sizeof(job.doneLine) - 1] = 0;
            job.doneT = 0;
            job.anyKey = false;
            uint32_t t0 = launcherMillis();
            while ((waitKeyPress || launcherMillis() - t0 < 2000) && !job.anyKey) frame();
            return;
        }
    }
    if (stepsOn && kind == DK_ERROR) steps.failed = true;
    stripeFresh = false;
    DialogOpts o;
    o.kind = kind;
    String m = txt;
    m.trim();
    o.msg = m.c_str();
    o.autoClose = waitKeyPress ? 0 : 2.0f;
    dialog(o);
    if (stepsOn && kind != DK_ERROR) { // the message ends the steps it reported
        remove(steps);
        stepsOn = false;
    }
}
void displayError(String txt, bool waitKeyPress) {
    Serial.println("ERR: " + txt);
    message(txt, waitKeyPress, DK_ERROR);
}
void displayMsg(String txt, bool waitKeyPress) { message(txt, waitKeyPress, looksOk(txt) ? DK_OK : DK_INFO); }

// Mirror what other tasks reported into the job screen; true while one is up
bool orbSyncRemote() {
    static uint32_t stripeSeen = 0, msgSeen = 0;
    if (remote.progress) {
        remote.progress = false;
        if (!jobOn) {
            jobName = "WEB UPLOAD";
            jobDownload = false;
        }
        progressHandler(remote.done, remote.total);
    }
    if (remote.stripeSeq != stripeSeen) {
        stripeSeen = remote.stripeSeq;
        String t = remote.stripe;
        if (jobOn && contains(t, "restart")) job.finish("Restart to boot the new app", "ANY KEY TO RETURN");
        else displayRedStripe(t);
    }
    if (remote.msgSeq != msgSeen) {
        msgSeen = remote.msgSeq;
        if (jobOn && remote.failed) {
            job.failed = true;
            if (!job.finished) job.finish(remote.msg, "ANY KEY TO RETURN");
        } else toast(remote.msg, remote.failed ? TK_ERR : TK_INFO, 2.5f);
    }
    if (jobOn && job.finished && job.anyKey) orbEndTransient();
    return jobOn;
}

void orbArcBegin(const char *title, const String &sub) {
    if (foreign()) return; // drawn by the UI task only
    orbEndTransient();
    static String s;
    s = sub;
    arc = ArcScene();
    arc.title = title;
    arc.sub = s.c_str();
    push(arc, TR_NONE);
    arcOn = true;
    tick(0);
}
void orbArcProgress(uint32_t done, uint32_t total) {
    if (!arcOn || foreign()) return;
    arc.p = total ? clampf((float)done / total, 0, 1) : 0;
    tick(66);
}
void orbArcEnd() {
    if (!arcOn || foreign()) return;
    arc.p = 1;
    uint32_t t0 = launcherMillis();
    while (launcherMillis() - t0 < 160) frame(false);
    remove(arc);
    arcOn = false;
}
void orbToast(const char *msg, bool ok) { toast(msg, ok ? TK_OK : TK_ERR); }

// ── generic menus ───────────────────────────────────────────────────────────
struct IconWord {
    const char *w;
    uint8_t icon;
};
static const IconWord ICONS[] = {
    {"install",   G_INSTALL},
    {"delete",    G_TRASH  },
    {"remove",    G_TRASH  },
    {"rename",    G_PEN    },
    {"copy",      G_COPY   },
    {"paste",     G_PASTE  },
    {"folder",    G_NEWDIR },
    {"main menu", G_HOME   },
    {"back",      G_BACK   },
    {"cancel",    G_BACK   },
    {"restore",   G_RESTORE},
    {"backup",    G_BKP    },
    {"detail",    G_INFO   },
    {"download",  G_DL     },
    {"favorite",  G_STAR   },
    {"bind",      G_BIND   },
    {"launch",    G_PLAY   },
    {"retry",     G_RESET  },
    {"add ",      G_PLUS   },
    {"size",      G_SORT   },
    {"format",    G_RESET  },
    {"associate", G_TAG    },
    {"attach",    G_TAG    },
    {"search",    G_SEARCH },
    {"order",     G_SORT   },
    {"star",      G_STAR   },
    {"ota",       G_SAT    },
    {"hidden",    G_EYE    },
    {"wipe",      G_WARN   },
    {"erase",     G_WARN   },
    {"update",    G_DL     },
    {"view",      G_EYE    },
    {"launcher",  G_ROCKET },
};
uint8_t orbOptIcon(const String &label) {
    String l = label;
    l.toLowerCase();
    for (const IconWord &w : ICONS)
        if (l.indexOf(w.w) >= 0) return w.icon;
    return 255;
}
static bool isDanger(const String &label) {
    static const char *const D[] = {"delete", "remove", "erase", "wipe", "format", "discard", "reset"};
    for (const char *w : D)
        if (contains(label, w)) return true;
    return false;
}
uint8_t orbOptKind(const String &l) {
    if (isDanger(l)) return RK_DANGER;
    if (contains(l, "main menu") || l == "Back" || l == "Cancel" || contains(l, "back to")) return RK_OP;
    return RK_ITEM;
}
struct OptSrc : ListSrc {
    std::vector<Option> &o;
    explicit OptSrc(std::vector<Option> &v) : o(v) {}
    int count() override { return o.size(); }
    uint8_t kind(int i) override {
        return o[i].color == ALCOLOR && !isDanger(o[i].label) ? RK_OP : orbOptKind(o[i].label);
    }
    void row(int i, Row &r) override {
        r.label = o[i].label.c_str();
        r.icon = orbOptIcon(o[i].label);
        r.kind = kind(i);
    }
};

int loopOptions(std::vector<Option> &options, bool, uint16_t, uint16_t, bool, int index) {
    if (options.empty() || returnToMenu) return -1;
    if (!ready) orbInit();
    String q = stripeFresh ? String(lastStripe) : String();
    orbEndTransient();
    int n = options.size(), picked = -1;
    bool asDialog = false;
    char btn[3][24];
    const char *bp[3];
    if (q.length() && n <= 3) {
        int w = 0;
        for (int i = 0; i < n; i++) {
            String l = options[i].label;
            l.toUpperCase();
            strncpy(btn[i], l.c_str(), 23);
            btn[i][23] = 0;
            bp[i] = btn[i];
            w += textW(F_MICRO, btn[i]) + 16;
        }
        asDialog = w <= 168;
    }
    if (asDialog) {
        DialogOpts o;
        o.kind = DK_CONFIRM;
        o.msg = q.c_str();
        o.buttons = bp;
        o.nbtn = n;
        o.sel = index >= 0 && index < n ? index : 0;
        for (int i = 0; i < n; i++)
            if (isDanger(options[i].label)) o.danger |= 1 << i;
        picked = dialog(o);
    } else {
        OptSrc src(options);
        picked =
            sheet(&src, "OPTIONS", q.length() ? q.c_str() : nullptr, index >= 0 && index < n ? index : 0);
    }
    if (picked < 0 || picked >= n) return -1;
    if (options[picked].operation) options[picked].operation();
    return picked;
}

bool orbAsk(
    const char *title, const String &msg, const char *const *buttons, int n, int sel, uint8_t dangerMask,
    int &picked
) {
    orbEndTransient();
    DialogOpts o;
    o.kind = DK_CONFIRM;
    o.title = title;
    o.msg = msg.c_str();
    o.buttons = buttons;
    o.nbtn = n;
    o.sel = sel;
    o.danger = dangerMask;
    picked = dialog(o);
    return picked >= 0;
}

void orbInfo(
    const char *title, const char *status, bool warn, const char *const *keys, const String *vals, int n
) {
    InfoScene s;
    s.title = title;
    s.status = status;
    s.statusC = warn ? C(T_warn) : C(T_ok);
    for (int i = 0; i < n && i < 8; i++) {
        s.keys[i] = keys[i];
        s.vals[i] = vals[i].c_str();
    }
    s.n = n < 8 ? n : 8;
    modal(s, TR_NONE);
}

String keyboard(String current_text, int max_size, const String &textbox_title) {
    if (!ready) orbInit();
    orbEndTransient();
    if (max_size > 120) max_size = 120;
    if (max_size < 1) max_size = 1;
    char buf[124];
    strncpy(buf, current_text.c_str(), max_size);
    buf[max_size] = 0;
    String t = textbox_title;
    t.trim();
    while (t.endsWith(":")) {
        t.remove(t.length() - 1);
        t.trim();
    }
    t.toUpperCase();
    if (!askText(t.c_str(), nullptr, buf, max_size, t.indexOf("PASSWORD") >= 0))
        return String((char)KEY_ESCAPE);
    return String(buf);
}

// ── boot screen ─────────────────────────────────────────────────────────────
static BootApp bootApps[10];
static int nBoot = 0;
static BootScene boot;
static Particle *bootParts = nullptr;
static bool bootOnStack = false;

static void copyCap(char *dst, const String &s, int n) {
    strncpy(dst, s.c_str(), n - 1);
    dst[n - 1] = 0;
}
static void loadApps() {
    std::vector<LauncherAppMetadata> apps = launcherListInstalledApps();
    LauncherPartitionTable table;
    bool haveTable = launcherPartitionReadCurrent(table, nullptr);
    nBoot = 0;
    ninstalled = 0;
    for (const LauncherAppMetadata &a : apps) {
        if (nBoot >= 10) break;
        BootApp &b = bootApps[nBoot++];
        copyCap(b.name, a.name.length() ? a.name : a.label, sizeof(b.name));
        copyCap(b.label, a.label, sizeof(b.label));
        const LauncherPartitionEntry *e =
            haveTable ? launcherPartitionFindByLabel(table, a.label.c_str()) : nullptr;
        b.size = e ? e->size : 0;
        copyCap(installed[ninstalled++], a.label, sizeof(installed[0]));
    }
}
static int bootPartitionApp() {
    const esp_partition_t *bp = esp_ota_get_boot_partition();
    if (!bp || bp->type != ESP_PARTITION_TYPE_APP || bp->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_0) return -1;
    for (int i = 0; i < nBoot; i++)
        if (!strcmp(bootApps[i].label, bp->label)) return i;
    return -1;
}
static void prepareBoot() {
    loadApps();
    boot = BootScene();
    boot.apps = bootApps;
    boot.napps = nBoot;
    boot.app = bootPartitionApp();
    if (boot.app < 0 && nBoot == 1) boot.app = 0;
    boot.bootToApp = bootToApp;
    boot.total = 1 + (bootToApp ? bootTimer : 0);
    boot.version = versionUp;
    if (!bootParts) bootParts = new Particle[220];
    boot.start(bootParts, bootParts ? 220 : 0);
}

void initDisplay(bool doAll) {
    orbInit();
    if (!bootOnStack && depth() == 0) {
        prepareBoot();
        push(boot, TR_NONE);
        bootOnStack = true;
    }
    if (doAll) redraw();
}
void initDisplayLoop() { // dev mode: watch the boot sequence without booting
    orbEndTransient();
    prepareBoot();
    push(boot, TR_FADE);
    for (;;) {
        int r = run(boot);
        if (r != 100) break;
        boot.hold();
    }
    pop(TR_FADE);
    returnToMenu = true;
}

int orbBootScreen() {
    initDisplay(false);
    uint32_t nextMsg = 0;
    resetGlobals();
    for (;;) {
        while (boot.res == RUNNING) {
            frame();
            if (launcherMillis() > nextMsg) {
                launcherConsolePrintln("Press the button to enter the Launcher!");
                nextMsg = launcherMillis() + 500;
            }
        }
        int r = boot.res;
        boot.res = RUNNING;
        if (r != 100) {
            delete[] bootParts;
            bootParts = nullptr;
            boot.parts = nullptr;
            boot.nparts = boot.maxParts = 0;
            return r;
        }
        // several apps installed and any key pressed: which one?
        MenuItem items[11];
        char keys[10][2];
        int n = nBoot < 10 ? nBoot : 10;
        for (int i = 0; i < n; i++) {
            keys[i][0] = '0' + (i + 1) % 10;
            keys[i][1] = 0;
            items[i] = {bootApps[i].name, G_CART, RK_ITEM, keys[i]};
        }
        items[n] = {"Launcher", G_ROCKET, RK_OP, nullptr};
        char sub[24];
        sprintf(sub, "%d installed", n);
        int p = sheet(items, n + 1, "BOOT WHICH APP?", sub, boot.app >= 0 ? boot.app : 0);
        if (p >= 0 && p < n) boot.launch(p);
        else if (p == n) boot.hold();
        else boot.resumeCount();
    }
}

// ── home ────────────────────────────────────────────────────────────────────
static Tile tiles[20];
static HomeScene home;
static int nTiles = 0;

static void devModeOn() {
    dev_mode = true;
    saveConfigs();
}
static Tile &addTile(uint8_t id, const char *title, const char *desc, const char *meta, uint8_t group) {
    Tile &t = tiles[nTiles++];
    t = Tile();
    t.id = id;
    strncpy(t.title, title, sizeof(t.title) - 1);
    strncpy(t.desc, desc, sizeof(t.desc) - 1);
    strncpy(t.meta, meta, sizeof(t.meta) - 1);
    t.group = group;
    return t;
}
static void buildTiles() {
    nTiles = 0;
    loadApps();
    char meta[48], sz[12];
    addTile(TL_SD, "SD CARD", "Launch or manage firmware on SD", "", 0);
    wifi_ap_record_t ap;
    if (launcherWifiIsConnected() && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        snprintf(meta, sizeof(meta), "%.24s \xC2\xB7 CONNECTED", (const char *)ap.ssid);
        for (char *p = meta; *p; p++)
            if (*p >= 'a' && *p <= 'z') *p -= 32;
    } else strcpy(meta, "WI-FI SCANS WHEN OPENED");
    addTile(TL_OTA, "ONLINE", "Install from LauncherHub", meta, 0);
    addTile(TL_WUI, "WEB UI", "Manage files from a browser", "LAUNCHER.LOCAL \xC2\xB7 AP OR HOME WI-FI", 0);
#if defined(SOC_USB_OTG_SUPPORTED) && !defined(DISABLE_MASS_STORAGE)
    addTile(TL_USB, "USB DRIVE", "Mount the SD card on a computer", "", 0);
#endif
    LauncherPartitionTable table;
    uint32_t freeB = 0, flash = 0;
    bool haveTable = launcherPartitionReadCurrent(table, nullptr);
    if (haveTable) {
        flash = table.flashSize;
        for (const LauncherPartitionRange &r : launcherPartitionFreeRanges(table)) freeB += r.size;
        fmtSize(sz, freeB);
        String s = sz;
        s.replace("KB", " KB");
        s.replace("MB", " MB");
        snprintf(meta, sizeof(meta), "%s FREE OF %d MB", s.c_str(), (int)(flash >> 20));
    } else strcpy(meta, "FLASH MAP");
    addTile(TL_PMAN, "PARTITIONS", "See and edit the flash map", meta, 0);
    snprintf(meta, sizeof(meta), "THEME %s \xC2\xB7 BRIGHT %d%%", THEME_NAMES[themeIdx], bright);
    addTile(TL_CFG, "SETTINGS", "Display, files, boot and power", meta, 0);
    int bootIdx = bootPartitionApp();
    for (int i = 0; i < nBoot && nTiles < 18; i++) {
        BootApp &a = bootApps[i];
        char up[22], lab[18], desc[40];
        int k = 0;
        for (; a.name[k] && k < 21; k++)
            up[k] = a.name[k] >= 'a' && a.name[k] <= 'z' ? a.name[k] - 32 : a.name[k];
        up[k] = 0;
        for (k = 0; a.label[k] && k < 17; k++)
            lab[k] = a.label[k] >= 'a' && a.label[k] <= 'z' ? a.label[k] - 32 : a.label[k];
        lab[k] = 0;
        fmtSize(sz, a.size);
        snprintf(desc, sizeof(desc), "Installed app \xC2\xB7 %s", sz);
        snprintf(
            meta, sizeof(meta), "%sSLOT %s", i == bootIdx ? "\xE2\x96\xB6 BOOTS NEXT \xC2\xB7 " : "", lab
        );
        Tile &t = addTile(TL_APP, up, desc, meta, 1);
        t.bootsNext = i == bootIdx;
        t.fam = famOf(a.label);
        strcpy(t.label, a.label);
        int n = 0;
        for (k = 0; a.name[k] && n < 2; k++)
            if (isalnum((unsigned char)a.name[k])) t.ini[n++] = toupper((unsigned char)a.name[k]);
        t.ini[n] = 0;
    }
    addTile(TL_CHG, "CHARGE", "Dim, low-power charging screen", "", 2);
    addTile(TL_OFF, "POWER OFF", "Turn the device off", "DEEP SLEEP \xC2\xB7 G0 WAKES IT", 2);
}

static void shortcut(char c) {
    String key = String(c), boundPath;
    if (!sdcardMounted || !getKeyBinding(key, boundPath)) return;
    if (SDM.exists(boundPath)) {
        char msg[64];
        snprintf(
            msg,
            sizeof(msg),
            "Shortcut '%c' \xE2\x86\x92 %s",
            toupper((unsigned char)c),
            boundPath.substring(boundPath.lastIndexOf('/') + 1).c_str()
        );
        toast(msg, TK_INFO, 1.2f);
        uint32_t t0 = launcherMillis();
        while (launcherMillis() - t0 < 650) frame(false);
        updateFromSD(boundPath); // reboots on success; only returns on failure
    } else {
        displayError("File not found");
        removeKeyBinding(key);
        displayMsg(String("'") + key + "' removed");
    }
}

void orbMainMenu() {
    static bool first = true;
    if (!ready) orbInit();
    buildTiles();
    home.tiles = tiles;
    home.n = nTiles;
    if (home.sel >= nTiles) home.sel = nTiles - 1;
    home.version = versionUp;
    home.devMode = dev_mode;
    home.onDevMode = devModeOn;
    {
        String bp;
        bool any = false;
        if (sdcardMounted)
            for (char c = 'a'; c <= 'z' && !any; c++) any = getKeyBinding(String(c), bp);
        home.shortcuts = any;
    }
    orbEndTransient();
    if (first) { // what the stock loop() does on entry
        getBrightness();
        launcherConsolePrintln("Type 'help' for Serial commands.");
        if (!sdcardMounted) home.setSel(1);
        push(home, TR_FADE);
        first = false;
    } else if (!onStack(home)) push(home, TR_NONE);
    returnToMenu = false;
    resetGlobals();
    home.res = RUNNING;
    while (home.res == RUNNING) {
        frame();
        if (bootOnStack && depth() > 1 && at(0) == &boot && !transitioning()) { // the boot screen faded out
            remove(boot);
            bootOnStack = false;
        }
        checkReboot();
    }
    int r = home.res;
    home.res = RUNNING;
    if (r >= 1000) return shortcut((char)(r - 1000));
    if (r < 0 || r >= nTiles) return;
    const Tile &t = tiles[r];
    switch (t.id) {
        case TL_SD: loopSD(false); break;
        case TL_OTA: ota_function(); break;
        case TL_WUI: loopOptionsWebUi(); break;
#if defined(SOC_USB_OTG_SUPPORTED) && !defined(DISABLE_MASS_STORAGE)
        case TL_USB:
            if (setupSdCard()) MassStorage();
            else displayError("Insert SD Card");
            break;
#endif
        case TL_PMAN: partList(); break;
        case TL_CFG: settings_menu(); break;
        case TL_APP: launcherShowAppActions(t.label); break;
        case TL_CHG: orbChargeMode(); break;
        case TL_OFF: orbPowerOff(); break;
    }
    returnToMenu = false;
}

// ── charge, power off ───────────────────────────────────────────────────────
void chargeMode() { orbChargeMode(); }
void orbChargeMode() {
    orbEndTransient();
#ifndef CONFIG_IDF_TARGET_ESP32P4
    setCpuFrequencyMhz(80);
#endif
    setBrightness(25, false);
    ChargeScene c;
    c.volts = launcherBatteryVolts();
    push(c, TR_FADE);
    while (c.res == RUNNING) {
        frame();
        vTaskDelay(pdMS_TO_TICKS(110)); // a few frames a second is plenty here
        static uint32_t lastV = 0;
        if (launcherMillis() - lastV > 5000) {
            lastV = launcherMillis();
            c.volts = launcherBatteryVolts();
        }
    }
    pop(TR_FADE);
#ifndef CONFIG_IDF_TARGET_ESP32P4
    setCpuFrequencyMhz(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
#endif
    setBrightness(bright, false);
}
void orbPowerOff() {
    orbEndTransient();
    PowerOffScene p;
    p.snap = (uint16_t *)heap_caps_malloc(W * H * 2, MALLOC_CAP_8BIT);
    push(p, TR_NONE);
    if (p.snap) snapshotBelow(p.snap);
    while (p.res == RUNNING) frame(false);
    powerOff();
}

void orbWebLog(int method, const char *uri) {
    char line[40];
    snprintf(
        line,
        sizeof(line),
        "%s %s",
        method == 3   ? "POST"
        : method == 1 ? "GET"
        : method == 0 ? "DELETE"
                      : "REQ",
        uri ? uri : ""
    );
    WebRunScene::logRequest(line);
}

// ── LauncherHub catalog ─────────────────────────────────────────────────────
struct CatSrc : ListSrc {
    bool upd = false, canUpdateAll = false;
    String q, order;
    bool star = false;
    int page = 1, pages = 1, items = 0;
    char ver[16][24];
    // rows: refine | update all, [prev], items..., [next], main menu
    int nTop() const { return 1 + (page > 1 ? 1 : 0); }
    int count() override { return nTop() + items + (page < pages ? 1 : 0) + 1; }
    int itemAt(int i) const { return i - nTop(); }
    uint8_t kind(int i) override {
        int k = itemAt(i);
        return k >= 0 && k < items ? RK_ITEM : RK_OP;
    }
    void row(int i, Row &r) override {
        if (i == 0) {
            if (upd) {
                r.label = canUpdateAll ? "Update all" : "Updates";
                r.icon = G_DL;
                snprintf(r.right, sizeof(r.right), "%d NEW", items);
                r.sub = "DOWNLOADS EVERY NEW VERSION TO SD";
            } else {
                static String lab;
                lab = q.length() ? String("\"") + q + "\"" : String("Search, sort, filter");
                r.label = lab.c_str();
                r.icon = G_SEARCH;
                snprintf(
                    r.right,
                    sizeof(r.right),
                    "%s%s",
                    star ? "\xE2\x98\x85 " : "",
                    order == "name"   ? "A-Z"
                    : order == "date" ? "NEW"
                                      : "TOP"
                );
                r.sub = "TEXT SEARCH \xC2\xB7 ORDER \xC2\xB7 STARRED ONLY";
            }
            r.kind = RK_OP;
            return;
        }
        if (page > 1 && i == 1) {
            r.label = "\xE2\x80\xB9 Previous page";
            r.kind = RK_OP;
            snprintf(r.right, sizeof(r.right), "P%d", page - 1);
            return;
        }
        int k = itemAt(i);
        if (k >= 0 && k < items) {
            JsonObject it = doc["items"][k];
            r.label = it["name"] | "?";
            bool st = it["star"].as<bool>();
            r.color = st ? C(T_hi) : C(T_text);
            snprintf(
                r.right,
                sizeof(r.right),
                "%s%s",
                st ? "\xE2\x98\x85 " : "",
                (const char *)(it["version"] | "")
            );
            r.rightC = st ? C(T_acc) : C(T_dim);
            static char sub[48];
            snprintf(sub, sizeof(sub), "BY %s", (const char *)(it["author"] | "?"));
            for (char *p = sub; *p; p++)
                if (*p >= 'a' && *p <= 'z') *p -= 32;
            r.sub = sub;
            return;
        }
        if (page < pages && k == items) {
            r.label = "Next page \xE2\x80\xBA";
            r.kind = RK_OP;
            snprintf(r.right, sizeof(r.right), "P%d", page + 1);
            return;
        }
        r.label = "Main menu";
        r.icon = G_HOME;
        r.kind = RK_OP;
    }
    bool icon(int i, Gfx &g, int x, int y, bool sel) override {
        int k = itemAt(i);
        if (k < 0 || k >= items) return false;
        const char *fid = doc["items"][k]["fid"] | "?";
        drawEmblem(g, fid, x, y + 2, 1, sel ? C(T_onAcc) : famC(famOf(fid), 0), sel ? C(T_accD) : C(T_bg3));
        return true;
    }
};

static void fetchLabel(const char *l);
void loopVersions(const String &fid) {
    fetchLabel("GETTING VERSIONS");
    JsonDocument item = getVersionInfo(fid);
    if (item.isNull()) return;
    JsonArray versions = item["versions"];
    static FwVersion vers[24];
    int nv = 0;
    for (JsonObject v : versions) {
        if (nv >= 24) break;
        copyCap(vers[nv].v, String(v["version"] | ""), sizeof(vers[nv].v));
        copyCap(vers[nv].date, String(v["published_at"] | ""), sizeof(vers[nv].date));
        nv++;
    }
    FirmwareScene f;
    copyCap(f.name, String(item["name"] | ""), sizeof(f.name));
    copyCap(f.author, String(item["author"] | ""), sizeof(f.author));
    copyCap(f.fid, fid, sizeof(f.fid));
    f.star = item["star"].as<bool>();
    f.vers = vers;
    f.nv = nv;
    loadApps();
    String lname = f.name;
    lname.toLowerCase();
    for (int i = 0; i < nBoot; i++) {
        String an = bootApps[i].name;
        an.toLowerCase();
        if (an.length() && (lname.indexOf(an) >= 0 || an.indexOf(lname) >= 0)) {
            String s = String("SLOT ") + bootApps[i].label;
            s.toUpperCase();
            copyCap(f.onDevice, s, sizeof(f.onDevice));
            break;
        }
    }
    orbEndTransient();
    push(f);
    returnToMenu = false;
    for (;;) {
        if (f.nv) copyCap(f.file, String(versions[f.vi]["file"] | ""), sizeof(f.file));
        int r = orbRun(f);
        if (r < 0 || returnToMenu) break;
        JsonObject ver = versions[f.vi];
        String name = item["name"] | "", author = item["author"] | "", version = ver["version"] | "",
               file = ver["file"] | "";
        bool isFav = false;
        for (JsonObject fv : favorite)
            if (fv["fid"].as<String>() == fid) isFav = true;
        MenuItem m[4];
        int n = 0;
        m[n++] = {"Install (OTA)", G_INSTALL, RK_ITEM, nullptr};
        if (sdcardMounted) {
            m[n++] = {"Download to SD", G_DL, RK_ITEM, nullptr};
            m[n++] = {isFav ? "Remove favorite" : "Add to favorites", G_STAR, RK_ITEM, nullptr};
        }
        m[n++] = {"Back to list", G_BACK, RK_OP, nullptr};
        char title[40];
        snprintf(title, sizeof(title), "V%s", version.c_str());
        int p = sheet(m, n, title, name.c_str());
        if (p == 0) {
            orbJobBegin(name + " " + version, false);
            orbJobAppName(name);
            installFirmwareFromManifest(fid, version, name + " - " + version);
        } else if (p == 1 && sdcardMounted) {
            orbJobBegin(name + "." + version + ".bin", true);
            downloadFirmware(fid, file, name + "." + version.substring(0, 10), dwn_path, version);
        } else if (p == 2 && sdcardMounted) {
            if (isFav) {
                for (size_t i = 0; i < favorite.size(); i++)
                    if (favorite[i]["fid"].as<String>() == fid) {
                        favorite.remove(i);
                        break;
                    }
                toast("Removed from favorites", TK_OK);
            } else {
                JsonObject fav = favorite.add<JsonObject>();
                fav["name"] = name + " - " + author + " (" + ota_tag + ")";
                fav["fid"] = fid;
                fav["link"] = "";
                toast("Added to favorites", TK_OK);
            }
            saveConfigs();
        } else if (p == n - 1) break;
        if (returnToMenu) break;
    }
    if (top() == &f) pop();
    else remove(f);
    returnToMenu = false;
}

void loopFirmware(bool isUpdate) {
    CatSrc src;
    src.upd = isUpdate;
    src.order = "downloads";
    ListScene sc;
    sc.hdr.icon = G_SAT;
    sc.hdr.title = isUpdate ? "UPDATES" : "LAUNCHERHUB";
    sc.pages = !isUpdate;
    static String crumb, rightHdr;
    bool fetched = true; // the caller fetched page current_page already
    int sel = -1;
    returnToMenu = false;
    for (;;) {
        if (!fetched) {
            fetchLabel(isUpdate ? "CHECKING UPDATES" : "GETTING RESULTS");
            bool ok = isUpdate ? checkForUpdates()
                               : GetJsonFromLauncherHub(current_page, src.order, src.star, src.q);
            if (!ok) break;
            fetched = true;
            sel = -1;
        }
        int ps = doc["page_size"] | 1;
        src.page = isUpdate ? 1 : (doc["page"] | 1);
        src.items = doc["items"].size();
        src.pages = isUpdate ? 1 : (total_firmware + ps - 1) / (ps > 0 ? ps : 1);
        if (src.pages < 1) src.pages = 1;
        src.canUpdateAll =
            isUpdate && sdcardMounted && src.items && !doc["items"][0]["file"].as<String>().isEmpty();
        crumb = isUpdate ? String("FROM ") + dwn_path : (src.q.length() ? String("SEARCH") : String(ota_tag));
        crumb.toUpperCase();
        sc.hdr.crumb = crumb.c_str();
        rightHdr = isUpdate ? String() : String("P") + src.page + "/" + src.pages;
        sc.hdr.right = rightHdr.c_str();
        snprintf(sc.right, sizeof(sc.right), isUpdate ? "" : "%d FIRMWARE", total_firmware);
        sc.hk = "\xE2\x8F\x8E|VIEW|\xE2\x97\x80\xE2\x96\xB6|PAGE|`|BACK";
        if (sel < 0) {
            sc.setup(&src, src.nTop(), 8);
            sel = sc.list.sel;
        } else sc.list.refresh(true);
        if (!onStack(sc)) push(sc);
        int r = orbRun(sc);
        sel = sc.list.sel;
        if (r == LS_BACK) break;
        if (r == LS_PREV || r == LS_NEXT) {
            if (isUpdate) continue;
            if (r == LS_PREV && current_page > 1) current_page--;
            else if (r == LS_NEXT && src.page < src.pages) current_page++;
            else continue;
            fetched = false;
            continue;
        }
        if (r == 0) {
            if (isUpdate) {
                if (!src.canUpdateAll) continue;
                int count = (int)doc["items"].size();
                for (int i = 0; i < count; i++) {
                    String fid = doc["items"][i]["fid"].as<String>(),
                           file = doc["items"][i]["file"].as<String>();
                    String name = doc["items"][i]["name"].as<String>(),
                           ver = doc["items"][i]["version"].as<String>();
                    orbJobBegin(name + "." + ver + ".bin", true);
                    downloadFirmware(fid, file, name + "." + ver.substring(0, 10), dwn_path, ver, true);
                    if (returnToMenu) break;
                }
                orbEndTransient();
                continue;
            }
            // refine
            static const char *const SORTS[3] = {"downloads", "name", "date"};
            MenuItem m[6] = {
                {"Most downloaded",         G_SORT,   RK_ITEM, src.order == "downloads" ? "\xE2\x9C\x93" : nullptr},
                {"Name A-Z",                G_SORT,   RK_ITEM, src.order == "name" ? "\xE2\x9C\x93" : nullptr     },
                {"Latest release",          G_SORT,   RK_ITEM, src.order == "date" ? "\xE2\x9C\x93" : nullptr     },
                {"Starred only",            G_STAR,   RK_ITEM, src.star ? "ON" : "OFF"                            },
                {"Text search\xE2\x80\xA6", G_SEARCH, RK_ITEM, nullptr                                            },
                {"Back to list",            G_BACK,   RK_OP,   nullptr                                            },
            };
            int cur = src.order == "name" ? 1 : src.order == "date" ? 2 : 0;
            char sub[24];
            sprintf(sub, "%d results", total_firmware);
            int p = sheet(m, 6, "REFINE", sub, cur);
            if (p >= 0 && p < 3) src.order = SORTS[p];
            else if (p == 3) src.star = !src.star;
            else if (p == 4) {
                String qq = keyboard(src.q, 76, "Search Firmware");
                if (qq == String((char)KEY_ESCAPE)) continue;
                src.q = qq;
            } else continue;
            current_page = 1;
            fetched = false;
            continue;
        }
        if (!isUpdate && src.page > 1 && r == 1) {
            current_page--;
            fetched = false;
            continue;
        }
        int k = src.itemAt(r);
        if (k >= 0 && k < src.items) {
            loopVersions(doc["items"][k]["fid"].as<String>()); // fetches into its own document
            returnToMenu = false;
            continue;
        }
        if (!isUpdate && src.page < src.pages && k == src.items) {
            current_page++;
            fetched = false;
            continue;
        }
        returnToMenu = true; // main menu
        break;
    }
    if (top() == &sc) pop();
    else remove(sc);
    doc.clear();
}

// ── downlink screen, worker tasks ──────────────────────────────────────────
const char *orbFetchText = "GETTING CATALOG";
static void fetchLabel(const char *l) { orbFetchText = l; }
FetchScene &orbFetchBegin(const String &path) {
    static String up;
    up = String("GET ") + path;
    up.toUpperCase();
    if (!fetchOn) {
        orbEndTransient();
        fetchSc = FetchScene();
        push(fetchSc);
        fetchOn = true;
    }
    fetchSc.label = orbFetchText;
    fetchSc.path = up.c_str();
    strcpy(fetchSc.tries, "TRY 1/5");
    orbFetchText = "GETTING INFO";
    return fetchSc;
}

struct Worker {
    void (*fn)(void *);
    void *arg;
    volatile bool done;
};
static void workerTask(void *p) {
    Worker *w = (Worker *)p;
    w->fn(w->arg);
    w->done = true;
    vTaskDelete(nullptr);
}
void orbWork(void (*fn)(void *), void *arg, uint32_t stack, bool input) {
    Worker w{fn, arg, false};
    if (xTaskCreate(workerTask, "orbWork", stack, &w, 1, nullptr) != pdPASS) {
        fn(arg);
        return;
    }
    while (!w.done) frame(input);
}

#endif
