// Scenarios: S-ids mirror the design mockup's screen registry (same sample data);
// addGallery() holds the README screens and films.
#include "harness.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <new>
#include <string>

using namespace orb;

// ── sample data, as in src/50_data.js ──
struct SimApp {
    const char *label, *name;
    uint32_t size;
};
static SimApp APPS[] = {{"bruce", "Bruce", 0x250000}, {"nemo", "NEMO", 0x100000}, {"evilc", "Evil-Cardputer", 0x120000}};
static const char *bootApp = "bruce";

static void baseStatus() {
    ST = Status();
    ST.battery = 78;
    ST.charging = false;
    ST.sd = true;
    ST.wifi = false;
    setTheme(0);
    ninstalled = 3;
    for (int i = 0; i < 3; i++) strcpy(installed[i], APPS[i].label);
}

static Tile tiles[16];
static int buildTiles() {
    int n = 0;
    auto T = [&](uint8_t id, const char *title, const char *desc, const char *meta, uint8_t group) -> Tile & {
        Tile &t = tiles[n++];
        t = Tile();
        t.id = id;
        strcpy(t.title, title);
        strcpy(t.desc, desc);
        strcpy(t.meta, meta);
        t.group = group;
        return t;
    };
    T(TL_SD, "SD CARD", "Launch or manage firmware on SD", "", 0);
    T(TL_OTA, "ONLINE", "Install from LauncherHub", ST.wifi ? "HOMENET \xC2\xB7 22 FIRMWARE" : "WI-FI SCANS WHEN OPENED", 0);
    T(TL_WUI, "WEB UI", "Manage files from a browser", "LAUNCHER.LOCAL \xC2\xB7 AP OR HOME WI-FI", 0);
    T(TL_USB, "USB DRIVE", "Mount the SD card on a computer", "", 0);
    T(TL_PMAN, "PARTITIONS", "See and edit the flash map", "2.1 MB FREE OF 8 MB", 0);
    T(TL_CFG, "SETTINGS", "Display, files, boot and power", "THEME NOVA \xC2\xB7 BRIGHT 80%", 0);
    for (auto &a : APPS) {
        char up[22], desc[40], meta[44], sz[12];
        int i = 0;
        for (; a.name[i]; i++) up[i] = toupper(a.name[i]);
        up[i] = 0;
        fmtSize(sz, a.size);
        sprintf(desc, "Installed app \xC2\xB7 %s", sz);
        bool boots = !strcmp(bootApp, a.label);
        char lab[18];
        for (i = 0; a.label[i]; i++) lab[i] = toupper(a.label[i]);
        lab[i] = 0;
        sprintf(meta, "%sSLOT %s", boots ? "\xE2\x96\xB6 BOOTS NEXT \xC2\xB7 " : "", lab);
        Tile &t = T(TL_APP, up, desc, meta, 1);
        t.bootsNext = boots;
        t.fam = famOf(a.label);
        strcpy(t.label, a.label);
        int k = 0;
        for (i = 0; a.name[i] && k < 2; i++)
            if (isalnum((unsigned char)a.name[i])) t.ini[k++] = toupper(a.name[i]);
        t.ini[k] = 0;
    }
    T(TL_CHG, "CHARGE", "Dim, low-power charging screen", "", 2);
    T(TL_OFF, "POWER OFF", "Turn the device off", "DEEP SLEEP \xC2\xB7 G0 WAKES IT", 2);
    return n;
}

static HomeScene home;
static void homeScene(int sel) {
    home = HomeScene();
    home.n = buildTiles();
    home.tiles = tiles;
    home.setSel(sel);
    home.version = "2.10.0";
    home.metBase = 252;
    home.shortcuts = true;
    push(home, TR_NONE);
}

static BootApp bootApps[3];
static Particle pool[1500];
static BootScene boot;
static void bootScene() {
    boot = BootScene();
    for (int i = 0; i < 3; i++) {
        strcpy(bootApps[i].name, APPS[i].name);
        strcpy(bootApps[i].label, APPS[i].label);
        bootApps[i].size = APPS[i].size;
    }
    boot.apps = bootApps;
    boot.napps = 3;
    boot.app = 0;
    boot.bootToApp = true;
    boot.version = "2.10.0";
    boot.start(pool, 1500);
    push(boot, TR_NONE);
    for (;;) {
        int r = run(boot);
        if (r != 100) break;
        static MenuItem items[4] = {{"Bruce", G_CART, RK_ITEM, "1"}, {"NEMO", G_CART, RK_ITEM, "2"}, {"Evil-Cardputer", G_CART, RK_ITEM, "3"}, {"Launcher", G_ROCKET, RK_OP, nullptr}};
        int p = sheet(items, 4, "BOOT WHICH APP?", "3 installed");
        if (p >= 0 && p < 3) boot.launch(p);
        else if (p == 3) boot.hold();
        else boot.resumeCount();
    }
    runForever();
}

