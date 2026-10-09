#include "storage.h"
#include <LittleFS.h>
#include <algorithm>

static const char* CFG_PATH = "/config.json";
void Storage::begin() { LittleFS.begin(); }
void Storage::normalize(Settings& s) {
    s.refreshMin = std::max(1UL, std::min(60UL, (unsigned long)s.refreshMin));
    s.channelSec = std::max(3UL, std::min(3600UL, (unsigned long)s.channelSec));
    s.galleryRefreshSec = std::max(1UL, std::min(86400UL, (unsigned long)s.galleryRefreshSec));
    s.brightness = std::max(5, std::min(100, (int)s.brightness));
    s.tzMinutes = std::max(-720, std::min(840, (int)s.tzMinutes));
    s.selectedPage = std::min(3, (int)s.selectedPage);
    if (!s.showHome && !s.showAntigravity && !s.showInfo && !s.showGallery) s.showInfo = true;
}
Settings Storage::load() {
    Settings s;
    File f = LittleFS.open(CFG_PATH, "r");
    if (!f) return s;
    JsonDocument doc;
    auto err = deserializeJson(doc, f);
    f.close();
    if (err || !doc.is<JsonObject>()) return s;
    s.wifiSSID = doc["wifi_ssid"] | "";
    s.wifiPass = doc["wifi_pass"] | "";
    s.agToken = doc["ag_token"] | "";
    s.agModelLabel = doc["ag_model"] | "Gemini";
    s.codexToken = doc["codex_token"] | "";
    s.codexDeviceId = doc["codex_dev"] | "";
    s.refreshMin = doc["refresh_min"] | 5;
    s.channelSec = doc["channel_sec"] | 8;
    s.galleryRefreshSec = doc["gallery_refresh_sec"] | 10;
    s.brightness = doc["brightness"] | 80;
    s.selectedPage = doc["selected_page"] | 0;
    s.showHome = doc["show_home"] | true;
    s.showAntigravity = doc["show_antigravity"] | true;
    s.showInfo = doc["show_info"] | true;
    s.showGallery = doc["show_gallery"] | true;
    s.autoRotate = doc["auto_rotate"] | true;
    s.agWeeklyHero = doc["ag_weekly_hero"] | false;
    s.codexWeeklyHero = doc["codex_weekly_hero"] | false;
    s.invertDisplay = doc["invert_display"] | true;
    // Preserve UTC (0) as a valid offset; migrate older hour-only backups.
    s.tzMinutes = doc["tz_minutes"].is<int>() ? doc["tz_minutes"].as<int>()
                  : (doc["tz_offset"].is<int>() ? doc["tz_offset"].as<int>() * 60 : 480);
    if (doc["tz_offset"].is<int>() && doc["tz_minutes"].as<int>() == 0 && doc["tz_offset"].as<int>() != 0)
        s.tzMinutes = doc["tz_offset"].as<int>() * 60;
    Storage::normalize(s);
    return s;
}
bool Storage::save(const Settings& s) {
    JsonDocument doc;
    doc["wifi_ssid"] = s.wifiSSID;
    doc["wifi_pass"] = s.wifiPass;
    doc["ag_token"] = s.agToken;
    doc["ag_model"] = s.agModelLabel;
    doc["codex_token"] = s.codexToken;
    doc["codex_dev"] = s.codexDeviceId;
    doc["refresh_min"] = s.refreshMin;
    doc["channel_sec"] = s.channelSec;
    doc["gallery_refresh_sec"] = s.galleryRefreshSec;
    doc["brightness"] = s.brightness;
    doc["selected_page"] = s.selectedPage;
    doc["show_home"] = s.showHome;
    doc["show_antigravity"] = s.showAntigravity;
    doc["show_info"] = s.showInfo;
    doc["show_gallery"] = s.showGallery;
    doc["auto_rotate"] = s.autoRotate;
    doc["ag_weekly_hero"] = s.agWeeklyHero;
    doc["codex_weekly_hero"] = s.codexWeeklyHero;
    doc["invert_display"] = s.invertDisplay;
    doc["tz_minutes"] = s.tzMinutes;
    File f = LittleFS.open("/config.tmp", "w");
    if (!f) return false;
    auto n = serializeJson(doc, f);
    f.close();
    if (!n) { LittleFS.remove("/config.tmp"); return false; }
    return LittleFS.rename("/config.tmp", CFG_PATH);
}
void Storage::factoryReset() { LittleFS.remove(CFG_PATH); }
