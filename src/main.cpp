// glimmer — main orchestrator.
//
// Architecture:
//   core/    hardware + I/O (display, wifi, time, storage, web, OTA)
//   data/    external API clients (antigravity, codex, weather, status) + history
//   channels/ self-contained renderers, registered in kChannels[]
//
// Adding a channel = drop one .cpp into src/channels/ + add a row to kChannels[].
//
// Fetching is a staggered job scheduler, not a blocking "refresh all": each
// loop pass starts at most one job, jobs are spaced ≥ 3 s apart, and a TLS job
// only starts when the largest free heap block clears Api::kTlsFloor. The web
// server, channel ticks and Wi-Fi checks keep running between jobs.

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
#include "weather.h"
#include "history.h"
#include "vendor_status.h"
#include "channel.h"
#include "night_core.h"
#include "chrome.h"
#include "attention.h"

// ── Channel registry — declared in their own .cpp files ─────────────────────
extern bool chAntigravityEnabled(const ChannelCtx&); extern void chAntigravityDraw(const ChannelCtx&);
extern bool chCodexEnabled (const ChannelCtx&);  extern void chCodexDraw (const ChannelCtx&);
extern bool chClockEnabled (const ChannelCtx&);  extern void chClockDraw (const ChannelCtx&);
extern bool chInfoEnabled  (const ChannelCtx&);  extern void chInfoDraw  (const ChannelCtx&);
extern bool chWeatherEnabled (const ChannelCtx&); extern void chWeatherDraw (const ChannelCtx&);
extern bool chAttentionEnabled(const ChannelCtx&); extern void chAttentionDraw(const ChannelCtx&);
extern bool chAgentsEnabled  (const ChannelCtx&); extern void chAgentsDraw  (const ChannelCtx&);
extern bool chHomeEnabled    (const ChannelCtx&); extern void chHomeDraw    (const ChannelCtx&);
extern bool chAiDashEnabled  (const ChannelCtx&); extern void chAiDashDraw  (const ChannelCtx&);
extern bool chForecastEnabled(const ChannelCtx&); extern void chForecastDraw(const ChannelCtx&);
extern bool chTrendEnabled   (const ChannelCtx&); extern void chTrendDraw   (const ChannelCtx&);
extern bool chNightEnabled   (const ChannelCtx&); extern void chNightDraw   (const ChannelCtx&);
extern bool chSetupEnabled   (const ChannelCtx&); extern void chSetupDraw   (const ChannelCtx&);
extern void chAttentionTick  (const ChannelCtx&);
extern void chAgentsTick     (const ChannelCtx&);
extern void chClockTick      (const ChannelCtx&);
extern void chHomeTick       (const ChannelCtx&);
extern void chAntigravityTick(const ChannelCtx&);
extern void chCodexTick      (const ChannelCtx&);
extern void chAiDashTick     (const ChannelCtx&);
extern void chWeatherTick    (const ChannelCtx&);
extern void chForecastTick   (const ChannelCtx&);
extern void chTrendTick      (const ChannelCtx&);
extern void chInfoTick       (const ChannelCtx&);
extern void chNightTick      (const ChannelCtx&);

static const Channel kChannels[] = {
    //  name          enabled                 draw                 tick
    { "Attention",    chAttentionEnabled,    chAttentionDraw,    chAttentionTick    },
    { "Night",        chNightEnabled,        chNightDraw,        chNightTick        },
    { "Setup",        chSetupEnabled,        chSetupDraw,        nullptr            },
    { "Home",         chHomeEnabled,         chHomeDraw,         chHomeTick         },
    { "Agents",       chAgentsEnabled,       chAgentsDraw,       chAgentsTick       },
    { "Antigravity",  chAntigravityEnabled,  chAntigravityDraw,  chAntigravityTick  },
    { "Codex",        chCodexEnabled,        chCodexDraw,        chCodexTick        },
    { "AI",           chAiDashEnabled,       chAiDashDraw,       chAiDashTick       },
    { "Trend",        chTrendEnabled,        chTrendDraw,        chTrendTick        },
    { "Weather",      chWeatherEnabled,      chWeatherDraw,      chWeatherTick      },
    { "Forecast",     chForecastEnabled,     chForecastDraw,     chForecastTick     },
    { "Clock",        chClockEnabled,        chClockDraw,        chClockTick        },
    { "Info",         chInfoEnabled,         chInfoDraw,         chInfoTick         },
};
static constexpr int kChannelCount = sizeof(kChannels) / sizeof(kChannels[0]);

