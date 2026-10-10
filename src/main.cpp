// SmallTV — main orchestrator.
//
// Architecture:
//   core/    hardware + I/O (display, wifi, time, storage, web, OTA)
//   data/    external API clients (antigravity, codex, ...)
//   channels/ self-contained renderers, registered in kChannels[]
//
// Adding a channel = drop one .cpp into src/channels/ + add a row to kChannels[].

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <time.h>

#include "config.h"
#include "theme.h"
#include "display.h"
#include "storage.h"
#include "web.h"
#include "api.h"
#include "channel.h"

// Four page options; no unused channels are linked.
extern bool chHomeEnabled(const ChannelCtx&); extern void chHomeDraw(const ChannelCtx&); extern void chHomeTick(const ChannelCtx&);
extern bool chAntigravityEnabled(const ChannelCtx&); extern void chAntigravityDraw(const ChannelCtx&); extern void chAntigravityTick(const ChannelCtx&);
extern bool chInfoEnabled(const ChannelCtx&); extern void chInfoDraw(const ChannelCtx&); extern void chInfoTick(const ChannelCtx&);
extern bool chGalleryEnabled(const ChannelCtx&); extern void chGalleryDraw(const ChannelCtx&); extern void chGalleryTick(const ChannelCtx&);
static const Channel kChannels[] = {
    { "Home", chHomeEnabled, chHomeDraw, chHomeTick },
    { "Antigravity", chAntigravityEnabled, chAntigravityDraw, chAntigravityTick },
    { "Info", chInfoEnabled, chInfoDraw, chInfoTick },
    { "Gallery", chGalleryEnabled, chGalleryDraw, chGalleryTick },
};
static constexpr int kChannelCount = sizeof(kChannels) / sizeof(kChannels[0]);

// ── Globals ──────────────────────────────────────────────────────────────────

static Settings        g_settings;
static bool            g_apMode      = false;
static AntigravityData g_antigravity;

static int         g_activeIdx[kChannelCount];        // indices into kChannels[] that are currently enabled
static int         g_activeCount = 0;
static int         g_activePtr   = 0;     // which active channel is on screen

static uint32_t    g_lastRefresh = 0;
static uint32_t    g_lastSlide   = 0;
static bool g_refreshRequested = false;
static bool g_settingsDirty = false;

// ── Accessors for web.cpp / ch_info.cpp ─────────────────────────────────────

const char* mainActiveChannelName() {
    if (g_activeCount == 0) return "none";
    return kChannels[g_activeIdx[g_activePtr]].name;
}
int  mainEnabledCount()    { return g_activeCount; }
int  mainTotalCount()      { return kChannelCount; }
uint32_t mainLastRefreshMs() { return g_lastRefresh; }
uint32_t mainRefreshIntervalMs() { return (uint32_t)g_settings.refreshMin * 60000UL; }
void mainTriggerRefresh() { g_refreshRequested = true; }
void mainSettingsChanged() { g_settingsDirty = true; }
const char* mainEnabledChannelName(int idx) {
    if (idx < 0 || idx >= g_activeCount) return nullptr;
    return kChannels[g_activeIdx[idx]].name;
}
const AntigravityData* mainAntigravityData() { return &g_antigravity; }

// ── Helpers ──────────────────────────────────────────────────────────────────

static ChannelCtx makeCtx() {
    return ChannelCtx { &g_settings, &g_antigravity, millis() };
}

static void recomputeActive() {
    ChannelCtx ctx = makeCtx();
    g_activeCount = 0;
    for (int i = 0; i < kChannelCount && g_activeCount < kChannelCount; i++) {
        if (kChannels[i].enabled(ctx)) g_activeIdx[g_activeCount++] = i;
    }
    if (g_activePtr >= g_activeCount) g_activePtr = 0;
    if (!g_settings.autoRotate) {
        for (int i = 0; i < g_activeCount; ++i)
            if (g_activeIdx[i] == g_settings.selectedPage) g_activePtr = i;
    }
}

static void drawActive() {
    if (g_activeCount == 0) {
        Display::drawError("No channels", "Configure web UI");
        return;
    }
    // Re-arm splash/connecting/OTA partial-redraw state in case we re-enter
    // a system screen later (e.g., WiFi reconnect drops to drawConnecting).
    Display::resetSystemScreens();
    // Quiet cut: the channel's draw() clears + paints in ~80 ms.
    ChannelCtx ctx = makeCtx();
    kChannels[g_activeIdx[g_activePtr]].draw(ctx);
}

