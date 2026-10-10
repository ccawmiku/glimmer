#include "web.h"
#include "display.h"
#include "api.h"
#include "gallery.h"
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>

static ESP8266WebServer server(80);
static ESP8266HTTPUpdateServer updater;
static Settings* pSettings = nullptr;
extern const char* mainActiveChannelName();
extern int mainEnabledCount();
extern void mainTriggerRefresh();
extern void mainSettingsChanged();
extern const AntigravityData* mainAntigravityData();
extern const Gallery::Frame* galleryFrame();
extern uint32_t gallerySeed();
extern uint32_t galleryCount();
extern uint32_t galleryDecodeUs();

static void sendJson(JsonDocument& doc) {
    String out; serializeJson(doc, out);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json; charset=utf-8", out);
}
static void error(int code, const char* message) {
    JsonDocument d; d["error"] = message;
    String out; serializeJson(d, out);
    server.send(code, "application/json; charset=utf-8", out);
}
static void settingsDoc(JsonDocument& d, const Settings& s, bool masked) {
    d["wifiSSID"] = s.wifiSSID;
    d["wifiPass"] = masked && !s.wifiPass.isEmpty() ? String("***") : s.wifiPass;
    d["agServer"] = s.agServer;
    d["agToken"] = masked && !s.agToken.isEmpty() ? String("***") : s.agToken;
    d["agModelLabel"] = s.agModelLabel;
    d["refreshMin"] = s.refreshMin;
    d["channelSec"] = s.channelSec;
    d["galleryRefreshSec"] = s.galleryRefreshSec;
    d["brightness"] = s.brightness;
    d["tzMinutes"] = s.tzMinutes;
    d["selectedPage"] = s.selectedPage;
    d["showHome"] = s.showHome;
    d["showAntigravity"] = s.showAntigravity;
    d["showInfo"] = s.showInfo;
    d["showGallery"] = s.showGallery;
    d["autoRotate"] = s.autoRotate;
    d["agWeeklyHero"] = s.agWeeklyHero;
    d["invertDisplay"] = s.invertDisplay;
}
static void handleState() {
    JsonDocument d;
    bool connected = WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0U);
    bool ap = WiFi.getMode() == WIFI_AP;
    d["fw"] = FW_VERSION;
    d["wifi"] = connected ? "connected" : (ap ? "ap" : "disconnected");
    d["ip"] = ap ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    d["ssid"] = ap ? String(SETUP_AP_SSID) : WiFi.SSID();
    d["rssi"] = WiFi.RSSI();
    d["uptime_s"] = millis() / 1000UL;
    d["heap"] = ESP.getFreeHeap();
    d["maxblk"] = ESP.getMaxFreeBlockSize();
    d["cpu_mhz"] = ESP.getCpuFreqMHz();
    time_t now = time(nullptr);
    d["time_synced"] = (now > 1000000000L);
    d["epoch"] = (uint32_t)now;
    if (now > 1000000000L) {
        char timeStr[32];
        struct tm tmv;
        localtime_r(&now, &tmv);
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &tmv);
        d["time_str"] = timeStr;
    }
    d["channel"] = mainActiveChannelName();
    d["enabled_count"] = mainEnabledCount();
    d["ag_configured"] = true;
    d["ag_http"] = Api::lastAgHttp();
    d["ag_valid"] = mainAntigravityData()->valid;
    d["ag_error"] = mainAntigravityData()->err;
    d["ag_parse"] = Api::lastAgParse();
    d["ag_count"] = mainAntigravityData()->accountCount;
    JsonArray accArr = d["ag_accounts"].to<JsonArray>();
    for (int i = 0; i < mainAntigravityData()->accountCount; i++) {
        JsonObject a = accArr.add<JsonObject>();
        a["email"] = mainAntigravityData()->accounts[i].email;
        a["tag"] = mainAntigravityData()->accounts[i].tag;
        a["p5h"] = mainAntigravityData()->accounts[i].primaryPct;
        a["p7d"] = mainAntigravityData()->accounts[i].secondaryPct;
    }
    d["gallery_seed"] = gallerySeed();
    d["gallery_count"] = galleryCount();
    d["gallery_decode_us"] = galleryDecodeUs();
    sendJson(d);
}
static void handleGallery() {
    const Gallery::Frame* frame = galleryFrame();
    if (!frame->seed) { error(409, "画廊尚未生成画面"); return; }
    // Stream descriptors instead of allocating a large JSON document on ESP8266.
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", "");
    char chunk[160];
    snprintf(chunk, sizeof(chunk), "{\"seed\":%lu,\"background\":%u,\"edge\":%u,\"stripes\":[", (unsigned long)frame->seed, Gallery::Background, frame->edgeExpand);
    server.sendContent(chunk);
    for (int i = 0; i < frame->count; ++i) {
        const auto& part = frame->stripes[i];
        snprintf(chunk, sizeof(chunk), "%s[%u,%u,%u,%u]", i ? "," : "", part.pos, part.size, part.color, part.vertical ? 1 : 0);
        server.sendContent(chunk); yield();
    }
    server.sendContent("]}"); server.sendContent("");
}
static void handleGetSettings() {
    JsonDocument d; settingsDoc(d, *pSettings, true); sendJson(d);
}
static void applyIfPresent(Settings& s, JsonDocument& d) {
    auto applyStr = [&](const char* k, String& dst) {
        if (d[k].is<const char*>() && strcmp(d[k].as<const char*>(), "***")) dst = d[k].as<const char*>();
    };
    auto applyBool = [&](const char* k, bool& dst) { if (d[k].is<bool>()) dst = d[k].as<bool>(); };
    applyStr("wifiSSID", s.wifiSSID);
    applyStr("wifiPass", s.wifiPass);
    applyStr("agServer", s.agServer);
    applyStr("agToken", s.agToken);
    applyStr("agModelLabel", s.agModelLabel);
    if (d["refreshMin"].is<int>()) s.refreshMin = constrain(d["refreshMin"].as<int>(), 1, 60);
    if (d["channelSec"].is<int>()) s.channelSec = constrain(d["channelSec"].as<int>(), 3, 3600);
    if (d["galleryRefreshSec"].is<int>()) s.galleryRefreshSec = constrain(d["galleryRefreshSec"].as<int>(), 1, 86400);
    if (d["brightness"].is<int>()) s.brightness = constrain(d["brightness"].as<int>(), 5, 100);
    if (d["tzMinutes"].is<int>()) s.tzMinutes = constrain(d["tzMinutes"].as<int>(), -720, 840);
    if (d["selectedPage"].is<int>()) s.selectedPage = constrain(d["selectedPage"].as<int>(), 0, 3);
    applyBool("showHome", s.showHome);
    applyBool("showAntigravity", s.showAntigravity);
    applyBool("showInfo", s.showInfo);
    applyBool("showGallery", s.showGallery);
    applyBool("autoRotate", s.autoRotate);
    applyBool("agWeeklyHero", s.agWeeklyHero);
    applyBool("invertDisplay", s.invertDisplay);
    Storage::normalize(s);
}
static void handlePostSettings() {
    JsonDocument d;
    if (deserializeJson(d, server.arg("plain")) || !d.is<JsonObject>()) { error(400, "设置格式错误"); return; }
    Settings updated = *pSettings;
    applyIfPresent(updated, d);
    if (!Storage::save(updated)) { error(500, "保存失败，存储空间不可写"); return; }
    bool tokensChanged = updated.agToken != pSettings->agToken || updated.agServer != pSettings->agServer;
    *pSettings = updated;
    Display::setInvert(updated.invertDisplay);
    Display::setBrightness(updated.brightness);
    int offset = -updated.tzMinutes;
    char tz[20];
    // Keep the sign on fractional negative offsets (e.g. UTC+00:30).
    snprintf(tz, sizeof(tz), "UTC%c%d:%02d", offset < 0 ? '-' : '+', abs(offset) / 60, abs(offset) % 60);
    setenv("TZ", tz, 1); tzset();
    mainSettingsChanged();
    if (tokensChanged) mainTriggerRefresh();
    bool restart = d["_restart"] | false;
    JsonDocument reply; reply["ok"] = true; reply["restart"] = restart; sendJson(reply);
    if (restart) { delay(300); ESP.restart(); }
}
static void handleExport() {
    // A backup intentionally contains credentials; normal settings remain masked.
    if (!LittleFS.exists("/config.json") && !Storage::save(*pSettings)) { error(500, "无法导出设置"); return; }
    File f = LittleFS.open("/config.json", "r");
    if (!f) { error(500, "无法读取设置"); return; }
    server.sendHeader("Content-Disposition", "attachment; filename=glimmer-config.json");
    server.sendHeader("Cache-Control", "no-store");
    server.streamFile(f, "application/json"); f.close();
}
static void handleImport() {
    JsonDocument d;
    String body = server.arg("plain");
    if (deserializeJson(d, body) || !d.is<JsonObject>() || !d["wifi_ssid"].is<const char*>()) {
        error(400, "请使用从设备导出的设置备份"); return;
    }
    File f = LittleFS.open("/config.tmp", "w");
    if (!f) { error(500, "无法写入设置"); return; }
    size_t n = serializeJson(d, f); f.close();
    if (!n || !LittleFS.rename("/config.tmp", "/config.json")) { error(500, "恢复设置失败"); return; }
    JsonDocument reply; reply["ok"] = true; reply["restart"] = true; sendJson(reply);
    delay(300); ESP.restart();
}
static void handleReboot() {
    JsonDocument d; d["ok"] = true; d["restart"] = true; sendJson(d);
    delay(300); ESP.restart();
}
static const char recoveryPage[] PROGMEM = R"HTML(<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>小电视设置</title><body><h1>小电视设置</h1><p>请先上传本次构建的网页文件系统，完整中文控制面板随后可用。</p><h2>刷写文件</h2><p>先刷固件，再刷文件系统。文件系统更新会清除设置，请先保存备份。</p><p><a href="/api/export">导出设置备份</a></p><form method="post" action="/update" enctype="multipart/form-data"><label>固件文件 <input type="file" name="firmware" accept=".bin" required></label><button>上传固件</button></form><form method="post" action="/update" enctype="multipart/form-data"><label>文件系统 <input type="file" name="filesystem" accept=".bin" required></label><button>上传文件系统</button></form></body></html>)HTML";
static bool serveStatic(const String& uri) {
    if (uri.indexOf("..") >= 0) return false;
    String path = "/web" + (uri == "/" ? String("/index.html") : uri);
    String actual = LittleFS.exists(path + ".gz") ? path + ".gz" : path;
    if (!LittleFS.exists(actual)) return false;
    File f = LittleFS.open(actual, "r"); if (!f) return false;
    const char* type = path.endsWith(".js") ? "application/javascript" : path.endsWith(".css") ? "text/css" : "text/html; charset=utf-8";
    server.sendHeader("Cache-Control", "no-cache");
    server.streamFile(f, type); f.close(); return true;
}
void Web::begin(Settings& settings) {
    pSettings = &settings;
    // Register a Chinese upload page before the updater's fallback GET page.
    server.on("/update", HTTP_GET, []() {
        if (!serveStatic("/")) server.send_P(200, "text/html; charset=utf-8", recoveryPage);
    });
    updater.setup(&server);
    Update.onProgress([](size_t cur, size_t total) {
        if (total) Display::drawOtaProgress(uint8_t(cur * 100UL / total));
    });
    server.on("/api/state", HTTP_GET, handleState);
    server.on("/api/gallery", HTTP_GET, handleGallery);
    server.on("/api/settings", HTTP_GET, handleGetSettings);
    server.on("/api/settings", HTTP_POST, handlePostSettings);
    server.on("/api/export", HTTP_GET, handleExport);
    server.on("/api/import", HTTP_POST, handleImport);
    server.on("/api/reboot", HTTP_POST, handleReboot);
    server.on("/api/refresh", HTTP_POST, []() { mainTriggerRefresh(); JsonDocument d; d["ok"] = true; sendJson(d); });
    server.onNotFound([]() {
        if (serveStatic(server.uri())) return;
        if (server.uri() == "/") server.send_P(200, "text/html; charset=utf-8", recoveryPage);
        else error(404, "页面不存在");
    });
    server.begin();
}
void Web::loop() { server.handleClient(); }
