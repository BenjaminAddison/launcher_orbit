// Orbit UI — what the Launcher calls. The generic helpers (loopOptions,
// displayRedStripe, displayError/Msg, progressHandler, keyboard) keep their
// signatures in display.h / mykeyboard.h and are implemented by the bridge when
// ORBIT_UI is set; these are the extra entry points.
#pragma once
#if defined(ORBIT_UI)
#include "orbit_screens.h"
#include <WString.h>
#include <stdint.h>

void orbPanelBegin(); // in place of tft->begin()
void orbSetRotation(uint8_t r);
void orbInit(); // after orbPanelBegin(): frame buffer, theme
uint8_t orbThemeIndex();
void orbApplyTheme(uint8_t idx); // live, plus the legacy FG/AL/BG colours
void orbSaveTheme(uint8_t idx);  // NVS
int orbBootScreen();             // -1 Launcher, n boot installed app n, 200 boot the current app
void orbMainMenu();              // one pass of the home orbit (main loop)
void orbChargeMode();
void orbPowerOff();

// Transient UI: steps panel, install job, ring progress. Persistent screens end it
// before they take over again.
void orbEndTransient();
int orbRun(orb::Scene &s); // end transients, then run s
void orbJobBegin(const String &name, bool download);
void orbJobStage(const char *label, uint32_t size, uint32_t ofs = 0, bool app = false);
void orbJobAppName(const String &name);
void orbArcBegin(const char *title, const String &sub);
void orbArcProgress(uint32_t done, uint32_t total);
void orbArcEnd();
void orbToast(const char *msg, bool ok = true);
bool orbAsk(
    const char *title, const String &msg, const char *const *buttons, int n, int sel, uint8_t dangerMask,
    int &picked
);
void orbInfo(
    const char *title, const char *status, bool warn, const char *const *keys, const String *vals, int n
);
void orbWebLog(int method, const char *uri);
uint8_t orbOptIcon(const String &label); // glyph for a menu label (by keyword), 255 = none
uint8_t orbOptKind(const String &label); // RK_ITEM, RK_OP or RK_DANGER
bool orbSyncRemote();                    // UI task: show what other tasks reported (web uploads)
// Run fn on a worker task while the screen keeps animating (inline if no memory)
void orbWork(void (*fn)(void *), void *arg, uint32_t stack, bool input = false);
// LauncherHub downlink screen shown while fetching; ends with the next screen
orb::FetchScene &orbFetchBegin(const String &path);
extern const char *orbFetchText;
#endif