// ── Globals ──────────────────────────────────────────────────────────────────

static Settings        g_settings;
static bool            g_apMode      = false;
static AntigravityData g_antigravity;
static CodexData       g_codex;

static int         g_activeIdx[kChannelCount];   // indices into kChannels[] that are enabled
static int         g_activeCount = 0;
static int         g_activePtr   = 0;     // which active channel is on screen

static uint32_t    g_lastRefresh = 0;     // millis of the last completed fetch job
static uint32_t    g_lastSlide   = 0;
static bool        g_nightFace   = false; // night window + mode clock/dark

// ── Fetch scheduler state ────────────────────────────────────────────────────

enum Job : uint8_t { JOB_ANTIGRAVITY, JOB_CODEX, JOB_CODEX_RESETS, JOB_WEATHER,
                     JOB_STATUS_OPENAI, JOB_COUNT };
static const char* kJobName[JOB_COUNT] = {"antigravity", "codex", "codex-resets", "weather", "status-openai"};

static uint32_t g_jobDue[JOB_COUNT];      // millis when due (0 = now)
static uint32_t g_lastJobStart = 0;
static uint32_t g_heapRefusals = 0;       // jobs deferred by the heap gate
static uint32_t g_minMaxBlk    = 0xFFFFFFFF;

// ── Accessors for web.cpp / channels ────────────────────────────────────────

const char* mainActiveChannelName() {
    if (g_activeCount == 0) return "none";
    return kChannels[g_activeIdx[g_activePtr]].name;
}
int  mainEnabledCount()    { return g_activeCount; }
int  mainTotalCount()      { return kChannelCount; }
uint32_t mainLastRefreshMs() { return g_lastRefresh; }
const char* mainEnabledChannelName(int idx) {
    if (idx < 0 || idx >= g_activeCount) return nullptr;
    return kChannels[g_activeIdx[idx]].name;
}
const AntigravityData* mainAntigravityData() { return &g_antigravity; }
const CodexData*       mainCodexData()       { return &g_codex; }
bool     mainNightFace()    { return g_nightFace; }
bool     mainPinApprovals()   { return g_settings.pinApprovals; }
bool     mainAgentNightShow() { return g_settings.agentNightShow; }
uint32_t mainApprovalTtlS()   { return (uint32_t)g_settings.approvalTtlMin * 60UL; }
bool     mainAgentDoneCards() { return g_settings.agentDoneCards; }
uint32_t mainHeapRefusals() { return g_heapRefusals; }
uint32_t mainMinMaxBlock()  { return g_minMaxBlk; }

// ── Helpers ──────────────────────────────────────────────────────────────────

static ChannelCtx makeCtx() {
    return ChannelCtx { &g_settings, &g_antigravity, &g_codex, millis() };
}

static bool timeSynced() { return time(nullptr) > 1000000000L; }

static bool nightNow() {
    if (!timeSynced() || g_settings.nightMode == Night::NONE) return false;
    time_t t = time(nullptr);
    struct tm tm; localtime_r(&t, &tm);
    return Night::inWindow(tm.tm_hour * 60 + tm.tm_min,
                           g_settings.nightStartMin, g_settings.nightEndMin);
}

static void applyBrightness() {
    ChannelCtx ctx = makeCtx();
    bool night = nightNow();
    Display::setBrightness(Night::brightness((Night::Mode)g_settings.nightMode, night,
                                             chAttentionEnabled(ctx), g_settings.brightness,
                                             g_settings.nightBright));
}