// ── sample SD tree ──
struct Node {
    const char *name;
    uint32_t size;
    std::vector<Node> kids;
    bool dir;
};
static Node D(const char *n, std::vector<Node> k) { return {n, 0, k, true}; }
static Node F(const char *n, uint32_t s) { return {n, s, {}, false}; }
static Node SD_ROOT = D("", {
    D("bkp", {F("bruce_spiffs_2025-10-02.bin", 458752), F("backupData.json", 2048)}),
    D("downloads", {F("Bruce.1.10.1.bin", 2490368), F("MicroHydra.2.4.0.bin", 1835008), F("downloaded.json", 812)}),
    D("firmware", {D("nemo", {F("M5Cardputer-NEMO.bin", 1376256), F("README.md", 3100)}),
                   D("emulators", {F("cardputer-gb-0.6.bin", 1245184), F("nes-cardputer.bin", 1572864), F("chip8.bin", 524288)}),
                   F("Bruce-cardputer.bin", 2490368), F("CardputerDoom_v0.9-shareware-build-2025-08.bin", 1966080),
                   F("Evil-Cardputer-v1.4.4.bin", 1835008), F("palnagotchi.bin", 1146880), F("UiFlow2-cardputer-v2.2.0.bin", 4089446)}),
    D("music", {F("chiptune01.wav", 912000), F("loop-a.wav", 455000)}),
    F("launcher.conf", 1400),
    F(".trashes", 0),
});
static Node *find(const char *path) {
    Node *n = &SD_ROOT;
    std::string p(path);
    size_t i = 1;
    while (i < p.size()) {
        size_t j = p.find('/', i);
        std::string part = p.substr(i, j == std::string::npos ? std::string::npos : j - i);
        Node *next = nullptr;
        for (auto &k : n->kids)
            if (part == k.name) next = &k;
        if (!next) return nullptr;
        n = next;
        if (j == std::string::npos) break;
        i = j + 1;
    }
    return n;
}
struct MockFs : SdFs {
    bool list(const char *path, SDScene &sc) override {
        Node *n = find(path);
        if (!n || !n->dir) return false;
        for (auto &k : n->kids) sc.add(k.name, k.dir, k.size);
        return true;
    }
    uint32_t size(const char *path) override {
        Node *n = find(path);
        return n ? n->size : SIZE_UNKNOWN;
    }
    char boundKey(const char *path) override {
        if (!strcmp(path, "/firmware/Bruce-cardputer.bin")) return 'b';
        if (!strcmp(path, "/firmware/nemo/M5Cardputer-NEMO.bin")) return 'n';
        return 0;
    }
};
static MockFs mockFs;
static SDScene *sdp;
static void sdScene(const char *path) {
    homeScene(0);
    static SDScene sd;
    sd.~SDScene();
    new (&sd) SDScene();
    sdp = &sd;
    sd.fs = &mockFs;
    sd.open(path);
    push(sd, TR_NONE);
    for (;;) {
        int r = run(sd);
        int e = sd.selEntry();
        char sz[12], title[40];
        if (r == SD_FILE) {
            fmtSize(sz, sd.ents[e].size);
            snprintf(title, sizeof(title), "%s \xC2\xB7 %s", sd.ents[e].flags & SDF_BIN ? "FIRMWARE" : "FILE", sz);
            static MenuItem items[] = {{"Install", G_INSTALL, RK_ITEM, nullptr}, {"Bind to key", G_BIND, RK_ITEM, nullptr},
                                       {"New folder", G_NEWDIR, RK_ITEM, nullptr}, {"Rename", G_PEN, RK_ITEM, nullptr},
                                       {"Copy", G_COPY, RK_ITEM, nullptr}, {"Delete", G_TRASH, RK_DANGER, nullptr},
                                       {"Main menu", G_HOME, RK_OP, nullptr}};
            sheet(items, 7, title, sd.name(e));
        } else if (r == SD_FOLDER) {
            static MenuItem items[] = {{"New folder", G_NEWDIR, RK_ITEM, nullptr}, {"Rename", G_PEN, RK_ITEM, nullptr},
                                       {"Delete", G_TRASH, RK_DANGER, nullptr}, {"Main menu", G_HOME, RK_OP, nullptr}};
            sheet(items, 4, "FOLDER", sd.name(e));
        } else break;
    }
    runForever();
}

// Mockup's simulated install: fixed rate, stages advance on their own
struct SimJob : JobScene {
    float rate = 0x90000;
    uint32_t addr0 = 0, appSize = 0;
    uint32_t flashAddr() const override { return (addr0 + (uint32_t)jsround(overall() * appSize)) & ~0xfffu; }
    void update(float dt, bool top) override {
        if (!finished) {
            Stage &s = st[si];
            float spd = rate * (0.85f + 0.3f * sinf(t * 2.3f));
            sp += dt * spd / s.size * (s.app ? 1 : 1.6f);
            if (sp >= 1) {
                sp = 0;
                si++;
                if (si >= nst) {
                    si = nst;
                    finished = true;
                    doneT = 0;
                }
            }
            speed = spd;
        }
        JobScene::update(dt, top);
    }
};
static SimJob job;
static void installSim(bool dl, const char *name, uint32_t size, bool spiffs, bool restore, bool sd) {
    job.~SimJob();
    new (&job) SimJob();
    job.reset(dl, name);
    if (dl) {
        job.addStage("PREPARE", 0x8000);
        job.addStage("BOOTLOADER", 0x5000);
        job.addStage("PARTITIONS", 0xc00);
        job.addStage("FIRMWARE", size, 0, true);
        job.addStage("DATA", spiffs ? 0x70000 : 0x10000);
        job.addStage("SAVE TO SD", 0x8000);
        job.rate = 0x30000;
        strcpy(job.doneLine, "Saved to /downloads/");
        strcpy(job.doneSub, "ANY KEY TO RETURN");
    } else {
        job.addStage(sd ? "READ IMAGE" : "CONNECT", 0x20000);
        job.addStage("FLASH APP", size, 0, true);
        if (spiffs) job.addStage("FLASH SPIFFS", 0x70000);
        job.addStage("WRITE TABLE", 0x6000);
        job.addStage("SET BOOT", 0x4000);
        if (restore) job.addStage("RESTORE DATA", 0x70000);
        job.rate = sd ? 0x90000 : 0x30000;
        strcpy(job.doneLine, "Restarting into Bruce");
        job.doneWait = 3;
    }
    job.addr0 = 0x5F0000;
    job.appSize = size;
    push(job, TR_NONE);
    runForever();
}

