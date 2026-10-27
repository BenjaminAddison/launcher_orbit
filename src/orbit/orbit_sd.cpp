#if defined(ORBIT_UI) || defined(ORBIT_NATIVE)
#include "orbit_screens.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

// SD — file browser. Folders first, .bin only unless "See all files" is on.
// Enter on a folder opens it, on a file asks for its actions; hold Enter on a
// folder or on ".." for folder actions (new folder, paste, rename, delete).
namespace orb {

SDScene::~SDScene() {
    free(ents);
    free(names);
}

bool SDScene::add(const char *nm, bool dir, uint32_t size) {
    if (!showDot && nm[0] == '.') return true;
    int l = strlen(nm);
    bool bin = l > 4 && !strcasecmp(nm + l - 4, ".bin");
    if (!dir && onlyBins && !bin) return true;
    if (nents >= 256) return false;
    if (nents >= cap) {
        int nc = cap ? cap * 2 : 32;
        SdEntry *ne = (SdEntry *)realloc(ents, nc * sizeof(SdEntry));
        if (!ne) return false;
        ents = ne;
        cap = nc;
    }
    if (namesLen + l + 1 > namesCap) {
        int nc = namesCap ? namesCap * 2 : 1024;
        while (nc < namesLen + l + 1) nc *= 2;
        if (nc > 65535) return false;
        char *nn = (char *)realloc(names, nc);
        if (!nn) return false;
        names = nn;
        namesCap = nc;
    }
    SdEntry &e = ents[nents++];
    e.name = namesLen;
    memcpy(names + namesLen, nm, l + 1);
    namesLen += l + 1;
    e.flags = (dir ? SDF_DIR : 0) | (bin ? SDF_BIN : 0);
    e.size = dir ? 0 : size;
    char full[200];
    join(full, sizeof(full), nm);
    e.key = dir ? 0 : fs->boundKey(full);
    return true;
}

void SDScene::join(char *out, int outLen, const char *nm) const {
    snprintf(out, outLen, "%s/%s", strcmp(path, "/") ? path : "", nm);
}

static SDScene *sortScene;
static int cmpEnt(const void *a, const void *b) {
    const SdEntry *x = (const SdEntry *)a, *y = (const SdEntry *)b;
    int dx = x->flags & SDF_DIR, dy = y->flags & SDF_DIR;
    if (dx != dy) return dy - dx;
    return strcasecmp(sortScene->names + x->name, sortScene->names + y->name);
}

void SDScene::open(const char *p, const char *selName) {
    int prev = list.src ? list.sel : 0;
    if (p != path) {
        strncpy(path, p, sizeof(path) - 1);
        path[sizeof(path) - 1] = 0;
    }
    nents = 0;
    namesLen = 0;
    readFailed = !fs->list(path, *this);
    sortScene = this;
    if (nents > 1) qsort(ents, nents, sizeof(SdEntry), cmpEnt);
    list.y = HDR_H + 1;
    list.h = FOOT_Y - HDR_H - 2;
    list.init(this, 0);
    int sel = 0;
    if (selName) {
        for (int i = 0; i < nents; i++)
            if (!strcmp(name(i), selName)) sel = i + 1;
    } else if (nents) {
        sel = prev < nents ? prev : nents;
        if (!sel) sel = 1;
    }
    list.sel = sel;
    list.snap();
}

void SDScene::reload() {
    char keep[96] = "";
    int e = selEntry();
    if (e >= 0) {
        strncpy(keep, name(e), sizeof(keep) - 1);
        keep[sizeof(keep) - 1] = 0;
    }
    int was = list.sel;
    open(path, keep[0] ? keep : nullptr);
    if (keep[0] && list.sel == 0 && nents) { // the entry is gone: stay near where it was
        list.sel = was <= nents ? was : nents;
        list.snap();
    }
}

void SDScene::up() {
    if (!strcmp(path, "/")) {
        res = SD_EXIT;
        return;
    }
    char from[96];
    const char *slash = strrchr(path, '/');
    strncpy(from, slash + 1, sizeof(from) - 1);
    from[sizeof(from) - 1] = 0;
    char np[160];
    int l = slash - path;
    memcpy(np, path, l);
    np[l] = 0;
    if (!l) strcpy(np, "/");
    enterAnim = -1;
    at = 0;
    open(np, from);
}

void SDScene::row(int i, Row &r) {
    if (i == 0) {
        r.label = "..";
        r.icon = G_UP;
        r.kind = RK_OP;
        strcpy(r.right, strcmp(path, "/") ? "UP ONE LEVEL" : "MAIN MENU");
        return;
    }
    const SdEntry &e = ents[i - 1];
    r.label = name(i - 1);
    if (e.flags & SDF_DIR) {
        r.icon = G_FOLDER;
        r.iconC = C(T_sec);
        r.color = C(T_hi);
        strcpy(r.right, "\xE2\x80\xBA");
        r.rightC = C(T_sec);
        return;
    }
    bool bin = e.flags & SDF_BIN;
    r.icon = bin ? G_BIN : G_FILE;
    r.iconC = bin ? C(T_acc) : C(T_dim);
    r.color = bin ? C(T_text) : C(T_dim);
    char sz[12];
    if (e.size == SIZE_UNKNOWN) strcpy(sz, "\xC2\xB7\xC2\xB7\xC2\xB7");
    else fmtSize(sz, e.size);
    if (e.key)
        snprintf(r.right, sizeof(r.right), "'%c' %s", e.key >= 'a' && e.key <= 'z' ? e.key - 32 : e.key, sz);
    else strcpy(r.right, sz);
    r.rightC = C(T_dim);
}

void SDScene::key(uint8_t k, char) {
    if (list.key(k)) return;
    int e = selEntry();
    if (k == K_BACK || k == K_LEFT) return up();
    if (k == K_OK || k == K_RIGHT) {
        if (e < 0) return up();
        if (ents[e].flags & SDF_DIR) {
            char np[160];
            join(np, sizeof(np), name(e));
            enterAnim = 1;
            at = 0;
            open(np);
            list.sel = nents ? 1 : 0;
            list.snap();
            return;
        }
        if (k == K_OK) res = picker ? SD_PICK : SD_FILE;
    }
    if (k == K_HOLD) res = (e < 0 || (ents[e].flags & SDF_DIR)) ? SD_FOLDER : SD_FILE;
}

void SDScene::update(float dt, bool) {
    list.update(dt);
    at += dt;
    if (enterAnim && at >= 0.18f) enterAnim = 0;
    // fill in sizes for the rows on screen, a few per frame
    int done = 0, y = 0;
    for (int i = 0; i < nents + 1 && done < 3; i++) {
        int rh = list.rowHeight(i);
        if (i > 0 && y + rh >= list.scroll && y < list.scroll + list.h && ents[i - 1].size == SIZE_UNKNOWN &&
            !(ents[i - 1].flags & SDF_DIR)) {
            char full[200];
            join(full, sizeof(full), name(i - 1));
            ents[i - 1].size = fs->size(full);
            if (ents[i - 1].size == SIZE_UNKNOWN) ents[i - 1].size = 0;
            done++;
        }
        y += rh;
    }
}

void SDScene::draw(Gfx &g) {
    g.clear(C(T_bg));
    char crumb[200];
    if (!strcmp(path, "/")) strcpy(crumb, "ROOT");
    else {
        int o = 0;
        for (const char *p = path + 1; *p && o < 190; p++) {
            if (*p == '/') {
                memcpy(crumb + o, " / ", 3);
                o += 3;
            } else crumb[o++] = *p;
        }
        crumb[o] = 0;
    }
    Hdr h;
    h.icon = G_SD;
    h.title = "SD CARD";
    h.crumb = crumb;
    h.crumbMax = 110;
    header(g, h);
    float k = enterAnim ? easeOut(clampf(at / 0.18f, 0, 1)) : 1;
    int off = enterAnim ? jsround((1 - k) * 18 * enterAnim) : 0;
    list.x = off;
    list.draw(g);
    list.x = 0;
    if (off) g.fill(off > 0 ? 0 : W + off, HDR_H, off > 0 ? off : -off, FOOT_Y - HDR_H, C(T_bg));
    if (!nents) {
        const int cy = 76;
        blit(g, ICO_SD, 104, cy - 26, PM_DIM);
        text(
            g,
            F_BODY,
            readFailed ? "Can't read this folder"
            : onlyBins ? "No .bin files here"
                       : "This folder is empty",
            120,
            cy + 10,
            C(T_text),
            TA(AL_C)
        );
        text(
            g,
            F_MICRO,
            onlyBins ? "SETTINGS \xE2\x80\xBA SEE ALL FILES SHOWS THE REST"
                     : "HOLD \xE2\x8F\x8E ON .. FOR NEW FOLDER OR PASTE",
            120,
            cy + 22,
            C(T_dim),
            TA(AL_C)
        );
    }
    char info[64];
    int e = selEntry();
    const char *hk;
    if (e < 0) {
        if (clip[0]) {
            int o = snprintf(info, sizeof(info), "CLIPBOARD ");
            for (int i = 0; clip[i] && o < 62; i++)
                info[o++] = clip[i] >= 'a' && clip[i] <= 'z' ? clip[i] - 32 : clip[i];
            info[o] = 0;
        } else sprintf(info, "%d ITEMS", nents);
        hk = strcmp(path, "/") ? "\xE2\x8F\x8E|UP|HOLD|MENU|`|BACK" : "\xE2\x8F\x8E|EXIT|HOLD|MENU|`|BACK";
    } else if (ents[e].flags & SDF_DIR) {
        strcpy(info, "FOLDER");
        hk = "\xE2\x8F\x8E|OPEN|HOLD|MENU|`|BACK";
    } else {
        char sz[12];
        if (ents[e].size == SIZE_UNKNOWN) strcpy(sz, "");
        else fmtSize(sz, ents[e].size);
        snprintf(info, sizeof(info), "%s%s", ents[e].flags & SDF_BIN ? "FIRMWARE " : "FILE ", sz);
        hk = picker ? "\xE2\x8F\x8E|CHOOSE|`|BACK" : "\xE2\x8F\x8E|ACTIONS|`|BACK";
    }
    hints(g, hk, info, C(T_line2));
}

} // namespace orb
#endif