static void recomputeActive() {
    const char* cur = g_activeCount ? kChannels[g_activeIdx[g_activePtr]].name : nullptr;
    g_nightFace = nightNow() && g_settings.nightMode >= Night::CLOCK;
    ChannelCtx ctx = makeCtx();
    g_activeCount = 0;
    for (int i = 0; i < kChannelCount; i++) {
        if (g_nightFace && strcmp(kChannels[i].name, "Night") && strcmp(kChannels[i].name, "Attention"))
            continue;
        if (kChannels[i].enabled(ctx)) g_activeIdx[g_activeCount++] = i;
    }
    g_activePtr = 0;
    for (int i = 0; cur && i < g_activeCount; i++)
        if (!strcmp(kChannels[g_activeIdx[i]].name, cur)) { g_activePtr = i; break; }
}

static void drawActive() {
    if (g_activeCount == 0) {
        Display::drawError("No channels", "Configure web UI");
        return;
    }
    Display::resetSystemScreens();
    ChannelCtx ctx = makeCtx();
    kChannels[g_activeIdx[g_activePtr]].draw(ctx);
}

static void refreshScreen(bool force) {
    const char* before = g_activeCount ? kChannels[g_activeIdx[g_activePtr]].name : nullptr;
    int countBefore = g_activeCount;
    recomputeActive();
    const char* after = g_activeCount ? kChannels[g_activeIdx[g_activePtr]].name : nullptr;
    if (force || before != after || countBefore != g_activeCount) {
        drawActive();
        g_lastSlide = millis();
    }
}

static void drawIndicator(uint32_t now) {
    using namespace Layout;
    if (g_apMode || g_activeCount == 0) return;
    const Attention::Queue& aq = AttentionQueue::get();
    if (Attention::countWaiting(aq, AttentionQueue::now()) > 0) {
        uint16_t c = Attention::countKind(aq, Attention::K_APPROVAL) ? Theme::AMBER : Theme::SKY;
        tft.fillRect(0, INDICATOR_Y, SCREEN_W, INDICATOR_H, c);
        return;
    }
    const char* name = kChannels[g_activeIdx[g_activePtr]].name;
    if (g_nightFace || !strcmp(name, "Attention")) {
        tft.fillRect(0, INDICATOR_Y, SCREEN_W, INDICATOR_H, Theme::BG);
        return;
    }
    if (g_activeCount <= 1) return;

    int w = 0;
    if (g_settings.autoRotate) {
        uint32_t slideMs = (uint32_t)g_settings.channelSec * 1000UL;
        uint32_t elapsed = now - g_lastSlide;
        if (elapsed > slideMs) elapsed = slideMs;
        w = (int)((uint64_t)SCREEN_W * elapsed / slideMs);
    }
    tft.fillRect(0, INDICATOR_Y, SCREEN_W, INDICATOR_H, Theme::PANEL);
    if (w > 0) tft.fillRect(0, INDICATOR_Y, w, INDICATOR_H, Theme::channelColor(name));
}

bool mainShowChannel(const char* name) {
    recomputeActive();
    for (int i = 0; i < g_activeCount; i++) {
        if (!strcasecmp(kChannels[g_activeIdx[i]].name, name)) {
            g_activePtr = i;
            drawActive();
            g_lastSlide = millis();
            return true;
        }
    }
    return false;
}

void mainNextChannel() {
    recomputeActive();
    if (g_activeCount == 0) return;
    g_activePtr = (g_activePtr + 1) % g_activeCount;
    drawActive();
    g_lastSlide = millis();
}

// ── Fetch scheduler ──────────────────────────────────────────────────────────

static bool jobEnabled(Job j) {
    switch (j) {
        case JOB_ANTIGRAVITY:  return !g_settings.agToken.isEmpty();
        case JOB_CODEX:        return !g_settings.codexToken.isEmpty();
        case JOB_CODEX_RESETS: return g_codex.cred == Cred::OK || g_codex.cred == Cred::EXPIRING;
        case JOB_WEATHER:      return Weather::configured(g_settings)
                                  && (g_settings.showWeather || g_settings.showHome || g_settings.showForecast);
        case JOB_STATUS_OPENAI: return g_settings.showStatus && !g_settings.codexToken.isEmpty();
        default: return false;
    }
}