// ── Wi-Fi ──
static const Net NETS[] = {{"HomeNet", -48, 3, true, true},        {"Starlink-7C2", -61, 3, true, false},
                           {"CafeGuest", -70, 0, false, false},    {"DIRECT-roku-41", -77, 3, true, false},
                           {"NETGEAR38-5G", -83, 3, true, false},  {"xfinitywifi", -88, 0, false, false}};
static WifiScene wifi;
struct SimConnect : ConnectScene {
    bool ok = true;
    void update(float dt, bool top) override {
        tries = 1 + (int)floorf(t / 0.5f);
        if (state == S_TRY && t > (ok ? 2.0f : failPassword ? 1.7f : 3.2f)) {
            state = ok ? S_LINKED : S_BROKEN;
            lt = 0;
        }
        ConnectScene::update(dt, top);
    }
};
static SimConnect conn;
static void wifiScene(bool noAuto, const char *connectTo, bool ok) {
    homeScene(1);
    wifi.~WifiScene();
    new (&wifi) WifiScene();
    memcpy(wifi.nets, NETS, sizeof(NETS));
    wifi.n = 6;
    wifi.autoNet = noAuto ? -1 : 0;
    wifi.results(2.75f);
    push(wifi, TR_NONE);
    if (connectTo) {
        conn.~SimConnect();
        new (&conn) SimConnect();
        strcpy(conn.ssid, connectTo);
        conn.ok = ok;
        strcpy(conn.sub, "IP 192.168.1.42 \xC2\xB7 GW 192.168.1.1");
        push(conn, TR_NONE);
        runForever();
    }
    for (;;) {
        int r = run(wifi);
        if (r >= 0 && wifi.nets[r].auth && !wifi.nets[r].saved) {
            char pw[64] = "";
            askText("NETWORK PASSWORD", wifi.nets[r].ssid, pw, 63, true);
        } else if (r < 0) break;
    }
    runForever();
}

// ── settings ──
static SettingsScene settingsSc;
static void setItem(SettingsScene &sc, const char *label, uint8_t icon, uint8_t kind, bool on = false, const char *val = "", bool adj = false, float bar = 0) {
    SetItem &it = sc.items[sc.n++];
    it.label = label;
    it.icon = icon;
    it.kind = kind;
    it.on = on;
    it.adj = adj;
    it.bar = bar;
    strcpy(it.val, val);
}
static void settingsScene() {
    homeScene(5);
    settingsSc.~SettingsScene();
    new (&settingsSc) SettingsScene();
    SettingsScene &sc = settingsSc;
    setItem(sc, "DISPLAY", 255, SK_SECTION);
    setItem(sc, "UI color", G_EYE, SK_THEME, false, "NOVA");
    setItem(sc, "Orientation", G_ROTATE, SK_VALUE, false, "LANDSCAPE");
    setItem(sc, "Brightness", G_BRIGHT, SK_BAR, false, "80%", true, 0.8f);
    setItem(sc, "Dim after", G_TIMER, SK_VALUE, false, "20S", true);
    setItem(sc, "FILES", 255, SK_SECTION);
    setItem(sc, "See all files", G_FILE, SK_TOGGLE, false);
    setItem(sc, "Show dotfiles", G_EYE, SK_TOGGLE, false);
    setItem(sc, "Auto backup", G_BKP, SK_TOGGLE, true);
    setItem(sc, "Ask to copy SPIFFS", G_CHIP, SK_TOGGLE, true);
    setItem(sc, "NETWORK", 255, SK_SECTION);
    setItem(sc, "Auto-connect known Wi-Fi", G_WIFI, SK_TOGGLE, true);
    setItem(sc, "BOOT", 255, SK_SECTION);
    setItem(sc, "Boot to Launcher", G_ROCKET, SK_TOGGLE, false);
    setItem(sc, "Boot timer", G_TIMER, SK_VALUE, false, "5S", true);
    setItem(sc, "Deep sleep opens Launcher", G_POWER, SK_TOGGLE, true);
    setItem(sc, "Keyboard shortcuts", G_KEY, SK_VALUE, false, "2 KEYS \xE2\x80\xBA");
    setItem(sc, "POWER", 255, SK_SECTION);
    setItem(sc, "Restart", G_RESET, SK_ACTION, false, "\xE2\x80\xBA");
    setItem(sc, "Turn off", G_POWER, SK_ACTION, false, "\xE2\x80\xBA");
    setItem(sc, "SYSTEM", 255, SK_SECTION);
    setItem(sc, "Reset configs & Wi-Fi", G_WARN, SK_DANGER);
    setItem(sc, "Main menu", G_HOME, SK_OP);
    sc.version = "2.10.0";
    sc.begin(1);
    push(sc, TR_NONE);
}