// 2-px progress strip at y=230. Fills in current channel's theme color as
// the slide window elapses. Hidden on the full-screen gallery.
static void drawIndicator(uint32_t now) {
    using namespace Layout;
    if (g_apMode || g_activeCount <= 1) return;
    const char* name = kChannels[g_activeIdx[g_activePtr]].name;
    if (!g_settings.autoRotate || !strcmp(name, "Gallery")) return;

    uint32_t slideMs = (uint32_t)g_settings.channelSec * 1000UL;
    uint32_t elapsed = now - g_lastSlide;
    if (elapsed > slideMs) elapsed = slideMs;
    int w = (int)((uint64_t)SCREEN_W * elapsed / slideMs);

    tft.fillRect(0, INDICATOR_Y, SCREEN_W, INDICATOR_H, Theme::PANEL);
    if (w > 0) tft.fillRect(0, INDICATOR_Y, w, INDICATOR_H, Theme::channelColor(name));
}

// ── WiFi orchestration ───────────────────────────────────────────────────────

static bool tryConnect() {
    if (g_settings.wifiSSID.isEmpty()) return false;

    // Multi-attempt connect with clean state between tries. Total budget ~3.5 min
    // before falling back to AP mode. Each attempt: 60 s with full disconnect+mode-flip.
    constexpr int ATTEMPTS    = 3;
    constexpr int ATTEMPT_TICKS = 120;     // 120 × 500 ms = 60 s per attempt

    for (int attempt = 1; attempt <= ATTEMPTS; attempt++) {
        WiFi.persistent(false);
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        delay(150);
        WiFi.mode(WIFI_STA);
        WiFi.setSleepMode(WIFI_NONE_SLEEP);
        WiFi.setOutputPower(20.5);
        WiFi.setAutoReconnect(true);
        WiFi.hostname(MDNS_HOSTNAME);
        delay(100);

        Serial.printf("[wifi] attempt %d/%d: connecting to '%s'\n",
                      attempt, ATTEMPTS, g_settings.wifiSSID.c_str());
        WiFi.begin(g_settings.wifiSSID.c_str(), g_settings.wifiPass.c_str());

        for (int i = 0; i < ATTEMPT_TICKS; i++) {
            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("[wifi] connected, IP=%s, RSSI=%d\n",
                              WiFi.localIP().toString().c_str(), WiFi.RSSI());
                return true;
            }
            Display::drawConnecting(g_settings.wifiSSID.c_str(), i + (attempt - 1) * ATTEMPT_TICKS);
            delay(500);
        }
        Serial.printf("[wifi] attempt %d timed out (status=%d), backing off...\n",
                      attempt, WiFi.status());
        delay(2000);
    }
    Serial.println(F("[wifi] all attempts failed — falling back to AP mode"));
    return false;
}

static void startAPMode() {
    g_apMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(SETUP_AP_SSID);
    Display::drawSetupMode(SETUP_AP_SSID, WiFi.softAPIP().toString().c_str());
}

// ── Refresh cycle ────────────────────────────────────────────────────────────

static void refreshAll() {
    if (WiFi.status() != WL_CONNECTED) return;
    Api::fetchAntigravity(g_settings, g_antigravity);
    recomputeActive();
    drawActive();
}

// ── Setup / loop ─────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Serial.println(F("\n=== SmallTV v" FW_VERSION " ==="));

    Display::begin();
    Display::drawSplash("booting...");
    delay(400);

    Storage::begin();
    g_settings = Storage::load();
    Display::setBrightness(g_settings.brightness);
    Display::setInvert(g_settings.invertDisplay);

    Display::drawSplash("connecting WiFi");
    if (!tryConnect()) {
        startAPMode();
    } else {
        MDNS.begin(MDNS_HOSTNAME);
        Display::drawSplash("Syncing time");
        configTime(0, 0, "ntp.aliyun.com", "ntp.tencent.com", "pool.ntp.org");
        // POSIX TZ expresses signed minutes east of UTC as the
        // "minutes to ADD to local time to get UTC" → invert the sign.
        int signedMin = g_settings.tzMinutes;
        int posixMin = -signedMin;
        char tzBuf[20];
        snprintf(tzBuf, sizeof(tzBuf), "UTC%c%d:%02d", posixMin < 0 ? '-' : '+', abs(posixMin) / 60, abs(posixMin) % 60);
        setenv("TZ", tzBuf, 1);
        tzset();
        for (int i = 0; i < 20 && time(nullptr) < 1000000000L; i++) delay(250);
    }

    Web::begin(g_settings);

    if (!g_apMode) {
        Display::drawSplash("Fetching...");
        refreshAll();
        g_lastRefresh = millis();
        g_lastSlide   = millis();
    }
}