static uint32_t jobIntervalMs(Job j) {
    switch (j) {
        case JOB_WEATHER:       return Weather::intervalMin(g_settings) * 60000UL;
        case JOB_CODEX_RESETS:  return 60UL * 60000UL;
        case JOB_STATUS_OPENAI: return VendorStatus::kIntervalMin * 60000UL;
        default:                return g_settings.refreshMin * 60000UL;
    }
}

static const FetchPolicy::State& jobPolicy(Job j) {
    switch (j) {
        case JOB_ANTIGRAVITY:   return Api::antigravityPolicy();
        case JOB_CODEX:
        case JOB_CODEX_RESETS:  return Api::codexPolicy();
        case JOB_WEATHER:       return Weather::policy();
        default:                return VendorStatus::policy(VendorStatus::OPENAI);
    }
}

void mainTriggerRefresh() {
    uint32_t now = millis();
    for (auto& d : g_jobDue) d = now;
}

uint32_t mainNextFetchInMs() {
    uint32_t now = millis(), best = 0xFFFFFFFF;
    for (int j = 0; j < JOB_COUNT; j++) {
        if (!jobEnabled((Job)j)) continue;
        int32_t in = (int32_t)(g_jobDue[j] - now);
        uint32_t v = in > 0 ? (uint32_t)in : 0;
        if (v < best) best = v;
    }
    return best == 0xFFFFFFFF ? 0 : best;
}

static uint16_t usageShape(bool valid, const char* err, Cred cred) {
    return (valid ? 1 : 0) | (err[0] ? 2 : 0) | ((uint16_t)cred << 2);
}

static void runJob(Job j) {
    uint16_t before = 0, after = 0;
    bool ok = false;
    switch (j) {
        case JOB_ANTIGRAVITY:
            before = usageShape(g_antigravity.valid, g_antigravity.err, g_antigravity.cred);
            ok = Api::fetchAntigravity(g_settings, g_antigravity);
            Api::refreshCred(g_settings, g_antigravity, g_codex);
            after = usageShape(g_antigravity.valid, g_antigravity.err, g_antigravity.cred);
            break;
        case JOB_CODEX:
            before = usageShape(g_codex.valid, g_codex.err, g_codex.cred);
            ok = Api::fetchCodex(g_settings, g_codex);
            Api::refreshCred(g_settings, g_antigravity, g_codex);
            after = usageShape(g_codex.valid, g_codex.err, g_codex.cred);
            break;
        case JOB_CODEX_RESETS:
            ok = Api::fetchCodexResets(g_settings, g_codex);
            break;
        case JOB_WEATHER: {
            const WeatherData& w = Weather::snapshot();
            before = usageShape(w.valid, w.err, Cred::OK);
            ok = Weather::fetch(g_settings);
            after = usageShape(w.valid, w.err, Cred::OK);
            break;
        }
        case JOB_STATUS_OPENAI: ok = VendorStatus::fetch(VendorStatus::OPENAI); break;
        default: break;
    }
    if (ok && (j == JOB_ANTIGRAVITY || j == JOB_CODEX)) UsageHistory::record(g_antigravity, g_codex);

    uint32_t now = millis();
    const FetchPolicy::State& pol = jobPolicy(j);
    uint32_t waitMs = (ok || j == JOB_CODEX_RESETS) ? jobIntervalMs(j)
                         : (pol.waitS ? pol.waitS * 1000UL : jobIntervalMs(j));
    g_jobDue[j] = now + waitMs;
    g_lastRefresh = now;
    Serial.printf_P(PSTR("[sched] %s %s → next in %lus (heap=%u maxblk=%u)\n"), kJobName[j],
                  ok ? "ok" : "fail", (unsigned long)(waitMs / 1000),
                  ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());
    refreshScreen(before != after);
}