// ── partitions ──
static PmanScene pman;
static void pmanScene() {
    homeScene(4);
    pman.~PmanScene();
    new (&pman) PmanScene();
    auto R = [](const char *label, const char *type, uint32_t ofs, uint32_t size, uint8_t kind, uint8_t fam, bool prot) {
        PartRow &r = pman.rows[pman.nrows];
        strcpy(r.label, label);
        char sz[12];
        fmtSize(sz, size);
        if (kind == PK_FREE) strcpy(r.right, sz);
        else snprintf(r.right, sizeof(r.right), "%s \xC2\xB7 %s", type, sz);
        r.ofs = ofs;
        r.size = size;
        r.kind = kind;
        r.fam = fam;
        r.prot = prot;
        r.opt = pman.nrows++;
    };
    R("launcher", "APP/TEST", 0x10000, 0x150000, PK_LAUNCHER, 0, true);
    R("coredump", "DATA/COREDUMP", 0x160000, 0x10000, PK_SYS, 0, true);
    R("bruce", "APP/OTA_0", 0x170000, 0x250000, PK_APP, 0, false);
    R("spiffs", "DATA/SPIFFS", 0x3C0000, 0x70000, PK_OWNED, 0, false);
    R("nemo", "APP/OTA_1", 0x430000, 0x100000, PK_APP, 1, false);
    R("evilc", "APP/OTA_2", 0x530000, 0x120000, PK_APP, 2, false);
    R("FREE", "", 0x650000, 0x1B0000, PK_FREE, 0, false);
    pman.ops[pman.nops++] = {"Erase BLE bonds", G_BLE, RK_OP, 100};
    pman.ops[pman.nops++] = {"Back", G_BACK, RK_OP, 101};
    pman.build(0);
    push(pman, TR_NONE);
}

