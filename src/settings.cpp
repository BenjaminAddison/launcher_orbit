
#include "settings.h"
#include "display.h"
#include "esp_mac.h"
#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "mykeyboard.h"
#include "nvs.h"
#include "nvs_helpers.h"
#include "onlineLauncher.h"
#include "partitioner.h"
#include "powerSave.h"
#include "sd_functions.h"
#include "utils.h"
#include "wifi_crypto.h"
#if defined(ORBIT_UI)
#include "orbit/orbit_bridge.h"
#endif
#include <FS.h>
#include <SD.h>
#include <cstdio>
#include <cstdlib>
#include <globals.h>
#include <memory>
#if !defined(SDM_SD)
#include <SD_MMC.h>
#endif
#ifdef USE_CARDKB2
#include <cardkb2.h>
#endif

#define _CLR_(a, b) (a == b ? FGCOLOR : ALCOLOR)
// forward declaration
void defaultValues();

namespace {
// Avoids re-erasing/rewriting the whole l_wifi NVS namespace on every
// saveConfigs() call when the wifi list itself hasn't changed.
bool wifiListDirty = true;
uint32_t crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    while (length--) {
        crc ^= *data++;
        for (int i = 0; i < 8; ++i) {
            if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
            else crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

String makeWifiKey(char prefix, uint32_t crc) {
    char key[11] = {0};
    std::snprintf(key, sizeof(key), "%c_%08X", prefix, crc);
    return String(key);
}

JsonArray ensureWifiListInternal() {
    JsonObject setting = ensureSettingsRoot();
    if (setting.isNull()) return JsonArray();

    JsonArray wifiList = setting["wifi"].as<JsonArray>();
    if (wifiList.isNull()) { wifiList = setting.createNestedArray("wifi"); }
    if (wifiList.isNull()) { log_e("ensureWifiList: failed to create wifi list"); }
    return wifiList;
}

JsonObject ensureKeyBindingObjectInternal() {
    JsonObject setting = ensureSettingsRoot();
    if (setting.isNull()) return JsonObject();

    JsonObject bindings = setting["key_binding"].as<JsonObject>();
    if (bindings.isNull()) { bindings = setting.createNestedObject("key_binding"); }
    if (bindings.isNull()) { log_e("ensureKeyBindingObject: failed to create key_binding object"); }
    return bindings;
}

// Both return true only when the key was missing and got created, so the caller
// knows whether a commit is needed.
bool ensureStringKey(nvs_handle_t handle, const char *key, const char *value) {
    size_t len = 0;
    if (nvs_get_str(handle, key, nullptr, &len) == ESP_OK) return false;
    return lnvs::setString(handle, key, value);
}

bool ensureU8Key(nvs_handle_t handle, const char *key, uint8_t value) {
    uint8_t current = 0;
    if (nvs_get_u8(handle, key, &current) == ESP_OK) return false;
    return nvs_set_u8(handle, key, value) == ESP_OK;
}

bool backupConfigFileIfPresent() {
    if (!setupSdCard()) return true;
    String backupPath = String(CONFIG_FILE) + ".old";
    if (SDM.exists(backupPath)) SDM.remove(backupPath);
    if (!SDM.exists(CONFIG_FILE)) return true;
    return SDM.rename(CONFIG_FILE, backupPath);
}

void factoryReset() {
#if defined(ORBIT_UI)
    if (!orb::confirm(
            "RESET", "Erase Launcher settings and saved Wi-Fi? Installed apps stay.", "RESET", true
        ))
        return;
#else
    bool confirmed = false;
    options = {
        {"Reset Configs/Wifi", [&]() { confirmed = true; } },
        {"Cancel",             [&]() { confirmed = false; }},
    };
    loopOptions(options);
    if (!confirmed) return;
#endif
    lnvs::eraseNamespace("l_wifi");
    lnvs::eraseNamespace("launcher");
    backupConfigFileIfPresent();
    favorite = JsonArray();
    settings.clear();
    wifiListDirty = true;
    defaultValues();
    saveConfigs();
}
} // namespace

bool eraseNamespace(const char *ns) { return lnvs::eraseNamespace(ns); }

JsonObject ensureSettingsRoot() {
    JsonArray settingsArray = settings.as<JsonArray>();
    if (settingsArray.isNull()) {
        favorite = JsonArray();
        settings.clear();
        settingsArray = settings.to<JsonArray>();
    }
    if (settingsArray.isNull()) {
        log_e("ensureSettingsRoot: unable to prepare settings array");
        return JsonObject();
    }

    JsonObject setting;
    if (settingsArray.size() > 0 && settingsArray[0].is<JsonObject>()) {
        setting = settingsArray[0].as<JsonObject>();
    } else {
        favorite = JsonArray();
        settingsArray.clear();
        setting = settingsArray.add<JsonObject>();
    }

    if (setting.isNull()) { log_e("ensureSettingsRoot: failed to create root object"); }
    return setting;
}

String get_efuse_mac_as_string();

int applySettingsFromRoot(JsonObject setting) {
    int count = 0;

    if (setting["onlyBins"].is<bool>()) onlyBins = setting["onlyBins"].as<bool>();
    else {
        count++;
        log_i("applySettingsFromRoot: missing onlyBins");
    }

    if (setting["bootToApp"].is<bool>()) bootToApp = setting["bootToApp"].as<bool>();
    else {
        count++;
        log_i("applySettingsFromRoot: missing bootToApp");
    }

    if (setting["DDLB"].is<bool>()) DDLB = setting["DDLB"].as<bool>();
    else {
        count++;
        log_i("applySettingsFromRoot: missing DDLB");
    }

    // LauncherOnKey is bound to this specific device's MAC (same as rotation) so an SD
    // card shared across multiple physical units of the same board doesn't cross-apply it.
    String key_dev_name = get_efuse_mac_as_string() + "-" + device_name + "-key";
    if (setting[key_dev_name].is<int>()) LauncherOnKey = setting[key_dev_name].as<int>();
    else {
        count++;
        log_i("applySettingsFromRoot: missing LauncherOnKey");
    }

    if (setting["LauncherKeyLvl"].is<bool>()) LauncherKeyLvl = setting["LauncherKeyLvl"].as<bool>();
    else {
        count++;
        log_i("applySettingsFromRoot: missing LauncherKeyLvl");
    }

    if (setting["bootTimer"].is<int>()) bootTimer = setting["bootTimer"].as<int>();
    else {
        count++;
        log_i("applySettingsFromRoot: missing bootTimer");
    }

    if (setting["noDotFiles"].is<bool>()) noDotFiles = setting["noDotFiles"].as<bool>();
    else {
        count++;
        log_i("applySettingsFromRoot: missing noDotFiles");
    }

    if (setting["autoBackup"].is<bool>()) autoBackup = setting["autoBackup"].as<bool>();
    else log_i("applySettingsFromRoot: missing autoBackup");

    if (setting["askSpiffs"].is<bool>()) askSpiffs = setting["askSpiffs"].as<bool>();
    else {
        count++;
        log_i("applySettingsFromRoot: missing askSpiffs");
    }

    if (setting["bright"].is<int>()) bright = setting["bright"].as<int>();
    else {
        count++;
        log_i("applySettingsFromRoot: missing bright");
    }

    if (setting["dimmerSet"].is<int>()) dimmerSet = setting["dimmerSet"].as<int>();
    else {
        count++;
        log_i("applySettingsFromRoot: missing dimmerSet");
    }
    String rot_dev_name = get_efuse_mac_as_string() + "-" + device_name;
    if (setting[get_efuse_mac_as_string()].is<int>()) rotation = setting[get_efuse_mac_as_string()].as<int>();
    else if (setting[rot_dev_name].is<int>()) rotation = setting[rot_dev_name].as<int>();
    else {
        count++;
        log_i("applySettingsFromRoot: missing rotation");
    }

#ifndef E_PAPER_DISPLAY
    if (setting["FGCOLOR"].is<uint16_t>()) FGCOLOR = setting["FGCOLOR"].as<uint16_t>();
    else {
        count++;
        log_i("applySettingsFromRoot: missing FGCOLOR");
    }

    if (setting["BGCOLOR"].is<uint16_t>()) BGCOLOR = setting["BGCOLOR"].as<uint16_t>();
    else {
        count++;
        log_i("applySettingsFromRoot: missing BGCOLOR");
    }

    if (setting["ALCOLOR"].is<uint16_t>()) ALCOLOR = setting["ALCOLOR"].as<uint16_t>();
    else {
        count++;
        log_i("applySettingsFromRoot: missing ALCOLOR");
    }

    if (setting["odd"].is<uint16_t>()) odd_color = setting["odd"].as<uint16_t>();
    else {
        count++;
        log_i("applySettingsFromRoot: missing odd");
    }

    if (setting["even"].is<uint16_t>()) even_color = setting["even"].as<uint16_t>();
    else {
        count++;
        log_i("applySettingsFromRoot: missing even");
    }
#endif

    if (setting["dev"].is<bool>()) dev_mode = setting["dev"].as<bool>();
    else {
        count++;
        log_i("applySettingsFromRoot: missing dev");
    }
    if (setting["autoConnect"].is<bool>()) autoConnect = setting["autoConnect"].as<bool>();
    else {
        count++;
        log_i("applySettingsFromRoot: missing autoConnect");
    }

    if (setting["wui_usr"].is<String>()) wui_usr = setting["wui_usr"].as<String>();
    else {
        count++;
        log_i("applySettingsFromRoot: missing wui_usr");
    }

    if (setting["wui_pwd"].is<String>()) wui_pwd = setting["wui_pwd"].as<String>();
    else {
        count++;
        log_i("applySettingsFromRoot: missing wui_pwd");
    }

    if (setting["dwn_path"].is<String>()) dwn_path = setting["dwn_path"].as<String>();
    else {
        count++;
        log_i("applySettingsFromRoot: missing dwn_path");
    }

    if (setting["wifi"].is<JsonArray>()) {
        for (JsonObject wifiEntry : setting["wifi"].as<JsonArray>()) {
            if (wifiEntry["secure"].as<bool>()) {
                wifiEntry["pwd"] = wifiPwdDecrypt(wifiEntry["pwd"].as<String>());
                wifiEntry.remove("secure");
            } else if (!wifiEntry["ssid"].as<String>().isEmpty()) {
                ++count; // plain-text entry — trigger re-save with encryption
            }
        }
    } else {
        ++count;
        log_i("applySettingsFromRoot: missing wifi");
    }

    if (setting["favorite"].is<JsonArray>()) {
        favorite = setting["favorite"].as<JsonArray>();
    } else {
        ++count;
        log_i("applySettingsFromRoot: missing favorite");
    }

    return count;
}

void populateSettingsFromGlobals(JsonObject setting) {
    if (!setting["favorite"].is<JsonArray>()) favorite = setting.createNestedArray("favorite");
    else favorite = setting["favorite"].as<JsonArray>();
    if (!setting["wifi"].is<JsonArray>()) setting.createNestedArray("wifi");

    setting["onlyBins"] = onlyBins;
    setting["bootToApp"] = bootToApp;
    setting["bootTimer"] = bootTimer;
    setting["DDLB"] = DDLB;
    setting["LauncherKeyLvl"] = LauncherKeyLvl;
    setting["noDotFiles"] = noDotFiles;
    setting["autoBackup"] = autoBackup;
    setting["askSpiffs"] = askSpiffs;
    setting["bright"] = bright;
    setting["dimmerSet"] = dimmerSet;
    String rot_dev_name = get_efuse_mac_as_string() + "-" + device_name;
    setting[rot_dev_name] = rotation;
    String key_dev_name = get_efuse_mac_as_string() + "-" + device_name + "-key";
    setting[key_dev_name] = LauncherOnKey;
    setting["FGCOLOR"] = FGCOLOR;
    setting["BGCOLOR"] = BGCOLOR;
    setting["ALCOLOR"] = ALCOLOR;
    setting["odd"] = odd_color;
    setting["even"] = even_color;
    setting["dev"] = dev_mode;
    setting["autoConnect"] = autoConnect;
    setting["wui_usr"] = wui_usr;
    setting["wui_pwd"] = wui_pwd;
    setting["dwn_path"] = dwn_path;
}

void printSettingsJson() {
    JsonObject setting = ensureSettingsRoot();
    if (setting.isNull()) {
        launcherConsolePrintln("ERR failed to prepare settings");
        return;
    }
    populateSettingsFromGlobals(setting);
    String out;
    serializeJson(setting, out);
    launcherConsolePrintLong(out.c_str());
}

bool loadSettingsJson(const String &json) {
    JsonDocument incoming;
    DeserializationError error = deserializeJson(incoming, json);
    if (error || !incoming.is<JsonObject>()) {
        log_e("loadSettingsJson: parse error (%s)", error.c_str());
        return false;
    }

    JsonObject setting = ensureSettingsRoot();
    if (setting.isNull()) return false;

    for (JsonPair kv : incoming.as<JsonObject>()) setting[kv.key()] = kv.value();

    applySettingsFromRoot(setting);
    setBrightness(bright);
    if (dimmerSet > 120) dimmerSet = 10;
    saveConfigs();
    return true;
}

bool getWifiCredential(const String &searchSsid, String &outPwd) {
    JsonArray wifiList = ensureWifiListInternal();
    if (wifiList.isNull()) return false;

    for (JsonObject wifiEntry : wifiList) {
        if (wifiEntry["ssid"].as<String>() == searchSsid) {
            outPwd = wifiEntry["pwd"].as<String>();
            return true;
        }
    }

    return false;
}

bool ensureM5StackUiFlowNVSDefaults() {
#if defined(M5STACK)
    lnvs::Handle handle("uiflow", true);
    if (!handle) return false;
    nvs_handle_t h = handle.raw();

    bool changed = false;
    // https://github.com/m5stack/uiflow-micropython/blob/master/m5stack/partition_nvs.csv

    changed |= ensureStringKey(h, "server", "uiflow2.m5stack.com");
    changed |= ensureStringKey(h, "net_mode", "WIFI");
    changed |= ensureStringKey(h, "protocol", "DHCP");
    changed |= ensureStringKey(h, "ip_addr", "");
    changed |= ensureStringKey(h, "netmask", "");
    changed |= ensureStringKey(h, "gateway", "");
    changed |= ensureStringKey(h, "dns", "8.8.8.8");
    changed |= ensureStringKey(h, "ssid0", "");
    changed |= ensureStringKey(h, "pswd0", "");
    changed |= ensureStringKey(h, "ssid1", "");
    changed |= ensureStringKey(h, "pswd1", "");
    changed |= ensureStringKey(h, "ssid2", "");
    changed |= ensureStringKey(h, "pswd2", "");
    changed |= ensureStringKey(h, "sntp0", "ntp.aliyun.com");
    changed |= ensureStringKey(h, "sntp1", "jp.pool.ntp.org");
    changed |= ensureStringKey(h, "sntp2", "pool.ntp.org");
    changed |= ensureStringKey(h, "tz", "GMT0");
    changed |= ensureU8Key(h, "boot_option", 1);

    if (changed) {
        if (!handle.commit()) {
            launcherConsolePrintln("ensureM5StackUiFlowNVSDefaults: commit failed");
            return false;
        }
        launcherConsolePrintln("ensureM5StackUiFlowNVSDefaults: default UiFlow keys created");
    }

    return true;
#else
    return true;
#endif
}

bool setWifiCredential(const String &ssidValue, const String &passwordValue, bool persist) {
    JsonArray wifiList = ensureWifiListInternal();
    if (wifiList.isNull()) return false;

    JsonObject target;
    for (JsonObject wifiEntry : wifiList) {
        if (wifiEntry["ssid"].as<String>() == ssidValue) {
            target = wifiEntry;
            break;
        }
    }
    if (target.isNull()) { target = wifiList.createNestedObject(); }
    if (target.isNull()) {
        log_e("setWifiCredential: failed to allocate entry");
        return false;
    }

    target["ssid"] = ssidValue;
    target["pwd"] = passwordValue;

    wifiListDirty = true;
    if (persist) { saveConfigs(); }
    return true;
}

bool removeWifiCredential(const String &ssidValue) {
    JsonArray wifiList = ensureWifiListInternal();
    if (wifiList.isNull()) return false;

    for (size_t i = 0; i < wifiList.size(); i++) {
        if (wifiList[i]["ssid"].as<String>() == ssidValue) {
            wifiList.remove(i);
            wifiListDirty = true;
            saveConfigs();
            return true;
        }
    }
    return false;
}

bool clearWifiCredentials() {
    JsonArray wifiList = ensureWifiListInternal();
    if (wifiList.isNull()) return false;

    while (wifiList.size() > 0) wifiList.remove(0);
    wifiListDirty = true;
    saveConfigs();
    return true;
}

std::vector<LauncherSavedWifiNetwork> getSavedWifiNetworks() {
    std::vector<LauncherSavedWifiNetwork> result;
    JsonArray wifiList = ensureWifiListInternal();
    if (wifiList.isNull()) return result;

    for (JsonObject wifiEntry : wifiList) {
        String ssid = wifiEntry["ssid"].as<String>();
        if (ssid.isEmpty()) continue;
        LauncherSavedWifiNetwork net;
        net.ssid = ssid;
        net.hasPassword = wifiEntry["pwd"].as<String>().length() > 0;
        result.push_back(net);
    }
    return result;
}

bool getKeyBinding(const String &key, String &outPath) {
    JsonObject bindings = ensureKeyBindingObjectInternal();
    if (bindings.isNull()) return false;
    if (!bindings[key].is<String>()) return false;
    outPath = bindings[key].as<String>();
    return true;
}

bool setKeyBinding(const String &key, const String &path, bool persist) {
    JsonObject bindings = ensureKeyBindingObjectInternal();
    if (bindings.isNull()) return false;

    bindings[key] = path;
    if (persist) saveConfigs();
    return true;
}

bool removeKeyBinding(const String &key, bool persist) {
    JsonObject bindings = ensureKeyBindingObjectInternal();
    if (bindings.isNull()) return false;
    if (!bindings[key].is<String>()) return false;

    bindings.remove(key);
    if (persist) saveConfigs();
    return true;
}

bool clearKeyBindings() {
    JsonObject setting = ensureSettingsRoot();
    if (setting.isNull()) return false;

    setting.remove("key_binding");
    saveConfigs();
    return true;
}

// Valid GPIOs for LauncherOnKey: -1 (disabled) plus either every pin up to
// GPIO_NUM_MAX, or, when the board defines LAUNCHER_GPIO_KEYS (e.g.
// LAUNCHER_GPIO_KEYS = {{"Up", 2}, {"Down", 13}}), only that explicit,
// labeled set.
struct LauncherGpioKey {
    const char *label;
    int pin;
};

std::vector<int> launcherOnKeyGpioOptions() {
    std::vector<int> pins = {-1};
#if defined(LAUNCHER_GPIO_KEYS)
    static constexpr LauncherGpioKey allowed[] = LAUNCHER_GPIO_KEYS;
    for (const LauncherGpioKey &key : allowed) pins.push_back(key.pin);
#else
    for (int pin = 0; pin < GPIO_NUM_MAX; pin++) pins.push_back(pin);
#endif
    return pins;
}

static void selectLauncherOnKeyGpio() {
    std::vector<int> pins = launcherOnKeyGpioOptions();
    options.clear();
    for (int pin : pins) {
        String label = "Disabled";
        if (pin >= 0) {
#if defined(LAUNCHER_GPIO_KEYS)
            static constexpr LauncherGpioKey allowed[] = LAUNCHER_GPIO_KEYS;
            for (const LauncherGpioKey &key : allowed) {
                if (key.pin == pin) {
                    label = String(key.label) + " (" + String(pin) + ")";
                    break;
                }
            }
#else
            label = "GPIO " + String(pin);
#endif
        }
        options.push_back({label, [pin]() { LauncherOnKey = pin; }, _CLR_(LauncherOnKey, pin)});
    }
    loopOptions(options);
    saveConfigs();
}

#if defined(HAS_KEYBOARD)
#if !defined(ORBIT_UI) // Orbit UI has its own, see src/orbit/
static void manageKeyBindings() {
    int idx = 0;
    returnToMenu = false;
    while (idx >= 0 && !returnToMenu) {
        JsonObject bindings = ensureKeyBindingObjectInternal();
        std::vector<Option> opts;
        for (JsonPair kv : bindings) {
            String key = kv.key().c_str();
            opts.push_back({String("'") + key + "': Remove", [key]() { removeKeyBinding(key); }});
        }
        opts.push_back({"Reset All", [=]() { clearKeyBindings(); }});
        opts.push_back({"Back to Menu", [&]() { returnToMenu = true; }});
        idx = loopOptions(opts);
    }
}
#endif // !ORBIT_UI
#endif

#if !defined(ORBIT_UI) // Orbit UI has its own, see src/orbit/
void settings_menu() {
    int idx = 0;
    returnToMenu = false;
    while (idx >= 0 && !returnToMenu) {
        options = {
#ifndef E_PAPER_DISPLAY
            {"Charge Mode",
                                   [=]() {
                 chargeMode();
                 returnToMenu = true;
             }                                                                          },
#endif
#if !defined(E_PAPER_DISPLAY)
            {"UI Color",
                                   [=]() {
                 setUiColor();
                 saveConfigs();
             }                                                                          },
#endif
#if !defined(LYLYGO_TDECK_PRO)
            {"Orientation",
                                   [=]() {
                 gsetRotation(true);
                 saveConfigs();
             }                                                                          },
#endif
            {"[" + String(bright) + "] Brightness",
                                   [=]() {
                 setBrightnessMenu();
                 saveConfigs();
             }                                                                          },
            {"[" + String(dimmerSet) + "] Dim time", [=]() {
                 setdimmerSet();
                 saveConfigs();
             }},
        };
        if (sdcardMounted) {
            options.push_back({onlyBins ? "[ ] See All Files" : "[x] See All Files", [=]() {
                                   onlyBins = !onlyBins;
                                   saveConfigs();
                               }});
            options.push_back({noDotFiles ? "[ ] Show Dotfiles" : "[x] Show Dotfiles", [=]() {
                                   noDotFiles = !noDotFiles;
                                   saveConfigs();
                               }});
            options.push_back({autoBackup ? "[x] Auto Backup" : "[ ] Auto Backup", [=]() {
                                   autoBackup = !autoBackup;
                                   saveConfigs();
                               }});
        }
        options.push_back({askSpiffs ? "[x] Ask to copy SPIFFS" : "[ ] Ask to copy SPIFFS", [=]() {
                               askSpiffs = !askSpiffs;
                               saveConfigs();
                           }});
        options.push_back(
            {autoConnect ? "[x] Auto connect to known Wifi" : "[ ] Auto connect to known Wifi", [=]() {
                 autoConnect = !autoConnect;
                 saveConfigs();
             }}
        );
        options.push_back({bootToApp ? "[ ] Boot to Launcher" : "[x] Boot to Launcher", [=]() {
                               bootToApp = !bootToApp;
                               saveConfigs();
                           }});
        options.push_back({"[" + String(bootTimer + 1) + "] Boot Timer (s)", [=]() {
                               setBootTimer();
                               saveConfigs();
                           }});
        // DDLB: Disable DeepSleep to Launcher Boot. This is a toggle for the user to choose whether to boot
        // into the launcher or not when waking from deep sleep.
        options.push_back({DDLB ? "[ ] DeepSleep starts Launcher" : "[x] DeepSleep starts Launcher", [=]() {
                               DDLB = !DDLB;
                               saveConfigs();
                           }});
#if defined(LAUNCHER_GPIO_KEYS)
        String launcherOnKeyLabel = "-";
        if (LauncherOnKey > -1) {
            launcherOnKeyLabel = String(LauncherOnKey);
            static constexpr LauncherGpioKey allowed[] = LAUNCHER_GPIO_KEYS;
            for (const LauncherGpioKey &key : allowed) {
                if (key.pin == LauncherOnKey) {
                    launcherOnKeyLabel = key.label;
                    break;
                }
            }
        }
        options.push_back({"[" + launcherOnKeyLabel + "] GPIO on boot starts Launcher", [=]() {
                               selectLauncherOnKeyGpio();
                           }});
        if (LauncherOnKey >= 0) {
            options.push_back({LauncherKeyLvl ? "[1] GPIO Level" : "[0] GPIO Level", [=]() {
                                   LauncherKeyLvl = !LauncherKeyLvl;
                                   saveConfigs();
                               }});
        }
#endif
#if defined(HAS_KEYBOARD)
        options.push_back({"Manage shortcuts", [=]() { manageKeyBindings(); }});
#endif

        if (dev_mode) options.push_back({"Boot Animation", [=]() { initDisplayLoop(); }});
        if (dev_mode)
            options.push_back({"Deactivate Dev", [=]() {
                                   dev_mode = false;
                                   saveConfigs();
                               }});
#if defined(HAS_RESISTIVE_TOUCH)
        options.push_back({"Calibrate Touch", calibrateTouch});
#endif
#if defined(USE_CARDKB2) && defined(CARDKB2_SDA) && defined(CARDKB2_SCL)
        options.push_back({"Start CardKb", [=]() { cardkb2_setup(CARDKB2_SDA, CARDKB2_SCL); }});
#endif
        // Only worth offering when the co-processor was latched off: clearing the
        // guard re-arms the bring-up, which only runs at boot, so reboot with it.
        if (!hostedWifiAvailable) {
            options.push_back({"Retry WiFi Module", [=]() {
                                   launcherWifiHostedResetGuard();
                                   releaseHeapObjectsAndReboot();
                               }});
        }
        options.push_back({"Reset Configs/Wifi", factoryReset});
        options.push_back({"Restart", [=]() { return (void)releaseHeapObjectsAndReboot(); }});
        options.push_back({"Turn-off", [=]() { powerOff(); }});

        options.push_back({"Main Menu", [=]() { returnToMenu = true; }});
        idx = loopOptions(idx, options);
    }
    tft->drawPixel(0, 0, 0);
    tft->fillScreen(BGCOLOR);
}
#endif // !ORBIT_UI

#if defined(ORBIT_UI)
/*********************************************************************
** Orbit UI: settings as one grouped list. Left/right adjusts values in
** place, Enter toggles or opens. Charge mode lives on the home orbit.
**********************************************************************/
namespace {
const int DIM_STEPS[7] = {10, 15, 20, 30, 45, 60, 0};

int pickValue(const char *title, const char *const *labels, int n, int cur) {
    orb::MenuItem m[8];
    for (int i = 0; i < n && i < 8; i++)
        m[i] = {labels[i], 255, orb::RK_ITEM, i == cur ? "\xE2\x9C\x93" : nullptr};
    return orb::sheet(m, n, title, nullptr, cur >= 0 ? cur : 0);
}

void shortcutsMenu() {
    struct KeySrc : orb::ListSrc {
        char keys[40];
        String paths[40];
        char caps[40][2];
        int n = 0;
        void load() {
            n = 0;
            String p;
            for (char c = '0'; c <= 'z' && n < 40; c++)
                if ((c <= '9' || c >= 'a') && getKeyBinding(String(c), p)) {
                    keys[n] = c;
                    caps[n][0] = toupper(c);
                    caps[n][1] = 0;
                    paths[n++] = p;
                }
        }
        int count() override { return (n ? n : 1) + 1 + (n ? 1 : 0) + 1; }
        uint8_t kind(int i) override {
            if (i < (n ? n : 1)) return n ? orb::RK_ITEM : orb::RK_OP;
            if (i == (n ? n : 1)) return orb::RK_SECTION;
            return n && i == n + 1 ? orb::RK_DANGER : orb::RK_OP;
        }
        void row(int i, orb::Row &r) override {
            r.kind = kind(i);
            if (!n && i == 0) {
                r.label = "No shortcuts yet";
                r.color = orb::C(orb::T_dim);
                return;
            }
            if (i < n) {
                int sl = paths[i].lastIndexOf('/');
                r.label = paths[i].c_str() + sl + 1;
                String dir = sl > 0 ? paths[i].substring(0, sl) : String("/");
                strncpy(r.right, dir.c_str(), sizeof(r.right) - 1);
                r.sub = paths[i].c_str();
                return;
            }
            if (i == (n ? n : 1)) {
                r.label = "BIND FROM SD \xE2\x80\xBA FILE \xE2\x80\xBA BIND TO KEY";
                return;
            }
            if (n && i == n + 1) {
                r.label = "Reset all";
                r.icon = orb::G_TRASH;
                return;
            }
            r.label = "Back to settings";
            r.icon = orb::G_BACK;
        }
        bool icon(int i, orb::Gfx &g, int x, int y, bool sel) override {
            if (i >= n) return false;
            orb::keycap(g, caps[i], x, y + 2, !sel);
            return true;
        }
    } src;
    src.load();
    orb::ListScene sc;
    sc.hdr.icon = orb::G_KEY;
    sc.hdr.title = "SHORTCUTS";
    sc.hdr.crumb = "HOME SCREEN KEYS";
    sc.list.iconW = 14;
    strcpy(sc.right, "PRESS THE KEY ON HOME");
    sc.setup(&src, 0);
    sc.list.iconW = 14;
    orb::push(sc);
    for (;;) {
        sc.hk = src.n && sc.list.sel < src.n ? "\xE2\x8F\x8E|REMOVE|`|BACK" : "\xE2\x8F\x8E|OPEN|`|BACK";
        int r = orbRun(sc);
        if (r < 0) break;
        if (src.n && r < src.n) {
            char msg[96];
            snprintf(
                msg,
                sizeof(msg),
                "Remove '%c' \xE2\x86\x92 %s?",
                toupper(src.keys[r]),
                src.paths[r].c_str() + src.paths[r].lastIndexOf('/') + 1
            );
            if (orb::confirm("SHORTCUT", msg, "REMOVE", true)) {
                removeKeyBinding(String(src.keys[r]));
                orbToast("Shortcut removed");
            }
        } else if (src.n && r == src.n + 1) {
            if (orb::confirm("RESET", "Remove every keyboard shortcut?", "REMOVE ALL", true))
                clearKeyBindings();
        } else if (r == src.count() - 1) break;
        src.load();
        sc.list.refresh(true);
    }
    if (orb::top() == &sc) orb::pop();
    else orb::remove(sc);
}
} // namespace

static void manageKeyBindings() { shortcutsMenu(); }

void setBrightnessMenu() {
    orb::BrightnessScene b;
    b.val = (bright / 5) * 5 < 5 ? 5 : (bright / 5) * 5;
    b.preview = [](int v) { setBrightness(v, false); };
    if (orb::modal(b, orb::TR_NONE) == 1) {
        setBrightness(b.val, true);
        char msg[24];
        snprintf(msg, sizeof(msg), "Brightness %d%%", b.val);
        orbToast(msg);
    } else setBrightness(bright, false);
}

void setUiColor() {
    uint8_t orig = orbThemeIndex();
    orb::ColorScene c;
    c.sel = orig;
    if (orb::modal(c, orb::TR_NONE) == 1) {
        orbApplyTheme(c.sel);
        orbSaveTheme(c.sel);
        char msg[32];
        snprintf(msg, sizeof(msg), "Theme %s saved", orb::THEME_NAMES[c.sel]);
        orbToast(msg);
    } else orbApplyTheme(orig);
}

void settings_menu() {
    enum : uint8_t {
        I_COLOR,
        I_ROT,
        I_BRIGHT,
        I_DIM,
        I_ALL,
        I_DOT,
        I_BKP,
        I_SPIFFS,
        I_AUTO,
        I_BTL,
        I_TIMER,
        I_DDLB,
        I_KEYS,
        I_RESTART,
        I_OFF,
        I_RESET,
        I_WIFIMOD,
        I_ANIM,
        I_DEVOFF,
        I_HOME,
        I_NONE
    };
    static orb::SettingsScene sc;
    uint8_t ids[orb::SettingsScene::MAXI];
    float animById[I_NONE + 1];
    for (float &a : animById) a = -1;
    int sel = 1;
    returnToMenu = false;
    bool first = true;
    for (;;) {
        for (int k = 0; !first && k < sc.n; k++) animById[ids[k]] = sc.items[k].anim; // toggles keep sliding
        sc.n = 0;
        auto add =
            [&](uint8_t id, const char *label, uint8_t icon, uint8_t kind, bool on = false, bool adj = false
            ) -> orb::SetItem & {
            orb::SetItem &it = sc.items[sc.n];
            it = orb::SetItem();
            it.label = label;
            it.icon = icon;
            it.kind = kind;
            it.on = on;
            it.adj = adj;
            it.anim = animById[id] >= 0 ? animById[id] : (on ? 1 : 0);
            ids[sc.n++] = id;
            return it;
        };
        const int landscapeA = 1 + ROT_OFFSET, landscapeB = 3 + ROT_OFFSET;
        add(I_NONE, "DISPLAY", 255, orb::SK_SECTION);
        strcpy(add(I_COLOR, "UI color", orb::G_EYE, orb::SK_THEME).val, orb::THEME_NAMES[orb::themeIdx]);
        strcpy(
            add(I_ROT, "Orientation", orb::G_ROTATE, orb::SK_VALUE).val,
            rotation == landscapeB ? "LANDSCAPE 180" : "LANDSCAPE"
        );
        orb::SetItem &br = add(I_BRIGHT, "Brightness", orb::G_BRIGHT, orb::SK_BAR, false, true);
        snprintf(br.val, sizeof(br.val), "%d%%", bright);
        br.bar = bright / 100.0f;
        orb::SetItem &dm = add(I_DIM, "Dim after", orb::G_TIMER, orb::SK_VALUE, false, true);
        if (dimmerSet) snprintf(dm.val, sizeof(dm.val), "%dS", dimmerSet);
        else strcpy(dm.val, "NEVER");
        if (sdcardMounted) {
            add(I_NONE, "FILES", 255, orb::SK_SECTION);
            add(I_ALL, "See all files", orb::G_FILE, orb::SK_TOGGLE, !onlyBins);
            add(I_DOT, "Show dotfiles", orb::G_EYE, orb::SK_TOGGLE, !noDotFiles);
            add(I_BKP, "Auto backup", orb::G_BKP, orb::SK_TOGGLE, autoBackup);
        }
        add(I_SPIFFS, "Ask to copy SPIFFS", orb::G_CHIP, orb::SK_TOGGLE, askSpiffs);
        add(I_NONE, "NETWORK", 255, orb::SK_SECTION);
        add(I_AUTO, "Auto-connect known Wi-Fi", orb::G_WIFI, orb::SK_TOGGLE, autoConnect);
        add(I_NONE, "BOOT", 255, orb::SK_SECTION);
        add(I_BTL, "Boot to Launcher", orb::G_ROCKET, orb::SK_TOGGLE, !bootToApp);
        snprintf(
            add(I_TIMER, "Boot timer", orb::G_TIMER, orb::SK_VALUE, false, true).val, 20, "%dS", bootTimer + 1
        );
        add(I_DDLB, "Deep sleep opens Launcher", orb::G_POWER, orb::SK_TOGGLE, !DDLB);
        {
            JsonObject b = ensureKeyBindingObjectInternal();
            snprintf(
                add(I_KEYS, "Keyboard shortcuts", orb::G_KEY, orb::SK_VALUE).val,
                20,
                "%d KEYS \xE2\x80\xBA",
                (int)b.size()
            );
        }
        add(I_NONE, "POWER", 255, orb::SK_SECTION);
        strcpy(add(I_RESTART, "Restart", orb::G_RESET, orb::SK_ACTION).val, "\xE2\x80\xBA");
        strcpy(add(I_OFF, "Turn off", orb::G_POWER, orb::SK_ACTION).val, "\xE2\x80\xBA");
        add(I_NONE, "SYSTEM", 255, orb::SK_SECTION);
        if (!hostedWifiAvailable) add(I_WIFIMOD, "Retry WiFi module", orb::G_WIFI, orb::SK_ACTION);
        add(I_RESET, "Reset configs & Wi-Fi", orb::G_WARN, orb::SK_DANGER);
        if (dev_mode) {
            add(I_NONE, "DEVELOPER", 255, orb::SK_SECTION);
            strcpy(add(I_ANIM, "Boot animation", orb::G_PLAY, orb::SK_ACTION).val, "\xE2\x80\xBA");
            add(I_DEVOFF, "Deactivate dev mode", orb::G_CROSS, orb::SK_OP);
        }
        add(I_HOME, "Main menu", orb::G_HOME, orb::SK_OP);
        sc.version = LAUNCHER;
        if (first) {
            sc.begin(sel);
            orb::push(sc);
            first = false;
        } else sc.list.refresh(true);
        int r = orbRun(sc);
        if (r < 0) break;
        int i = r / 4, act = r % 4;
        uint8_t id = ids[i];
        int d = act == orb::SA_LEFT ? -1 : act == orb::SA_RIGHT ? 1 : 0;
        bool save = true;
        switch (id) {
            case I_COLOR: setUiColor(); break;
            case I_ROT: {
                const char *L[2] = {"Landscape (default)", "Landscape 180\xC2\xB0"};
                int p = pickValue("ORIENTATION", L, 2, rotation == landscapeB ? 1 : 0);
                if (p >= 0) {
                    rotation = p ? landscapeB : landscapeA;
                    orbSetRotation(rotation);
                }
                break;
            }
            case I_BRIGHT:
                if (d) {
                    int v = bright + d * 5;
                    setBrightness(v < 5 ? 5 : v > 100 ? 100 : v, true);
                } else setBrightnessMenu();
                break;
            case I_DIM: {
                int cur = 0;
                for (int k = 0; k < 7; k++)
                    if (DIM_STEPS[k] == dimmerSet) cur = k;
                if (d) {
                    cur += d;
                    dimmerSet = DIM_STEPS[cur < 0 ? 0 : cur > 6 ? 6 : cur];
                } else {
                    const char *L[7] = {
                        "10 seconds",
                        "15 seconds",
                        "20 seconds",
                        "30 seconds",
                        "45 seconds",
                        "60 seconds",
                        "Never"
                    };
                    int p = pickValue("DIM AFTER", L, 7, cur);
                    if (p >= 0) dimmerSet = DIM_STEPS[p];
                }
                break;
            }
            case I_ALL: onlyBins = !onlyBins; break;
            case I_DOT: noDotFiles = !noDotFiles; break;
            case I_BKP: autoBackup = !autoBackup; break;
            case I_SPIFFS: askSpiffs = !askSpiffs; break;
            case I_AUTO: autoConnect = !autoConnect; break;
            case I_BTL: bootToApp = !bootToApp; break;
            case I_TIMER:
                if (d) {
                    int v = bootTimer + d;
                    bootTimer = v < 0 ? 0 : v > 4 ? 4 : v;
                } else {
                    const char *L[5] = {"1 + 0 s", "1 + 1 s", "1 + 2 s", "1 + 3 s", "1 + 4 s"};
                    int p = pickValue("BOOT TIMER", L, 5, bootTimer);
                    if (p >= 0) bootTimer = p;
                }
                break;
            case I_DDLB: DDLB = !DDLB; break;
            case I_KEYS:
                manageKeyBindings();
                save = false;
                break;
            case I_RESTART:
                if (orb::confirm("RESTART", "Restart the Cardputer now?", "RESTART"))
                    releaseHeapObjectsAndReboot();
                save = false;
                break;
            case I_OFF: orbPowerOff(); break;
            case I_WIFIMOD:
                launcherWifiHostedResetGuard();
                releaseHeapObjectsAndReboot();
                break;
            case I_RESET:
                factoryReset();
                orbApplyTheme(orbThemeIndex());
                save = false;
                break;
            case I_ANIM:
                initDisplayLoop();
                returnToMenu = false;
                save = false;
                break;
            case I_DEVOFF:
                dev_mode = false;
                orbToast("Dev mode off");
                break;
            case I_HOME: returnToMenu = true; break;
        }
        if (save && id != I_HOME) saveConfigs();
        if (returnToMenu) break;
    }
    if (orb::top() == &sc) orb::pop();
    else orb::remove(sc);
}
#endif // ORBIT_UI

// This function comes from interface.h
void _setBrightness(uint8_t brightval) {
#ifdef TFT_BL
    if (brightval == 0) {
        analogWrite(TFT_BL, brightval);
    } else {
        const uint8_t PWM_MIN = 85;
        const uint8_t PWM_MAX = 255;
        float linear = (float)brightval / 100.0;
        uint8_t value = PWM_MIN + round(pow(linear, 2.2) * (PWM_MAX - PWM_MIN));
        analogWrite(TFT_BL, value);
    }
#endif
}

/*********************************************************************
**  Function: setBrightness
**  save brightness value into EEPROM
**********************************************************************/
void setBrightness(int brightval, bool save) {
    if (brightval > 100) brightval = 100;
    _setBrightness(brightval);
    if (save) { bright = brightval; }
}

/*********************************************************************
**  Function: getBrightness
**  save brightness value into EEPROM
**********************************************************************/
void getBrightness() {
    if (bright > 100) {
        bright = 100;
        _setBrightness(bright);
        setBrightness(100);
    }
    _setBrightness(bright);
}

/*********************************************************************
**  Function: gsetRotation
**  get onlyBins from EEPROM
**********************************************************************/
int gsetRotation(bool set) {

    const int mountRotation = displayConfig.rotation;
    int result = mountRotation;

    // Selectable rotations live in the window ROT_OFFSET..ROT_OFFSET+3 -- the
    // board's own rotation family (ROT_OFFSET is 0 for almost every board, see
    // pre_compiler.h). Anything outside it is a stale value saved by a build
    // with a different family, so fall back to the board's mounting rotation.
    if (rotation < ROT_OFFSET || rotation > ROT_OFFSET + 3) {
        set = true;
        result = mountRotation;
    } else result = rotation;

    if (set) {
        const int DRV = (displayConfig.width < displayConfig.height) ? 1 : 0;
        const bool offerPortrait = panelWidth() >= 200 && panelHeight() >= 200;
        // The four rotations this panel can take, in its own family.
        const int landscapeA = DRV + ROT_OFFSET;
        const int landscapeB = DRV + 2 + ROT_OFFSET;
        const int portraitA = (DRV == 1 ? 0 : 1) + ROT_OFFSET;
        const int portraitB = (DRV == 1 ? 2 : 3) + ROT_OFFSET;
        options = {
            {"Default (" + String(mountRotation) + ")", [&]() { result = mountRotation; }}
        };
        if (offerPortrait)
            options.push_back(
                {"Portrait " + String(portraitA), [&]() { result = portraitA; }, _CLR_(rotation, portraitA)}
            );
        options.push_back(
            {"Landscape " + String(landscapeA), [&]() { result = landscapeA; }, _CLR_(rotation, landscapeA)}
        );
        if (offerPortrait)
            options.push_back(
                {"Portrait " + String(portraitB), [&]() { result = portraitB; }, _CLR_(rotation, portraitB)}
            );
        options.push_back(
            {"Landscape " + String(landscapeB), [&]() { result = landscapeB; }, _CLR_(rotation, landscapeB)}
        );
        loopOptions(options);
        rotation = result;

        // See the same block in main.cpp: the panel size is runtime state now.
        if (rotation & 0b1) {
#if defined(HAS_TOUCH) && !defined(HAS_TOUCH_NO_BORDER)
            tftHeight = displayConfig.width - (_fm * LH + 4);
#else
            tftHeight = displayConfig.width;
#endif
            tftWidth = displayConfig.height;
        } else {
#if defined(HAS_TOUCH) && !defined(HAS_TOUCH_NO_BORDER)
            tftHeight = displayConfig.height - (_fm * LH + 4);
#else
            tftHeight = displayConfig.height;
#endif
            tftWidth = displayConfig.width;
        }

        tft->setRotation(result);
        tft->fillScreen(BGCOLOR);
    }
    return result;
}
/*********************************************************************
**  Function: setBrightnessMenu
**  Handles Menu to set brightness
**********************************************************************/
#if !defined(ORBIT_UI) // Orbit UI has its own, see src/orbit/
void setBrightnessMenu() {
    int original = bright;
    int val = (bright / 5) * 5;
    if (val > 100) val = 100;
    if (val < 0) val = 0;

    const int lineHeight = _fm * LH;
    const int hintHeight = _fp * LH;
    const int sliderH = 12;
    const int radius = sliderH / 2;
    const int spacing = 8;
    const int paddingTop = 8;
    const int paddingBottom = 8;
    const int paddingSide = 12;

    int contentWidth = static_cast<int>(tftWidth * 0.8f);
    int contentHeight = paddingTop + lineHeight + spacing + sliderH + spacing + hintHeight + paddingBottom;
    int boxX = (tftWidth - contentWidth) / 2;
    int boxY = (tftHeight - contentHeight) / 2;

    int trackX = boxX + paddingSide;
    int trackW = contentWidth - 2 * paddingSide;
    int trackY = boxY + paddingTop + lineHeight + spacing;

    bool redraw = true;
    bool first_draw = true;
    while (1) {
        if (redraw) {
            if (first_draw) {
                tft->fillRoundRect(boxX, boxY, contentWidth, contentHeight, 5, BGCOLOR);
                tft->drawRoundRect(boxX, boxY, contentWidth, contentHeight, 5, FGCOLOR);
                first_draw = false;
            }
            tft->setTextSize(_fm);
            tft->setTextColor(FGCOLOR, BGCOLOR);
            tft->drawCentreString(
                " Bright: " + String(val) + "% ", boxX + contentWidth / 2, boxY + paddingTop, 1
            );

            int indicatorX = trackX + (trackW * val) / 100;
            tft->fillRect(trackX, trackY - 3, trackW, 3, BGCOLOR);
            tft->fillRect(trackX, trackY + sliderH, trackW, 3, BGCOLOR);
            tft->fillRect(trackX - radius - 2, trackY - 4, radius * 2, sliderH + 8, BGCOLOR);
            tft->fillRect(trackX + trackW - radius + 2, trackY - 4, radius * 2, sliderH + 8, BGCOLOR);
            tft->drawRoundRect(trackX, trackY, trackW, sliderH, radius, ALCOLOR);
            int fillW = indicatorX - trackX;

            tft->fillRoundRect(trackX, trackY, fillW, sliderH, radius, ALCOLOR);
            tft->fillRoundRect(trackX + fillW, trackY + 1, trackW - fillW, sliderH - 2, radius, BGCOLOR);

            tft->fillCircle(indicatorX, trackY + sliderH / 2, radius + 2, FGCOLOR);

            tft->setTextSize(_fp);
            tft->setTextColor(FGCOLOR, BGCOLOR);
            tft->drawCentreString("Sel to apply", boxX + contentWidth / 2, trackY + sliderH + spacing, 1);

            setBrightness(val, false);
            tft->display();
            redraw = false;
        }

#if defined(HAS_TOUCH)
        if (touchPoint.pressed) {
            const int touchX = touchPoint.x;
            const int touchY = touchPoint.y;
            touchPoint.Clear();

            const int touchPadding = 10;
            if (touchX >= boxX && touchX <= boxX + contentWidth && touchY >= trackY - touchPadding &&
                touchY <= trackY + sliderH + touchPadding) {
                int clampedX = touchX;
                if (clampedX < trackX) clampedX = trackX;
                if (clampedX > trackX + trackW) clampedX = trackX + trackW;
                int newVal = ((clampedX - trackX) * 100 + trackW / 2) / trackW;
                newVal = (newVal / 5) * 5;
                if (newVal > 100) newVal = 100;
                if (newVal < 0) newVal = 0;
                if (newVal != val) {
                    val = newVal;
                    redraw = true;
                }
            }
        }
#endif

        if (check(NextPress)) {
            val += 5;
            if (val > 100) val = 100;
            redraw = true;
        }
        if (check(PrevPress)) {
            val -= 5;
            if (val < 0) val = 0;
            redraw = true;
        }
        if (check(SelPress)) {
            setBrightness(val);
            break;
        }
        if (check(EscPress) || returnToMenu) {
            setBrightness(original, false);
            break;
        }
    }
    tft->fillScreen(BGCOLOR);
}
#endif // !ORBIT_UI
/*********************************************************************
**  Function: setUiColor
**  Change Ui Color scheme
**********************************************************************/
#if !defined(ORBIT_UI) // Orbit UI has its own, see src/orbit/
void setUiColor() {
    options = {
        {"Default",
         [&]() {
             FGCOLOR = 0x07E0;
             BGCOLOR = 0x0000;
             ALCOLOR = 0xF800;
             odd_color = 0x30c5;
             even_color = 0x32e5;
         }, _CLR_(FGCOLOR, 0x07E0)},
        {"Red",
         [&]() {
             FGCOLOR = 0xF800;
             BGCOLOR = 0x0000;
             ALCOLOR = 0xE3E0;
             odd_color = 0xFBC0;
             even_color = 0xAAC0;
         }, _CLR_(FGCOLOR, 0xF800)},
        {"Blue",
         [&]() {
             FGCOLOR = 0x94BF;
             BGCOLOR = 0x0000;
             ALCOLOR = 0xd81f;
             odd_color = 0xd69f;
             even_color = 0x079F;
         }, _CLR_(FGCOLOR, 0x94BF)},
        {"Yellow",
         [&]() {
             FGCOLOR = 0xFFE0;
             BGCOLOR = 0x0000;
             ALCOLOR = 0xFB80;
             odd_color = 0x9480;
             even_color = 0xbae0;
         }, _CLR_(FGCOLOR, 0xFFE0)},
        {"Purple",
         [&]() {
             FGCOLOR = 0xe01f;
             BGCOLOR = 0x0000;
             ALCOLOR = 0xF800;
             odd_color = 0xf57f;
             even_color = 0x89d3;
         }, _CLR_(FGCOLOR, 0xe01f)},
        {"White",
         [&]() {
             FGCOLOR = 0xFFFF;
             BGCOLOR = 0x0000;
             ALCOLOR = 0x6b6d;
             odd_color = 0x630C;
             even_color = 0x8410;
         }, _CLR_(FGCOLOR, 0xFFFF)},
        {"Black",
         [&]() {
             FGCOLOR = 0x0000;
             BGCOLOR = 0xFFFF;
             ALCOLOR = 0x6b6d;
             odd_color = 0x8c71;
             even_color = 0xb596;
         }, _CLR_(FGCOLOR, 0x0000)},
    };
    loopOptions(options);
    displayRedStripe("Saving...");
}
#endif // !ORBIT_UI
/*********************************************************************
**  Function: setdimmerSet
**  set dimmerSet time
**********************************************************************/
void setdimmerSet() {
    int time = 20;
    options = {
        {"10s",     [&]() { time = 10; }, _CLR_(dimmerSet, 10)},
        {"15s",     [&]() { time = 15; }, _CLR_(dimmerSet, 15)},
        {"30s",     [&]() { time = 30; }, _CLR_(dimmerSet, 30)},
        {"45s",     [&]() { time = 45; }, _CLR_(dimmerSet, 45)},
        {"60s",     [&]() { time = 60; }, _CLR_(dimmerSet, 60)},
        {"Disable", [&]() { time = 0; },  _CLR_(dimmerSet, 0) },
    };

    loopOptions(options);
    dimmerSet = time;
}

/*********************************************************************
**  Function: setBootTimer
**  set bootTimer (extra seconds added to the 1s boot-screen minimum)
**********************************************************************/
void setBootTimer() {
    uint8_t time = bootTimer;
    options = {
        {"1+0s", [&]() { time = 0; }, _CLR_(bootTimer, 0)},
        {"1+1s", [&]() { time = 1; }, _CLR_(bootTimer, 1)},
        {"1+2s", [&]() { time = 2; }, _CLR_(bootTimer, 2)},
        {"1+3s", [&]() { time = 3; }, _CLR_(bootTimer, 3)},
        {"1+4s", [&]() { time = 4; }, _CLR_(bootTimer, 4)},
    };

    loopOptions(options);
    bootTimer = time;
}

/*********************************************************************
**  Function: chargeMode
**  Enter in Charging mode
**********************************************************************/
#if !defined(ORBIT_UI) // Orbit UI has its own, see src/orbit/
void chargeMode() {
#ifndef CONFIG_IDF_TARGET_ESP32P4
    setCpuFrequencyMhz(80);
#endif
    setBrightness(25, false);
    vTaskDelay(pdTICKS_TO_MS(500));
    tft->fillScreen(BGCOLOR);
    unsigned long tmp = 0;
    while (!check(SelPress)) {
        if (launcherMillis() - tmp > 5000) {
            displayRedStripe(
                String(getBattery()) + (isCharging() ? " % CHG" : " %"),
                getComplementaryColor(BGCOLOR),
                ALCOLOR,
                false
            );
            tmp = launcherMillis();
        }
    }
#ifndef CONFIG_IDF_TARGET_ESP32P4
    setCpuFrequencyMhz(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
#endif
    setBrightness(bright, false);
}
#endif // !ORBIT_UI
String get_efuse_mac_as_string() {
    uint8_t mac[6] = {0};
    String str = "";
    esp_efuse_mac_get_default(mac);
    for (int i = 0; i < 6; i++) {
        if (i > 0) str += ":";
        str += String(mac[i], 16);
    }
    return str;
}

bool saveIntoNVS() {
    lnvs::Handle nvsHandle("launcher", true);
    if (!nvsHandle) return false;
    nvs_handle_t h = nvsHandle.raw();

    bool ok = true;
    ok &= lnvs::setInt(h, "dimtime", dimmerSet);
    ok &= lnvs::setInt(h, "bright", bright);
    ok &= lnvs::setBool(h, "onlyBins", onlyBins);
    ok &= lnvs::setBool(h, "bootToApp", bootToApp);
    ok &= nvs_set_u8(h, "bootTimer", bootTimer) == ESP_OK;
    ok &= lnvs::setBool(h, "DDLB", DDLB);
    ok &= lnvs::setInt(h, "LauncherOnKey", LauncherOnKey);
    ok &= lnvs::setBool(h, "LauncherKeyLvl", LauncherKeyLvl);
    ok &= lnvs::setBool(h, "noDotFiles", noDotFiles);
    ok &= lnvs::setBool(h, "autoBackup", autoBackup);
    ok &= lnvs::setBool(h, "askSpiffs", askSpiffs);
    ok &= lnvs::setInt(h, "rotation", rotation);
    ok &= nvs_set_u16(h, "FGCOLOR", FGCOLOR) == ESP_OK;
    ok &= nvs_set_u16(h, "BGCOLOR", BGCOLOR) == ESP_OK;
    ok &= nvs_set_u16(h, "ALCOLOR", ALCOLOR) == ESP_OK;
    ok &= nvs_set_u16(h, "odd_color", odd_color) == ESP_OK;
    ok &= nvs_set_u16(h, "even_color", even_color) == ESP_OK;
    ok &= lnvs::setBool(h, "dev_mode", dev_mode);
    ok &= lnvs::setBool(h, "autoConnect", autoConnect);
    ok &= lnvs::setString(h, "wui_usr", wui_usr.c_str());
    ok &= lnvs::setString(h, "wui_pwd", wui_pwd.c_str());
    ok &= lnvs::setString(h, "dwn_path", dwn_path.c_str());
    ok &= lnvs::setString(h, "last_app", lastInstalledApp.c_str());
#if defined(HEADLESS)
    // SD Pins
    ok &= nvs_set_i8(h, "miso", _miso) == ESP_OK;
    ok &= nvs_set_i8(h, "mosi", _mosi) == ESP_OK;
    ok &= nvs_set_i8(h, "sck", _sck) == ESP_OK;
    ok &= nvs_set_i8(h, "cs", _cs) == ESP_OK;
#endif
    if (!ok) {
        launcherConsolePrintln("Failed to store settings in NVS");
    } else {
        launcherConsolePrintln("Settings stored in NVS successfully");
    }

    nvsHandle.commit();
    if (!saveWifiIntoNVS()) { launcherConsolePrintln("saveIntoNVS: failed to store WiFi list"); }
    return true;
}

bool saveSessionToken(const String &token) {
    lnvs::Handle nvsHandle("launcher", true);
    if (!nvsHandle) return false;

    bool ok = token.isEmpty() ? lnvs::eraseKey(nvsHandle.raw(), "token")
                              : lnvs::setString(nvsHandle.raw(), "token", token.c_str());
    return ok && nvsHandle.commit();
}

bool saveWifiIntoNVS() {
    if (!wifiListDirty) return true;

    JsonArray wifiList = ensureWifiListInternal();
    if (wifiList.isNull()) return false;

    lnvs::Handle nvsHandle("l_wifi", true);
    if (!nvsHandle) return false;
    nvs_handle_t h = nvsHandle.raw();

    if (nvs_erase_all(h) != ESP_OK) { log_i("saveWifiIntoNVS: failed to clear WiFi namespace"); }

    for (JsonObject wifiObj : wifiList) {
        String ssid = wifiObj["ssid"].as<String>();
        if (ssid.isEmpty()) continue;
        String encPwd = wifiPwdEncrypt(wifiObj["pwd"].as<String>());
        uint32_t crc = crc32(reinterpret_cast<const uint8_t *>(ssid.c_str()), ssid.length());

        bool ok = lnvs::setString(h, makeWifiKey('s', crc).c_str(), ssid.c_str());
        ok &= lnvs::setString(h, makeWifiKey('p', crc).c_str(), encPwd.c_str());
        ok &= nvs_set_u8(h, makeWifiKey('b', crc).c_str(), 1) == ESP_OK;
        if (!ok) { log_i("saveWifiIntoNVS: failed storing %s", ssid.c_str()); }
    }

    nvsHandle.commit();
    wifiListDirty = false;
    return true;
}

String loadSessionToken() { return lnvs::getString("launcher", "token", 64); }

void defaultValues() {
    // rotation = ROTATION;
#ifdef DIMMER_SETUP
    dimmerSet = DIMMER_SETUP;
#else
    dimmerSet = 20;
#endif
    bright = 100;
    onlyBins = true;
    bootToApp = true;
    bootTimer = 4;
    DDLB = false;
    LauncherOnKey = -1;
    LauncherKeyLvl = false;
    noDotFiles = true;
    askSpiffs = true;
    autoBackup = true;
#if defined(E_PAPER_DISPLAY)
    FGCOLOR = 0x0000;
    BGCOLOR = 0xFFFF;
    ALCOLOR = 0x8888;
    odd_color = 0x5555;
    even_color = 0x2222;
#else
    FGCOLOR = 0x07E0;
    BGCOLOR = 0x0000;
    ALCOLOR = 0xF800;
    odd_color = 0x30c5;
    even_color = 0x32e5;
#endif
    dev_mode = false;
    autoConnect = true;
    wui_usr = "admin";
    wui_pwd = "launcher";
    dwn_path = "/downloads/";
#if defined(HEADLESS)
    // SD Pins
    if (_miso < 0) _miso = 0;
    if (_mosi < 0) _mosi = 0;
    if (_sck < 0) _sck = 0;
    if (_cs < 0) _cs = 0;
#endif
    saveIntoNVS();
}
bool getFromNVS() {
    lnvs::Handle nvsHandle("launcher", false);
    if (!nvsHandle) {
        // If NVS read fails, set default values
        log_i("Failed to open the launcher namespace, using default values");
        defaultValues();
        return false;
    }
    nvs_handle_t h = nvsHandle.raw();
    lnvs::getInt(h, "dimtime", dimmerSet);
    lnvs::getInt(h, "bright", bright);
    lnvs::getBool(h, "onlyBins", onlyBins);
    lnvs::getBool(h, "bootToApp", bootToApp);
    nvs_get_u8(h, "bootTimer", &bootTimer);
    lnvs::getBool(h, "DDLB", DDLB);
    lnvs::getInt(h, "LauncherOnKey", LauncherOnKey);
    lnvs::getBool(h, "LauncherKeyLvl", LauncherKeyLvl);
    lnvs::getBool(h, "noDotFiles", noDotFiles);
    lnvs::getBool(h, "autoBackup", autoBackup);
    lnvs::getBool(h, "askSpiffs", askSpiffs);
    lnvs::getInt(h, "rotation", rotation);
    nvs_get_u16(h, "FGCOLOR", &FGCOLOR);
    nvs_get_u16(h, "BGCOLOR", &BGCOLOR);
    nvs_get_u16(h, "ALCOLOR", &ALCOLOR);
    nvs_get_u16(h, "odd_color", &odd_color);
    nvs_get_u16(h, "even_color", &even_color);
    lnvs::getBool(h, "dev_mode", dev_mode);
    lnvs::getBool(h, "auto_connect", autoConnect);
#if defined(HEADLESS)
    // SD Pins
    nvs_get_i8(h, "miso", &_miso);
    nvs_get_i8(h, "mosi", &_mosi);
    nvs_get_i8(h, "sck", &_sck);
    nvs_get_i8(h, "cs", &_cs);
#endif
    String value = lnvs::getString(h, "wui_usr", 63);
    if (!value.isEmpty()) wui_usr = value;
    value = lnvs::getString(h, "wui_pwd", 63);
    if (!value.isEmpty()) wui_pwd = value;
    value = lnvs::getString(h, "dwn_path", 63);
    if (!value.isEmpty()) dwn_path = value;
    lastInstalledApp = lnvs::getString(h, "last_app", 127);

    return true;
}
bool getWifiFromNVS(bool mergeExisting) {
    JsonArray wifiList = ensureWifiListInternal();
    if (wifiList.isNull()) return false;
    if (!mergeExisting) wifiList.clear();

    launcherConsolePrintln("NVS: Finding keys in NVS...");
    lnvs::Handle handle("l_wifi", false);
    if (!handle) return true; // nothing stored yet

    for (const String &key : lnvs::keys("l_wifi")) {
        if (!key.startsWith("s_")) continue;

        String ssid = lnvs::getString(handle.raw(), key.c_str());
        if (ssid.isEmpty()) {
            launcherConsolePrintf("Error retrieving %s\n", key.c_str());
            continue;
        }

        String suffix = key.substring(2);
        String pwdValue = lnvs::getString(handle.raw(), ("p_" + suffix).c_str());

        uint8_t isSecure = 0;
        nvs_get_u8(handle.raw(), ("b_" + suffix).c_str(), &isSecure);
        if (isSecure) pwdValue = wifiPwdDecrypt(pwdValue);

        setWifiCredential(ssid, pwdValue, false);
        launcherConsolePrintf("SSID: %s\n", ssid.c_str());
    }
    return true;
}

/*********************************************************************
**  Function: getConfigs
**  getConfigurations from EEPROM or JSON
**********************************************************************/
void getConfigs() {
    if (!setupSdCard()) {
        getFromNVS();
        getWifiFromNVS();
        return;
    }

    if (!SDM.exists(CONFIG_FILE)) {
        launcherConsolePrintln("getConfigs: config.conf not found, creating with defaults");
        defaultValues();
        saveConfigs();
        return;
    }

    File file = SDM.open(CONFIG_FILE, FILE_READ);
    if (!file) {
        launcherConsolePrintln("getConfigs: failed to open config.conf, resetting to defaults");
        defaultValues();
        saveConfigs();
        return;
    }

    favorite = JsonArray();
    DeserializationError error = deserializeJson(settings, file);
    file.close();

    if (error) {
        launcherConsolePrintf("getConfigs: parse error (%s), resetting to defaults", error.c_str());
        favorite = JsonArray();
        settings.clear();
        defaultValues();
        saveConfigs();
        return;
    }

    log_i("getConfigs: deserialized correctly");
    JsonObject setting = settings[0];
    int count = applySettingsFromRoot(setting);
    getWifiFromNVS(true);
    if (count > 0) saveConfigs();

    log_i("Brightness: %d", bright);
    setBrightness(bright);
    if (dimmerSet > 120) dimmerSet = 10;
    saveIntoNVS();
    log_i("getConfigs: loaded from config.conf");
}
/*********************************************************************
**  Function: saveConfigs
**  save configs into JSON config.conf file
**********************************************************************/
void saveConfigs() {
    if (!sdcardMounted) {
        saveIntoNVS();
        return;
    }

    // Get (or create) the root settings object — preserves any unknown keys
    JsonObject setting = ensureSettingsRoot();
    if (setting.isNull()) {
        launcherConsolePrintln("saveConfigs: failed to prepare settings root");
        saveIntoNVS();
        return;
    }

    populateSettingsFromGlobals(setting);

    // Ensure wifi array has at least a placeholder entry
    JsonArray wifiList = setting["wifi"].as<JsonArray>();
    if (!wifiList.isNull() && wifiList.size() == 0) {
        JsonObject wifiObj = wifiList.add<JsonObject>();
        if (!wifiObj.isNull()) {
            wifiObj["ssid"] = ssid.length() == 0 ? "myNetSSID" : ssid;
            wifiObj["pwd"] = pwd.length() == 0 ? "myNetPwd" : pwd;
        }
    }

    // Encrypt wifi passwords before writing to SD
    {
        JsonArray wl = setting["wifi"].as<JsonArray>();
        if (!wl.isNull()) {
            for (JsonObject e : wl) {
                if (!e["ssid"].as<String>().isEmpty()) {
                    e["pwd"] = wifiPwdEncrypt(e["pwd"].as<String>());
                    e["secure"] = true;
                }
            }
        }
    }

    // Overwrite config.conf directly (FILE_WRITE truncates on open)
    File file = SDM.open(CONFIG_FILE, FILE_WRITE, true);
    size_t written = 0;
    if (file) {
        written = serializeJsonPretty(settings, file);
        file.flush();
        file.close();
    } else {
        launcherConsolePrintln("saveConfigs: failed to open config.conf for writing");
        sdcardMounted = false;
    }

    // Restore plaintext passwords in memory immediately after write
    {
        JsonArray wl = setting["wifi"].as<JsonArray>();
        if (!wl.isNull()) {
            for (JsonObject e : wl) {
                if (e["secure"].as<bool>()) {
                    e["pwd"] = wifiPwdDecrypt(e["pwd"].as<String>());
                    e.remove("secure");
                }
            }
        }
    }

    if (written >= 5) {
        log_i("saveConfigs: config.conf written successfully");
    } else {
        sdcardMounted = false;
        launcherConsolePrintf("saveConfigs: write failed (written=%u)", written);
    }

    saveIntoNVS();
}

#if defined(HAS_RESISTIVE_TOUCH)
#include <CYD28_TouchscreenR.h>

constexpr const char *TOUCH_CAL_NAMESPACE = "touch_cal";

bool validTouchCalibration(uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1) {
    return x0 > 0 && x1 > 0 && y0 > 0 && y1 > 0 && x0 != x1 && y0 != y1;
}

esp_err_t readTouchCalibrationItems(
    nvs_handle_t handle, uint16_t &x0, uint16_t &x1, uint16_t &y0, uint16_t &y1, uint8_t &rot
) {
    esp_err_t err = nvs_get_u16(handle, "x0", &x0);
    err |= nvs_get_u16(handle, "x1", &x1);
    err |= nvs_get_u16(handle, "y0", &y0);
    err |= nvs_get_u16(handle, "y1", &y1);
    err |= nvs_get_u8(handle, "r", &rot);
    if (err == ESP_OK) return err;

    // Older builds used single-letter keys.
    err = nvs_get_u16(handle, "x", &x0);
    err |= nvs_get_u16(handle, "X", &x1);
    err |= nvs_get_u16(handle, "y", &y0);
    err |= nvs_get_u16(handle, "Y", &y1);
    err |= nvs_get_u8(handle, "r", &rot);
    return err;
}

bool getTouchCalibration(uint16_t &x0, uint16_t &x1, uint16_t &y0, uint16_t &y1, uint8_t &rot) {
    x0 = 0;
    x1 = 0;
    y0 = 0;
    y1 = 0;
    rot = 0;

    lnvs::Handle nvsHandle(TOUCH_CAL_NAMESPACE, false);
    if (!nvsHandle) {
        log_i("getTouchCalibration: no %s namespace found", TOUCH_CAL_NAMESPACE);
        return false;
    }

    esp_err_t err = readTouchCalibrationItems(nvsHandle.raw(), x0, x1, y0, y1, rot);
    rot &= 0x07;
    return err == ESP_OK && validTouchCalibration(x0, x1, y0, y1);
}

// Load touch calibration from NVS namespace "touch_cal"
// Returns true if calibration data is found, provides data via parameters
bool loadTouchCalibration() {
    uint16_t x0;
    uint16_t x1;
    uint16_t y0;
    uint16_t y1;
    uint8_t rot;

    if (!getTouchCalibration(x0, x1, y0, y1, rot)) {
        launcherConsolePrintln("loadTouchCalibration: Failed to load valid calibration data");
        return false;
    }

    uint16_t parameters[5] = {x0, x1, y0, y1, rot};
    extern CYD28_TouchR touch;
    touch.setTouch(parameters);
    launcherConsolePrintf(
        "loadTouchCalibration: Loaded calibration - x0:%u x1:%u y0:%u y1:%u rot:%u\n", x0, x1, y0, y1, rot
    );
    return true;
}

// Save touch calibration to NVS namespace "touch_cal"
bool saveTouchCalibration(uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1, uint8_t rot) {
    if (!validTouchCalibration(x0, x1, y0, y1)) {
        launcherConsolePrintf(
            "saveTouchCalibration: Invalid calibration - x0:%u x1:%u y0:%u y1:%u rot:%u\n",
            x0,
            x1,
            y0,
            y1,
            rot
        );
        return false;
    }

    lnvs::Handle nvsHandle(TOUCH_CAL_NAMESPACE, true);
    if (!nvsHandle) {
        launcherConsolePrintf("saveTouchCalibration: Failed to open %s namespace\n", TOUCH_CAL_NAMESPACE);
        return false;
    }
    nvs_handle_t h = nvsHandle.raw();

    rot &= 0x07;
    esp_err_t err = nvs_set_u16(h, "x0", x0);
    err |= nvs_set_u16(h, "x1", x1);
    err |= nvs_set_u16(h, "y0", y0);
    err |= nvs_set_u16(h, "y1", y1);
    err |= nvs_set_u8(h, "r", rot);

    if (err == ESP_OK && !nvsHandle.commit()) err = ESP_FAIL;

    if (err == ESP_OK) {
        launcherConsolePrintf(
            "saveTouchCalibration: Saved calibration - x0:%u x1:%u y0:%u y1:%u rot:%u\n", x0, x1, y0, y1, rot
        );
        return true;
    }

    launcherConsolePrintf(
        "saveTouchCalibration: Failed to save calibration data: %s\n", esp_err_to_name(err)
    );
    return false;
}
void calibrateTouch() {
    extern CYD28_TouchR touch; // Objeto declarado nos interface.cpp que usam essa biblioteca.
    tft->setRotation(0);
    tft->fillScreen(BGCOLOR);
    wakeUpScreen();
    int saved_dimmerSet = dimmerSet;
    dimmerSet = 0;
    const uint16_t _w = tft->width();
    const uint16_t _h = tft->height();

    struct RawTouchPoint {
        uint16_t x;
        uint16_t y;
    };

    auto drawCenteredLine = [&](const char *text, int16_t y) { tft->drawCentreString(text, _w / 2, y, 1); };

    tft->setTextColor(FGCOLOR, BGCOLOR);
    tft->setTextSize(_fp);
    const int16_t lineHeight = LH;
    int16_t y = (_h - lineHeight * 4) / 2;
    drawCenteredLine("Launcher Touch Calibration", y);
    y += lineHeight;
    drawCenteredLine("---------------------------", y);
    y += lineHeight;
    drawCenteredLine("Touch the screen corners", y);
    y += lineHeight;
    drawCenteredLine("indicated by the arrows", y);
    launcherDelayMs(500);

    auto drawArrow = [&](uint8_t corner) {
        tft->fillRect(0, 0, 30, 30, BGCOLOR);
        tft->fillRect(0, _h - 30, 30, 30, BGCOLOR);
        tft->fillRect(_w - 30, 0, 30, 30, BGCOLOR);
        tft->fillRect(_w - 30, _h - 30, 30, 30, BGCOLOR);
        const int16_t edge = 0;
        const int16_t len = 28;
        const int16_t head = 8;
        const bool right = corner == 1 || corner == 2;
        const bool bottom = corner >= 2;
        const int16_t x0 = right ? _w - edge : edge;
        const int16_t y0 = bottom ? _h - edge : edge;
        const int16_t sx = right ? -1 : 1;
        const int16_t sy = bottom ? -1 : 1;

        tft->drawLine(x0 + sx * len, y0 + sy * len, x0, y0, FGCOLOR);
        tft->drawLine(x0, y0, x0 + sx * head, y0, FGCOLOR);
        tft->drawLine(x0, y0, x0, y0 + sy * head, FGCOLOR);
        tft->drawLine(x0 + 1, y0 + sy, x0 + sx * (head + 1), y0 + sy, FGCOLOR);
        tft->drawLine(x0 + sx, y0 + 1, x0 + sx, y0 + sy * (head + 1), FGCOLOR);
    };

    auto readRawPoint = [&]() {
        while (touch.touched()) { launcherDelayMs(10); }
        while (!touch.touched()) { launcherDelayMs(10); }

        uint32_t sx = 0;
        uint32_t sy = 0;
        const uint8_t samples = 6;
        for (uint8_t i = 0; i < samples; ++i) {
            auto p = touch.getPointRaw();
            sx += p.x;
            sy += p.y;
            launcherDelayMs(18);
        }

        while (touch.touched()) { launcherDelayMs(10); }
        return RawTouchPoint{uint16_t(sx / samples), uint16_t(sy / samples)};
    };

    RawTouchPoint p[4];
    for (uint8_t i = 0; i < 4; ++i) {
        drawArrow(i);
        p[i] = readRawPoint();
    }

    const int32_t leftRawX = (int32_t(p[0].x) + p[3].x) / 2;
    const int32_t rightRawX = (int32_t(p[1].x) + p[2].x) / 2;
    const int32_t topRawX = (int32_t(p[0].x) + p[1].x) / 2;
    const int32_t bottomRawX = (int32_t(p[2].x) + p[3].x) / 2;

    const int32_t leftRawY = (int32_t(p[0].y) + p[3].y) / 2;
    const int32_t rightRawY = (int32_t(p[1].y) + p[2].y) / 2;
    const int32_t topRawY = (int32_t(p[0].y) + p[1].y) / 2;
    const int32_t bottomRawY = (int32_t(p[2].y) + p[3].y) / 2;

    const uint8_t swapXY = abs(rightRawX - leftRawX) < abs(rightRawY - leftRawY);
    const uint8_t invertX = swapXY ? p[0].y > p[1].y : p[0].x > p[1].x;
    const uint8_t invertY = swapXY ? p[0].x > p[3].x : p[0].y > p[3].y;
    // swapXY and invertX are inverted to keep compatibility with current interface.cpp files..
    // need to adjust orientation on the lib to some stardard code and adjust the interface.cpp of the devices
    // that uses it. I´m not doint it now because I dont have all affected devices, jusk have a brief
    // knowladge of what is currently working
    const uint8_t rot_true = swapXY | (invertX << 1) | (invertY << 2); // must use this in the future (or not)
    // remove ! in a near future, and fix the invertedY and X positions
    const uint8_t rot = !swapXY | (invertY << 1) | (!invertX << 2);
    uint16_t xMin = !swapXY ? p[0].y : p[0].x; // remove ! in a near furute
    uint16_t xMax = xMin;
    uint16_t yMin = !swapXY ? p[0].x : p[0].y; // remove ! in a near furute
    uint16_t yMax = yMin;
    for (uint8_t i = 1; i < 4; ++i) {
        const uint16_t rx = !swapXY ? p[i].y : p[i].x; // remove ! in a near furute
        const uint16_t ry = !swapXY ? p[i].x : p[i].y; // remove ! in a near furute
        if (rx < xMin) xMin = rx;
        if (rx > xMax) xMax = rx;
        if (ry < yMin) yMin = ry;
        if (ry > yMax) yMax = ry;
    }

    uint16_t parameters[5] = {xMin, xMax, yMin, yMax, rot};
    touch.setTouch(parameters);
    saveTouchCalibration(xMin, xMax, yMin, yMax, rot);

    launcherConsolePrintf(
        "calibrateTouch: x0:%u x1:%u y0:%u y1:%u rot:%u (rot_true:%u) swap:%u invX:%u invY:%u\n",
        xMin,
        xMax,
        yMin,
        yMax,
        rot,
        rot_true,
        swapXY,
        invertX,
        invertY
    );
    tft->setRotation(rotation);
    tft->fillScreen(BGCOLOR);
    wakeUpScreen();
    dimmerSet = saved_dimmerSet;
}
#endif