static void schedulerTick(uint32_t now) {
    if (g_apMode || WiFi.status() != WL_CONNECTED) return;
    if (now - g_lastJobStart < 3000) return;
    for (int i = 0; i < JOB_COUNT; i++) {
        Job j = (Job)i;
        if (!jobEnabled(j) || (int32_t)(now - g_jobDue[j]) < 0) continue;
        uint32_t blk = ESP.getMaxFreeBlockSize();
        if (blk < g_minMaxBlk) g_minMaxBlk = blk;
        if (j != JOB_WEATHER && blk < Api::kTlsFloor) {
            g_jobDue[j] = now + 5000;
            g_heapRefusals++;
            Serial.printf_P(PSTR("[sched] %s deferred: maxblk=%u < %u\n"), kJobName[j], blk,
                          (unsigned)Api::kTlsFloor);
            return;
        }
        g_lastJobStart = now;
        tft.fillRect(SCREEN_W - 5, 224, 3, 3, Theme::BLUE);
        runJob(j);
        tft.fillRect(SCREEN_W - 5, 224, 3, 3, Theme::BG);
        return;
    }
}

// ── WiFi orchestration ───────────────────────────────────────────────────────

static void prepareSta(WiFiMode_t mode) {
    WiFi.persistent(false);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(150);
    WiFi.mode(mode);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    WiFi.setOutputPower(20.5);
    WiFi.setAutoReconnect(true);
    WiFi.hostname(MDNS_HOSTNAME);
    delay(100);
}

static bool tryConnect() {
    if (g_settings.wifiSSID.isEmpty()) return false;

    constexpr int ATTEMPTS    = 2;
    constexpr int ATTEMPT_TICKS = 60;      // 60 × 500 ms = 30 s per attempt

    for (int attempt = 1; attempt <= ATTEMPTS; attempt++) {
        prepareSta(WIFI_STA);
        Serial.printf_P(PSTR("[wifi] attempt %d/%d: connecting to '%s'\n"), attempt, ATTEMPTS, g_settings.wifiSSID.c_str());
        WiFi.begin(g_settings.wifiSSID.c_str(), g_settings.wifiPass.c_str());

        for (int i = 0; i < ATTEMPT_TICKS; i++) {
            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf_P(PSTR("[wifi] connected, IP=%s, RSSI=%d\n"), WiFi.localIP().toString().c_str(), WiFi.RSSI());
                return true;
            }
            Display::drawConnecting(g_settings.wifiSSID.c_str(), i + (attempt - 1) * ATTEMPT_TICKS);
            delay(500);
        }
        Serial.printf_P(PSTR("[wifi] attempt %d timed out (status=%d), backing off...\n"), attempt, WiFi.status());
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

static void onStaUp() {
    MDNS.begin(MDNS_HOSTNAME);
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    Storage::applyTimezone(g_settings);
    for (int i = 0; i < 20 && !timeSynced(); i++) delay(250);
    mainTriggerRefresh();
}

static void apStrandRetry(uint32_t now) {
    static uint32_t lastTry = 0, tryStart = 0;
    if (!g_apMode || g_settings.wifiSSID.isEmpty()) return;
    if (tryStart == 0) {
        if (lastTry != 0 && now - lastTry < 180000UL) return;
        if (lastTry == 0) { lastTry = now; return; }
        Serial.println(F("[wifi] AP-strand: retrying saved network"));
        WiFi.mode(WIFI_AP_STA);
        WiFi.begin(g_settings.wifiSSID.c_str(), g_settings.wifiPass.c_str());
        tryStart = now;
        return;
    }
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0U)) {
        Serial.printf_P(PSTR("[wifi] AP-strand: joined, IP=%s\n"), WiFi.localIP().toString().c_str());
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        g_apMode = false;
        tryStart = 0;
        onStaUp();
        refreshScreen(true);
        return;
    }
    if (now - tryStart >= 20000UL) {
        WiFi.disconnect(false);
        WiFi.mode(WIFI_AP);
        tryStart = 0;
        lastTry = now;
    }
}

// ── Device notices ───────────────────────────────────────────────────────────

static CredState::NoticeLog g_noticeLog[2];          // 0 = Antigravity, 1 = Codex