void addShots() {
    SHOTS.push_back({"S43", 1.0f, downs(0.2f, 2), [] { baseStatus(); settingsScene(); runForever(); }});
    SHOTS.push_back({"S44", 1.6f, downs(0.2f, 9, 0.08f), [] { baseStatus(); settingsScene(); runForever(); }});
    SHOTS.push_back({"S45", 0.8f, K(0.2f, {{K_LEFT, 0}, {K_LEFT, 0}, {K_LEFT, 0}}), [] { baseStatus(); settingsScene(); static BrightnessScene b; b = BrightnessScene(); b.val = 80; push(b, TR_NONE); runForever(); }});
    SHOTS.push_back({"S46", 0.6f, {}, [] { baseStatus(); settingsScene(); static ColorScene c; c = ColorScene(); push(c, TR_NONE); runForever(); }});
    SHOTS.push_back({"S47", 2.0f, {}, [] { baseStatus(); ST.charging = true; homeScene(9); static ChargeScene c; c = ChargeScene(); c.volts = 4.02f; push(c, TR_NONE); runForever(); }});
    SHOTS.push_back({"S39", 1.0f, downs(0.2f, 2), [] { baseStatus(); pmanScene(); runForever(); }});
    SHOTS.push_back({"S41", 1.0f, K(0.2f, {{K_RIGHT, 0}, {K_RIGHT, 0}, {K_RIGHT, 0}}), [] { baseStatus(); pmanScene(); static RangeScene r; r = RangeScene(); strcpy(r.label, "evilc"); r.minO = 0x530000; r.maxO = 0x800000; r.start = 0x530000; r.end = 0x650000; r.fam = 2; push(r, TR_NONE); runForever(); }});
    SHOTS.push_back({"S52", 2.1f, cat(cat(downs(0.2f, 5), {{0.8f, K_OK, 0}}), cat(downs(1.2f, 5, 0.08f), {{1.7f, K_OK, 0}})), [] {
        baseStatus(); homeScene(0);
        static SDScene sd; sd.~SDScene(); new (&sd) SDScene(); sd.fs = &mockFs; sd.open("/firmware"); push(sd, TR_NONE);
        for (;;) { int r = run(sd); if (r == SD_FILE) { static MenuItem it[] = {{"Install", G_INSTALL, RK_ITEM, nullptr}, {"Bind to key", G_BIND, RK_ITEM, nullptr}, {"New folder", G_NEWDIR, RK_ITEM, nullptr}, {"Rename", G_PEN, RK_ITEM, nullptr}, {"Copy", G_COPY, RK_ITEM, nullptr}, {"Delete", G_TRASH, RK_DANGER, nullptr}, {"Main menu", G_HOME, RK_OP, nullptr}};
            char title[40], sz[12]; fmtSize(sz, sd.ents[sd.selEntry()].size); snprintf(title, sizeof(title), "FIRMWARE \xC2\xB7 %s", sz);
            int p = sheet(it, 7, title, sd.name(sd.selEntry()));
            if (p == 5) { char msg[96], det[96]; snprintf(msg, sizeof(msg), "Delete %s? This can't be undone.", sd.name(sd.selEntry())); snprintf(det, sizeof(det), "%s \xC2\xB7 /firmware/%s", sz, sd.name(sd.selEntry())); confirm("DELETE", msg, "DELETE", true, det); } } }
    }});
    SHOTS.push_back({"S53", 0.8f, {}, [] { baseStatus(); homeScene(0); static SDScene sd; sd.~SDScene(); new (&sd) SDScene(); sd.fs = &mockFs; sd.open("/firmware"); push(sd, TR_NONE); char b[80] = "palnagotchi-1.2.1.bin"; askText("RENAME", "palnagotchi.bin", b, 76); }});
    SHOTS.push_back({"S55", 0.22f, {}, [] { baseStatus(); homeScene(10); static PowerOffScene p; p = PowerOffScene(); static uint16_t snap[W * H]; push(p, TR_NONE); snapshotBelow(snap); p.snap = snap; runForever(); }});
    SHOTS.push_back({"S21", 1.5f, {}, [] { baseStatus(); wifiScene(true, nullptr, true); }});
    SHOTS.push_back({"S22", 3.6f, {{3.0f, K_DOWN, 0}}, [] { baseStatus(); wifiScene(true, nullptr, true); }});
    SHOTS.push_back({"S23", 3.2f, {}, [] { baseStatus(); wifiScene(false, nullptr, true); }});
    SHOTS.push_back({"S24", 4.3f, cat({{3.0f, K_DOWN, 0}, {3.1f, K_OK, 0}}, K(3.4f, {{K_CHAR, 'h'}, {K_CHAR, 'u'}, {K_CHAR, 'n'}, {K_CHAR, 't'}, {K_CHAR, 'e'}, {K_CHAR, 'r'}, {K_CHAR, '2'}}, 0.1f)), [] { baseStatus(); wifiScene(true, nullptr, true); }});
    SHOTS.push_back({"S25", 1.0f, {}, [] { baseStatus(); wifiScene(true, "Starlink-7C2", true); }});
    SHOTS.push_back({"S26", 1.95f, {}, [] { baseStatus(); wifiScene(true, "HomeNet", false); }});
    SHOTS.push_back({"S18", 1.7f, {}, [] { baseStatus(); homeScene(0); installSim(false, "Bruce-cardputer.bin", 2490368, true, true, true); }});
    SHOTS.push_back({"S19", 7.4f, {}, [] { baseStatus(); homeScene(0); installSim(false, "Bruce-cardputer.bin", 2490368, true, false, true); }});
    SHOTS.push_back({"S33", 2.2f, {}, [] { baseStatus(); ST.wifi = true; homeScene(1); installSim(true, "Bruce.1.10.2.bin", 0x100000 + (fnv("bruce1.10.2") % 0x200000), true, false, false); }});
    SHOTS.push_back({"S09", 1.0f, {}, [] { baseStatus(); sdScene("/"); }});
    SHOTS.push_back({"S10", 3.4f, downs(0.2f, 3), [] { baseStatus(); sdScene("/firmware"); }});
    SHOTS.push_back({"S11", 0.9f, cat(downs(0.2f, 2), {{0.5f, K_OK, 0}}), [] { baseStatus(); sdScene("/firmware"); }});
    SHOTS.push_back({"S12", 0.9f, cat(downs(0.2f, 2), {{0.5f, K_HOLD, 0}}), [] { baseStatus(); sdScene("/"); }});
    SHOTS.push_back({"S13", 1.0f, {}, [] { baseStatus(); sdScene("/music"); }});
    SHOTS.push_back({"S01", 1.7f, {}, [] { baseStatus(); bootScene(); }});
    SHOTS.push_back({"S02", 0.9f, K(0.4f, {{K_CHAR, 'x'}}), [] { baseStatus(); bootScene(); }});
    SHOTS.push_back({"S03", 6.25f, {}, [] { baseStatus(); bootScene(); }});
    SHOTS.push_back({"S05", 2.0f, {}, [] { baseStatus(); homeScene(1); runForever(); }});
    SHOTS.push_back({"S06", 2.0f, {}, [] { baseStatus(); homeScene(6); runForever(); }});
    SHOTS.push_back({"S07", 2.0f, {}, [] { baseStatus(); ST.sd = false; homeScene(0); runForever(); }});
    SHOTS.push_back({"S56", 2.0f, {}, [] { baseStatus(); ST.charging = true; homeScene(9); runForever(); }});
}

// ── gallery: screens for the README, rendered by the firmware's own scenes ──
// (no mockup reference; same sample data as the mockup)
struct CatRow {
    const char *fid, *name, *author, *ver;
    bool star;
};
static const CatRow CAT[] = {{"bruce", "Bruce", "pr3y", "1.10.2", true},
                             {"evil", "Evil-Cardputer", "7h30th3r0n3", "1.4.4", true},
                             {"nemo", "M5Cardputer-NEMO", "n0xa", "2.7.1", false},
                             {"hydra", "MicroHydra", "echo-lalia", "2.5.1", true},
                             {"uiflow2", "UiFlow2", "M5Stack", "2.2.0", false},
                             {"palna", "Palnagotchi", "viniciusbo", "1.2.1", false},
                             {"marauder", "ESP32 Marauder", "justcallmekoko", "1.8.6", false},
                             {"doom", "Cardputer Doom", "sample-dev", "0.9.0", false}};