void loop() {
    Web::loop();
    if (!g_apMode) MDNS.update();

    uint32_t now = millis();

    // WiFi health check. "Healthy" means associated AND holding a DHCP lease.
    // A DHCP lease-renewal failure (common after ~10-12 h) leaves the link
    // associated — WiFi.status() stays WL_CONNECTED — but with IP 0.0.0.0, so
    // a status-only check never notices and the device silently stops talking
    // to the network. We treat "no IP" as lost too.
    //
    // Recovery is ALWAYS in-place (full RF reset, mirroring tryConnect()) and
    // never ESP.restart(): a software restart does NOT reset the ESP8266 WiFi
    // RF/calibration, so the post-restart connect reliably fails and the device
    // drops into AP mode — stuck there until a physical power-cycle. Retrying in
    // STA forever recovers cleanly whenever WiFi/DHCP returns. The 30 s arm
    // debounces brief glitches so a momentary blip doesn't bounce the radio.
    static uint32_t lastWifiCheck = 0;
    static uint32_t wifiRetrySince = 0;
    if (!g_apMode && now - lastWifiCheck >= 10000) {
        lastWifiCheck = now;
        bool healthy = (WiFi.status() == WL_CONNECTED)
                    && (WiFi.localIP() != IPAddress(0U));
        if (!healthy) {
            if (wifiRetrySince == 0) {
                wifiRetrySince = now;
                Serial.printf("[wifi] unhealthy (status=%d ip=%s), waiting...\n",
                              WiFi.status(), WiFi.localIP().toString().c_str());
            } else if (now - wifiRetrySince >= 30000) {
                Serial.println(F("[wifi] reconnecting (full RF reset)..."));
                WiFi.persistent(false);
                WiFi.disconnect(true);
                WiFi.mode(WIFI_OFF);
                delay(150);
                WiFi.mode(WIFI_STA);
                WiFi.setSleepMode(WIFI_NONE_SLEEP);
                WiFi.setAutoReconnect(true);
                WiFi.hostname(MDNS_HOSTNAME);
                WiFi.begin(g_settings.wifiSSID.c_str(), g_settings.wifiPass.c_str());
                wifiRetrySince = now;
            }
        } else {
            if (wifiRetrySince != 0)
                Serial.println(F("[wifi] reconnected"));
            wifiRetrySince = 0;
        }
    }

    // Periodic API refresh
    uint32_t refreshMs = (uint32_t)g_settings.refreshMin * 60000UL;
    if (!g_apMode && (g_refreshRequested || now - g_lastRefresh >= refreshMs)) {
        g_refreshRequested = false;
        g_lastRefresh = now;
        tft.fillCircle(SCREEN_W - 6, 6, 3, Theme::CORAL);
        refreshAll();
        g_lastSlide = now;
    }

    // Periodic NTP retry if time is still unsynced
    static uint32_t lastNtpRetry = 0;
    if (!g_apMode && time(nullptr) < 1000000000L && now - lastNtpRetry >= 10000) {
        lastNtpRetry = now;
        configTime(0, 0, "ntp.aliyun.com", "ntp.tencent.com", "pool.ntp.org");
    }

    // Apply saved page choices immediately, including when rotation is disabled.
    if (!g_apMode && g_settingsDirty) {
        g_settingsDirty = false;
        recomputeActive();
        for (int i = 0; i < g_activeCount; ++i)
            if (g_activeIdx[i] == g_settings.selectedPage) g_activePtr = i;
        drawActive();
        g_lastSlide = now;
    }

    // Channel auto-rotate — instant cut (no transition animation). Premium
    // devices do this; the single fillScreen+redraw in drawActive() is the
    // only flash, and it lasts <30 ms.
    uint32_t slideMs = (uint32_t)g_settings.channelSec * 1000UL;
    if (!g_apMode && g_settings.autoRotate && g_activeCount > 1
        && now - g_lastSlide >= slideMs) {
        g_lastSlide = now;
        // Pick up any settings toggles before deciding what's next.
        recomputeActive();
        if (g_activeCount > 0) g_activePtr = (g_activePtr + 1) % g_activeCount;
        drawActive();
    }

    // Partial-redraw tick — channels with a tick() callback are called at 5 Hz
    // (every 200 ms) to update only the regions that changed. NO full redraw
    // here — channels handle their own minimal repaints (see channel.h's
    // PARTIAL REDRAW DISCIPLINE comment).
    static uint32_t lastTick = 0;
    if (!g_apMode && g_activeCount > 0 && now - lastTick >= 200) {
        lastTick = now;
        auto tickFn = kChannels[g_activeIdx[g_activePtr]].tick;
        if (tickFn) tickFn(makeCtx());
    }

    // Indicator strip refreshes ~4 Hz (touches only y=230..231)
    static uint32_t lastInd = 0;
    if (!g_apMode && now - lastInd >= 250) {
        lastInd = now;
        drawIndicator(now);
    }

    delay(10);
}