static bool notice(int prov, CredState::NoticeReason r, const char* title, const char* value,
                   const char* sub, uint16_t color) {
    time_t now = time(nullptr);
    if (!CredState::noticeDue(g_noticeLog[prov], r, now)) return false;
    Attention::Item it;
    snprintf_P(it.id, sizeof(it.id), PSTR("sys:%d:%d"), prov, (int)r);
    it.kind = color == Theme::CORAL ? Attention::K_ERROR : Attention::K_WARNING;
    Attention::copyStr(it.title, sizeof(it.title), title);
    Attention::copyStr(it.value, sizeof(it.value), value);
    Attention::copyStr(it.body,  sizeof(it.body),  sub);
    uint32_t an = AttentionQueue::now();
    it.expires = an + 12;
    it.interruptUntil = an + 12;
    if (AttentionQueue::put(it) < 0) return false;
    g_noticeLog[prov].last[r] = now;
    Serial.printf_P(PSTR("[notice] %s %s\n"), title, value);
    return true;
}

static void noticesTick() {
    if (!timeSynced() || nightNow()) return;
    time_t now = time(nullptr);
    char sub[40]; snprintf(sub, sizeof(sub), "update at %s.local", MDNS_HOSTNAME);
    struct P { const char* title; Cred cred; time_t exp; const ResetGrant* g; } ps[2] = {
        {"ANTIGRAVITY TOKEN", g_antigravity.cred, 0,                &g_antigravity.resets},
        {"CODEX TOKEN",       g_codex.cred,       g_codex.jwtExp,   &g_codex.resets},
    };
    for (int i = 0; i < 2; i++) {
        const P& p = ps[i];
        if (CredState::bad(p.cred)) {
            const char* v = p.cred == Cred::EXPIRED ? "EXPIRED"
                          : p.cred == Cred::REJECTED ? "REJECTED" : "BLOCKED";
            const char* s2 = p.cred == Cred::BLOCKED ? "key is fine - retrying" : sub;
            if (notice(i, CredState::NOTICE_BAD, p.title, v, s2, credColor(p.cred))) return;
        } else if (p.cred == Cred::EXPIRING) {
            long left = (long)(p.exp - now);
            char v[16];
            if (left >= 86400L) snprintf(v, sizeof(v), "%ld DAY%s", left / 86400L, left >= 172800L ? "S" : "");
            else                snprintf(v, sizeof(v), "%ldH LEFT", left / 3600L > 0 ? left / 3600L : 1);
            if (notice(i, CredState::NOTICE_EXPIRING, p.title, v, sub, Theme::AMBER)) return;
        }
        if (p.g->left && p.g->endsAt > now && p.g->endsAt - now < 48L * 3600L) {
            char v[16]; snprintf(v, sizeof(v), "%u RESET%s", p.g->left, p.g->left > 1 ? "S" : "");
            char d[8];  TimeUtil::shortDuration((long)(p.g->endsAt - now), d, sizeof(d));
            char s3[40]; snprintf(s3, sizeof(s3), "unused - expires in %s", d);
            const char* who = i == 0 ? "ANTIGRAVITY" : "CODEX";
            if (notice(i, CredState::NOTICE_RESET, who, v, s3, Theme::AMBER)) return;
        }
    }
}

// ── Setup / loop ─────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Serial.println(F("\n=== glimmer v" FW_VERSION " ==="));

    Display::begin();
    Display::drawSplash("booting...");
    delay(400);

    Storage::begin();
    g_settings = Storage::load();
    Display::setBrightness(g_settings.brightness);
    Display::setInvert(g_settings.invertDisplay);
    Weather::begin();
    UsageHistory::begin();
    Api::refreshCred(g_settings, g_antigravity, g_codex);

    Display::drawSplash("connecting WiFi");
    if (!tryConnect()) {
        startAPMode();
    } else {
        Display::drawSplash("Syncing time");
        onStaUp();
    }

    Web::begin(g_settings);

    if (!g_apMode) {
        if (!g_settings.userName.isEmpty()) {
            char greet[40];
            snprintf(greet, sizeof(greet), "Hi, %s", g_settings.userName.c_str());
            Display::drawSplash(greet);
            delay(900);
        }
        applyBrightness();
        refreshScreen(true);
    }
}