struct SimCat : ListSrc {
    static constexpr int N = sizeof(CAT) / sizeof(CAT[0]);
    char subs[N][32];
    int count() override { return N + 3; }
    uint8_t kind(int i) override { return i >= 1 && i <= N ? RK_ITEM : RK_OP; }
    void row(int i, Row &r) override {
        if (i == 0) {
            r.label = "Search, sort, filter";
            r.icon = G_SEARCH;
            strcpy(r.right, "TOP");
            r.sub = "TEXT SEARCH \xC2\xB7 ORDER \xC2\xB7 STARRED ONLY";
            r.kind = RK_OP;
            return;
        }
        int k = i - 1;
        if (k < N) {
            const CatRow &c = CAT[k];
            r.label = c.name;
            r.color = c.star ? C(T_hi) : C(T_text);
            snprintf(r.right, sizeof(r.right), "%s%s", c.star ? "\xE2\x98\x85 " : "", c.ver);
            r.rightC = c.star ? C(T_acc) : C(T_dim);
            snprintf(subs[k], sizeof(subs[k]), "BY %s", c.author);
            for (char *p = subs[k]; *p; p++) *p = toupper((unsigned char)*p);
            r.sub = subs[k];
            return;
        }
        r.kind = RK_OP;
        if (k == N) {
            r.label = "Next page \xE2\x80\xBA";
            strcpy(r.right, "P2");
        } else {
            r.label = "Main menu";
            r.icon = G_HOME;
        }
    }
    bool icon(int i, Gfx &g, int x, int y, bool sel) override {
        int k = i - 1;
        if (k < 0 || k >= N) return false;
        drawEmblem(g, CAT[k].fid, x, y + 2, 1, sel ? C(T_onAcc) : famC(famOf(CAT[k].fid), 0), sel ? C(T_accD) : C(T_bg3));
        return true;
    }
};
static SimCat simCat;
static ListScene catScene;
static void catalogScene(int sel) {
    homeScene(1);
    catScene.~ListScene();
    new (&catScene) ListScene();
    catScene.hdr.icon = G_SAT;
    catScene.hdr.title = "LAUNCHERHUB";
    catScene.hdr.crumb = "CARDPUTER";
    catScene.hdr.right = "P1/3";
    strcpy(catScene.right, "22 FIRMWARE");
    catScene.hk = "\xE2\x8F\x8E|VIEW|\xE2\x97\x80\xE2\x96\xB6|PAGE|`|BACK";
    catScene.pages = true;
    catScene.setup(&simCat, sel, 8);
    push(catScene, TR_NONE);
}
static OtaMenuScene hub;
static FirmwareScene fw;
static FwVersion fwVers[6] = {{"1.10.2", "2026-09-28"}, {"1.10.1", "2026-08-30"}, {"1.10", "2026-08-02"},
                              {"1.9.1", "2026-06-11"},  {"1.9", "2026-05-19"},    {"1.8.2", "2026-03-07"}};
static WebModeScene webMode;
struct SimWeb : WebRunScene {
    float nextReq = 0.6f;
    int k = 0;
    void update(float dt, bool top) override {
        static const char *const REQ[] = {"GET /", "GET /scripts.js", "GET /listfiles?folder=/", "POST /upload",
                                          "GET /listfiles?folder=/firmware", "GET /systeminfo"};
        if (t > nextReq) {
            logRequest(REQ[k++ % 6]);
            nextReq += 0.55f;
        }
        WebRunScene::update(dt, top);
    }
};
static SimWeb webRun;
struct SimUsb : UsbScene {
    void update(float dt, bool top) override {
        if (t > 0.8f) state = S_MOUNTED;
        if (state == S_MOUNTED) {
            if (fmodf(t, 1.6f) < 1.0f) rd += (uint32_t)(dt * 2400000);
            else wr += (uint32_t)(dt * 900000);
        }
        UsbScene::update(dt, top);
    }
};
static SimUsb usb;
static AppScene appSc;
static const MenuItem APP_ITEMS[] = {{"Launch App", G_PLAY, RK_ITEM, nullptr},       {"Rename App", G_PEN, RK_ITEM, nullptr},
                                     {"Backup Data", G_BKP, RK_ITEM, nullptr},       {"Restore Last Data", G_RESTORE, RK_ITEM, nullptr},
                                     {"Delete Bruce", G_TRASH, RK_DANGER, nullptr}, {"Cancel", G_BACK, RK_OP, nullptr}};
static void appScene() {
    homeScene(6);
    appSc.~AppScene();
    new (&appSc) AppScene();
    strcpy(appSc.name, "Bruce");
    strcpy(appSc.label, "bruce");
    strcpy(appSc.ini, "BR");
    appSc.fam = famOf("bruce");
    appSc.bootsNext = true;
    static const char *K4[4] = {"APP", "SPIFFS", "BACKUP", "SOURCE"}, *V4[4] = {"2.3 MB", "448 KB", "SAVED", "BRUCE-CARDPUTER.BIN"};
    for (int i = 0; i < 4; i++) appSc.keys[i] = K4[i], appSc.vals[i] = V4[i];
    appSc.nrows = 4;
    appSc.menu.items = APP_ITEMS;
    appSc.menu.n = 6;
    appSc.begin();
    push(appSc, TR_NONE);
}
struct SimArc : ArcScene {
    void update(float dt, bool) override { p = clampf(t / 2.2f, 0, 1); }
};
static SimArc arcSc;
struct SimSteps : StepsScene {
    float each = 0.55f;
    int shown = 0;
    void update(float dt, bool) override {
        static const char *const S[] = {"CLEARING BOOT", "REMOVING FIRMWARE", "OPTIMIZING FLASH", "WRITING TABLE"};
        while (shown < 4 && t >= shown * each) add(S[shown++]);
        frac = fmodf(t, each) / each;
    }
};
static SimSteps stepsSc;

