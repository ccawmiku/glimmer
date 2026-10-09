#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Only settings used by the four retained pages and device setup.
struct Settings {
    String wifiSSID, wifiPass;
    String agToken, agModelLabel;
    String codexToken, codexDeviceId;
    uint32_t refreshMin = 5;
    uint32_t channelSec = 8;
    uint32_t galleryRefreshSec = 10;
    uint8_t brightness = 80;
    int16_t tzMinutes = 480;
    uint8_t selectedPage = 0; // Home / Antigravity / Info / Gallery
    bool showHome = true;
    bool showAntigravity = true;
    bool showInfo = true;
    bool showGallery = true;
    bool autoRotate = true;
    bool agWeeklyHero = false;
    bool codexWeeklyHero = false;
    bool invertDisplay = true;
};

namespace Storage {
    void begin();
    Settings load();
    void normalize(Settings& s);
    bool save(const Settings& s);
    void factoryReset();
}