void loop() {
    Web::loop();
    if (!g_apMode) MDNS.update();

    uint32_t now = millis();
    apStrandRetry(now);

    static uint32_t lastWifiCheck = 0;
    static uint32_t wifiRetrySince = 0;
    if (!g_apMode && now - lastWifiCheck >= 10000) {
        lastWifiCheck = now;
        bool healthy = (WiFi.status() == WL_CONNECTED)
                    && (WiFi.localIP() != IPAddress(0U));
        if (!healthy) {
            if (wifiRetrySince == 0) {
                wifiRetrySince = now;
                Serial.printf_P(PSTR("[wifi] unhealthy (status=%d ip=%s), waiting...\n"), WiFi.status(), WiFi.localIP().toString().c_str());
            } else if (now - wifiRetrySince >= 30000) {
                Serial.println(F("[wifi] reconnecting (full RF reset)..."));
                prepareSta(WIFI_STA);
                WiFi.begin(g_settings.wifiSSID.c_str(), g_settings.wifiPass.c_str());
                wifiRetrySince = now;
            }
        } else {
            if (wifiRetrySince != 0) {
                Serial.println(F("[wifi] reconnected"));
                mainTriggerRefresh();
            }
            wifiRetrySince = 0;
        }
    }

    schedulerTick(now);

    static uint32_t lastExpire = 0;
    if (now - lastExpire >= 1000UL) { lastExpire = now; AttentionQueue::expire(); }
    static bool wasAttention = false;
    ChannelCtx ctx = makeCtx();
    bool attention = !g_apMode && chAttentionEnabled(ctx);
    bool interrupt = AttentionQueue::takeInterrupt();
    const char* curName = g_activeCount ? kChannels[g_activeIdx[g_activePtr]].name : "";
    if (attention && (!wasAttention || (interrupt && strcmp(curName, "Attention")))) {
        mainShowChannel("Attention");
        applyBrightness();
    } else if (!attention && wasAttention) {
        recomputeActive();
        drawActive();
        applyBrightness();
        g_lastSlide = now;
    }
    wasAttention = attention;

    static uint32_t lastState = 0;
    static uint32_t stateSig  = 0xFFFFFFFF;
    if (!g_apMode && now - lastState >= 10000UL) {
        lastState = now;
        Api::refreshCred(g_settings, g_antigravity, g_codex);
        uint32_t sig = (nightNow() ? 1 : 0)
                     | (Api::isStale(g_antigravity.lastOk, g_settings) ? 2 : 0)
                     | (Api::isStale(g_codex.lastOk, g_settings)       ? 4 : 0)
                     | (Weather::isStale(g_settings)                   ? 8 : 0)
                     | ((uint32_t)g_antigravity.cred << 4) | ((uint32_t)g_codex.cred << 8);
        if (sig != stateSig) {
            bool redraw = ((sig ^ stateSig) & 0xFF1) != 0;
            stateSig = sig;
            applyBrightness();
            refreshScreen(redraw);
        }
    }

    static uint32_t lastNotice = 0;
    if (!g_apMode && now - lastNotice >= 60000UL) {
        lastNotice = now;
        noticesTick();
    }

    uint32_t slideMs = (uint32_t)g_settings.channelSec * 1000UL;
    const bool holding = attention && g_activeCount
                      && !strcmp(kChannels[g_activeIdx[g_activePtr]].name, "Attention");
    if (!g_apMode && g_settings.autoRotate && !g_nightFace && !holding && g_activeCount > 1
        && now - g_lastSlide >= slideMs) {
        g_lastSlide = now;
        recomputeActive();
        if (g_activeCount > 0) g_activePtr = (g_activePtr + 1) % g_activeCount;
        drawActive();
    }

    static uint32_t lastTick = 0;
    if (!g_apMode && g_activeCount > 0 && now - lastTick >= 200) {
        lastTick = now;
        auto tickFn = kChannels[g_activeIdx[g_activePtr]].tick;
        if (tickFn) tickFn(makeCtx());
    }

    static uint32_t lastInd = 0;
    if (!g_apMode && now - lastInd >= 250) {
        lastInd = now;
        drawIndicator(now);
    }

    delay(10);
}

void mainSettingsChanged() {
    if (Api::refreshCred(g_settings, g_antigravity, g_codex)) mainTriggerRefresh();
    Display::setInvert(g_settings.invertDisplay);
    Storage::applyTimezone(g_settings);
    applyBrightness();
    if (!g_apMode) refreshScreen(true);
}