void addGallery() {
    // stills
    SHOTS.push_back({"boot", 1.7f, {}, [] { baseStatus(); bootScene(); }});
    SHOTS.push_back({"launch", 5.6f, {}, [] { baseStatus(); bootScene(); }});
    SHOTS.push_back({"home", 2.0f, {}, [] { baseStatus(); homeScene(0); runForever(); }});
    SHOTS.push_back({"home-app", 2.0f, {}, [] { baseStatus(); homeScene(6); runForever(); }});
    SHOTS.push_back({"home-charge", 2.0f, {}, [] { baseStatus(); ST.charging = true; homeScene(9); runForever(); }});
    SHOTS.push_back({"sd", 3.4f, downs(0.2f, 3), [] { baseStatus(); sdScene("/firmware"); }});
    SHOTS.push_back({"sd-actions", 0.9f, cat(downs(0.2f, 2), {{0.5f, K_OK, 0}}), [] { baseStatus(); sdScene("/firmware"); }});
    SHOTS.push_back({"install", 3.6f, {}, [] { baseStatus(); homeScene(0); installSim(false, "Bruce-cardputer.bin", 2490368, true, true, true); }});
    SHOTS.push_back({"install-done", 7.4f, {}, [] { baseStatus(); homeScene(0); installSim(false, "Bruce-cardputer.bin", 2490368, true, false, true); }});
    SHOTS.push_back({"download", 2.2f, {}, [] { baseStatus(); ST.wifi = true; homeScene(1); installSim(true, "Bruce.1.10.2.bin", 0x1A4000, true, false, false); }});
    SHOTS.push_back({"wifi-scan", 1.5f, {}, [] { baseStatus(); wifiScene(true, nullptr, true); }});
    SHOTS.push_back({"wifi-list", 3.6f, {{3.0f, K_DOWN, 0}}, [] { baseStatus(); wifiScene(true, nullptr, true); }});
    SHOTS.push_back({"wifi-password", 4.3f, cat({{3.0f, K_DOWN, 0}, {3.1f, K_OK, 0}}, K(3.4f, {{K_CHAR, 'h'}, {K_CHAR, 'u'}, {K_CHAR, 'n'}, {K_CHAR, 't'}, {K_CHAR, 'e'}, {K_CHAR, 'r'}, {K_CHAR, '2'}}, 0.1f)), [] { baseStatus(); wifiScene(true, nullptr, true); }});
    SHOTS.push_back({"wifi-connect", 1.95f, {}, [] { baseStatus(); wifiScene(true, "HomeNet", true); }});
    SHOTS.push_back({"online-hub", 1.0f, {}, [] {
        baseStatus(); ST.wifi = true; homeScene(1);
        hub = OtaMenuScene();
        hub.crumb = "HOMENET";
        hub.tiles[0] = {0, G_SAT, "CATALOG", "ON LAUNCHERHUB", "CARDPUTER", "22", 0};
        hub.tiles[1] = {1, G_STAR, "FAVORITES", "SAVED BY YOU", "BRUCE \xC2\xB7 EVIL-CARDPUTER", "2", 0};
        hub.tiles[2] = {2, G_DL, "UPDATES", "DOWNLOADED", "CHECK ONLINE", "3", 0};
        hub.n = 3;
        push(hub, TR_NONE); runForever(); }});
    SHOTS.push_back({"catalog", 1.2f, {}, [] { baseStatus(); ST.wifi = true; catalogScene(1); runForever(); }});
    SHOTS.push_back({"firmware", 1.0f, {}, [] {
        baseStatus(); ST.wifi = true; catalogScene(1);
        fw = FirmwareScene();
        strcpy(fw.name, "Bruce"); strcpy(fw.author, "pr3y"); strcpy(fw.fid, "bruce");
        strcpy(fw.file, "Bruce-cardputer.bin"); strcpy(fw.onDevice, "SLOT BRUCE");
        fw.star = true; fw.vers = fwVers; fw.nv = 6;
        push(fw, TR_NONE); runForever(); }});
    SHOTS.push_back({"webui-mode", 1.0f, {}, [] {
        baseStatus(); ST.wifi = true; homeScene(2);
        webMode = WebModeScene(); strcpy(webMode.ssid, "HomeNet");
        push(webMode, TR_NONE); runForever(); }});
    SHOTS.push_back({"webui", 4.0f, {}, [] {
        baseStatus(); homeScene(2);
        webRun.~SimWeb(); new (&webRun) SimWeb();
        webRun.ap = true; strcpy(webRun.ip, "172.0.0.1"); strcpy(webRun.user, "admin"); strcpy(webRun.pass, "launcher");
        webRun.begin();
        push(webRun, TR_NONE); runForever(); }});
    SHOTS.push_back({"usb", 3.5f, {}, [] {
        baseStatus(); homeScene(3);
        usb.~SimUsb(); new (&usb) SimUsb();
        strcpy(usb.card, "SDHC 16 GB \xC2\xB7 FAT32 \xC2\xB7 11.2 GB FREE");
        push(usb, TR_NONE); runForever(); }});
    SHOTS.push_back({"partitions", 1.0f, downs(0.2f, 2), [] { baseStatus(); pmanScene(); runForever(); }});
    SHOTS.push_back({"partition-resize", 1.0f, K(0.2f, {{K_RIGHT, 0}, {K_RIGHT, 0}, {K_RIGHT, 0}}), [] { baseStatus(); pmanScene(); static RangeScene r; r = RangeScene(); strcpy(r.label, "evilc"); r.minO = 0x530000; r.maxO = 0x800000; r.start = 0x530000; r.end = 0x650000; r.fam = 2; push(r, TR_NONE); runForever(); }});
    SHOTS.push_back({"app", 1.0f, {}, [] { baseStatus(); appScene(); runForever(); }});
    SHOTS.push_back({"app-delete", 1.3f, {}, [] {
        baseStatus(); appScene();
        stepsSc.~SimSteps(); new (&stepsSc) SimSteps(); strcpy(stepsSc.title, "DELETE BRUCE");
        push(stepsSc, TR_NONE); runForever(); }});
    SHOTS.push_back({"copy", 1.0f, {}, [] {
        baseStatus(); homeScene(0);
        static SDScene sd; sd.~SDScene(); new (&sd) SDScene(); sd.fs = &mockFs; sd.open("/firmware"); push(sd, TR_NONE);
        arcSc = SimArc(); arcSc.title = "COPYING"; arcSc.sub = "Bruce-cardputer.bin";
        push(arcSc, TR_NONE); runForever(); }});
    SHOTS.push_back({"error", 0.9f, {}, [] {
        baseStatus(); homeScene(0);
        static SDScene sd; sd.~SDScene(); new (&sd) SDScene(); sd.fs = &mockFs; sd.open("/firmware"); push(sd, TR_NONE);
        static const char *const B[1] = {"OK"};
        DialogOpts o; o.kind = DK_ERROR; o.title = "NO INSTALL SPACE";
        o.msg = "UiFlow2 needs 3.9 MB, the largest free range is 2.1 MB.";
        o.detail = "Delete an app or free space in Partitions"; o.buttons = B; o.nbtn = 1;
        dialog(o); runForever(); }});
    SHOTS.push_back({"settings", 1.0f, downs(0.2f, 2), [] { baseStatus(); settingsScene(); runForever(); }});
    SHOTS.push_back({"themes", 0.6f, {}, [] { baseStatus(); settingsScene(); static ColorScene c; c = ColorScene(); push(c, TR_NONE); runForever(); }});
    SHOTS.push_back({"charge", 2.0f, {}, [] { baseStatus(); ST.charging = true; homeScene(9); static ChargeScene c; c = ChargeScene(); c.volts = 4.02f; push(c, TR_NONE); runForever(); }});
    SHOTS.push_back({"rename", 0.8f, {}, [] { baseStatus(); homeScene(0); static SDScene sd; sd.~SDScene(); new (&sd) SDScene(); sd.fs = &mockFs; sd.open("/firmware"); push(sd, TR_NONE); char b[80] = "palnagotchi-1.2.1.bin"; askText("RENAME", "palnagotchi.bin", b, 76); }});
    SHOTS.push_back({"poweroff", 0.22f, {}, [] { baseStatus(); homeScene(10); static PowerOffScene p; p = PowerOffScene(); static uint16_t snap[W * H]; push(p, TR_NONE); snapshotBelow(snap); p.snap = snap; runForever(); }});
    // home in each theme
    for (int th = 0; th < THEME_COUNT; th++) {
        static char ids[THEME_COUNT][16];
        snprintf(ids[th], sizeof(ids[th]), "theme-%d", th);
        SHOTS.push_back({ids[th], 2.0f, {}, [th] { baseStatus(); setTheme(th); homeScene(0); runForever(); }});
    }
    // films (captured every few frames with --film)
    SHOTS.push_back({"film-home", 6.0f, K(0.6f, {{K_RIGHT, 0}, {K_RIGHT, 0}, {K_RIGHT, 0}, {K_RIGHT, 0}, {K_RIGHT, 0}, {K_RIGHT, 0}, {K_RIGHT, 0}, {K_DOWN, 0}}, 0.62f), [] { baseStatus(); homeScene(0); runForever(); }});
    SHOTS.push_back({"film-launch", 6.25f, {}, [] { baseStatus(); bootScene(); }});
    SHOTS.push_back({"film-install", 7.4f, {}, [] { baseStatus(); homeScene(0); installSim(false, "Bruce-cardputer.bin", 2490368, true, false, true); }});
    SHOTS.push_back({"film-wifi", 4.4f, {{3.0f, K_DOWN, 0}, {3.4f, K_DOWN, 0}}, [] { baseStatus(); wifiScene(true, nullptr, true); }});
}
