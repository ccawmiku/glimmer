#include "api.h"
#include "display.h"
#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

// ── time parsing ─────────────────────────────────────────────────────────────

static time_t parseISO8601(const char* s) {
    if (!s || strlen(s) < 19) return 0;
    struct tm t = {};
    if (sscanf(s, "%d-%d-%dT%d:%d:%d",
               &t.tm_year, &t.tm_mon, &t.tm_mday,
               &t.tm_hour, &t.tm_min, &t.tm_sec) < 6) return 0;
    t.tm_year -= 1900;
    t.tm_mon  -= 1;
    char* prev = getenv("TZ");
    String backup = prev ? prev : "";
    setenv("TZ", "UTC0", 1); tzset();
    time_t r = mktime(&t);
    if (backup.length()) setenv("TZ", backup.c_str(), 1); else unsetenv("TZ");
    tzset();
    return r;
}

String Api::formatCountdown(time_t t) {
    time_t now = time(nullptr);
    if (now < 1000000000L) return "--";
    long diff = (long)(t - now);
    if (diff <= 0) return "now";
    long h = diff / 3600;
    long m = (diff % 3600) / 60;
    if (h >= 24) { char b[8]; snprintf(b, sizeof(b), "%ldd", (h + 12) / 24); return b; }
    if (h >= 1)  { char b[10]; snprintf(b, sizeof(b), "%ldh %ldm", h, m);   return b; }
    char b[8]; snprintf(b, sizeof(b), "%ldm", m); return b;
}

// ── shared HTTP / TLS request ────────────────────────────────────────────────

static bool httpRequest(const String& url,
                        const std::function<void(HTTPClient&)>& addHeaders,
                        String& body, int& httpCode) {
    Display::releaseFont();
    yield(); delay(20);

    bool isHttps = url.startsWith("https://");
    HTTPClient http;
    http.useHTTP10(true);
    http.setTimeout(10000);

    if (isHttps) {
        BearSSL::WiFiClientSecure sc;
        sc.setInsecure();
        sc.setBufferSizes(4096, 1024);
        if (!http.begin(sc, url)) {
            httpCode = -2;
            return false;
        }
        addHeaders(http);
        httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) body = http.getString();
        http.end();
        return httpCode == HTTP_CODE_OK;
    } else {
        WiFiClient client;
        if (!http.begin(client, url)) {
            httpCode = -2;
            return false;
        }
        addHeaders(http);
        httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) body = http.getString();
        http.end();
        return httpCode == HTTP_CODE_OK;
    }
}

// ── Transient failure suppression ────────────────────────────────────────────
static constexpr int kMaxSilentFails = 3;
static int s_agFails = 0;

// ── Debug telemetry (surfaced in /api/state) ──
static int  s_dbgAgHttp    = 0;
static int  s_dbgAgBodyLen = -1;
static char s_dbgAgParse[24] = "";

namespace Api {
    int  lastAgHttp()    { return s_dbgAgHttp; }
    int  lastAgBodyLen() { return s_dbgAgBodyLen; }
    const char* lastAgParse() { return s_dbgAgParse; }
}

static bool agSoftFail(AntigravityData& out, const char* err) {
    s_agFails++;
    if (s_agFails < kMaxSilentFails) {
        if (!out.valid) out.err[0] = '\0';
        Serial.printf("[antigravity] soft fail (%s) %d/%d — %s\n", err, s_agFails,
                      kMaxSilentFails, out.valid ? "keeping stale" : "showing placeholder");
        return false;
    }
    strncpy(out.err, err, sizeof(out.err) - 1);
    out.err[sizeof(out.err) - 1] = '\0';
    return false;
}

// ── Antigravity fetch from Antigravity Tools ─────────────────────────────────

bool Api::fetchAntigravity(const Settings& s, AntigravityData& out) {
    String serverBase = s.agServer;
    if (serverBase.isEmpty()) serverBase = "http://192.168.1.1:8045";
    while (serverBase.endsWith("/")) serverBase.remove(serverBase.length() - 1);
    
    String url = serverBase;
    if (!url.endsWith("/api/accounts")) url += "/api/accounts";

    String token = s.agToken;
    if (token.isEmpty()) token = "PS4bilibili"; // 默认管理密码

    String body; int code = 0;
    auto addH = [&](HTTPClient& h) {
        h.addHeader("Authorization", "Bearer " + token);
        h.addHeader("x-api-key", token);
        h.addHeader("Content-Type", "application/json");
        h.setUserAgent("Glimmer/1.0");
    };

    if (!httpRequest(url, addH, body, code)) {
        s_dbgAgHttp = code;
        s_dbgAgBodyLen = -1;
        snprintf(s_dbgAgParse, sizeof(s_dbgAgParse), "HTTP %d", code);
        char e[24]; snprintf(e, sizeof(e), "HTTP %d", code);
        return agSoftFail(out, e);
    }

    s_dbgAgHttp = code;
    s_dbgAgBodyLen = (int)body.length();

    // 过滤解析以节省内存：剔除庞大的 models 数组，保留 quota_groups 结构
    JsonDocument filter;
    filter["current_account_id"] = true;
    filter["accounts"][0]["id"] = true;
    filter["accounts"][0]["email"] = true;
    filter["accounts"][0]["name"] = true;
    filter["accounts"][0]["is_current"] = true;
    filter["accounts"][0]["disabled"] = true;
    filter["accounts"][0]["proxy_disabled"] = true;
    filter["accounts"][0]["validation_blocked"] = true;
    filter["accounts"][0]["quota"]["quota_groups"] = true;
    filter["accounts"][0]["quota"]["last_updated"] = true;

    JsonDocument doc;
    DeserializationError jerr = deserializeJson(doc, body, DeserializationOption::Filter(filter));
    strncpy(s_dbgAgParse, jerr.c_str(), sizeof(s_dbgAgParse) - 1);
    s_dbgAgParse[sizeof(s_dbgAgParse) - 1] = '\0';

    if (jerr) {
        char e[24]; snprintf(e, sizeof(e), "JSON %s", jerr.c_str());
        return agSoftFail(out, e);
    }

    body = String(); // 立即释放 body 内存

    JsonArray accArray = doc["accounts"].as<JsonArray>();
    if (accArray.isNull() || accArray.size() == 0) {
        snprintf(s_dbgAgParse, sizeof(s_dbgAgParse), "No accounts");
        char e[24] = "No accounts";
        return agSoftFail(out, e);
    }

    const char* currentAccountId = doc["current_account_id"] | "";

    out.accountCount = 0;
    int currentFoundIdx = -1;

    for (JsonObject accObj : accArray) {
        if (out.accountCount >= kMaxAgAccounts) break;

        AntigravityAccount& acc = out.accounts[out.accountCount];
        const char* email = accObj["email"] | "";
        const char* name  = accObj["name"]  | "";
        strncpy(acc.email, email, sizeof(acc.email) - 1);
        acc.email[sizeof(acc.email) - 1] = '\0';
        strncpy(acc.name, name, sizeof(acc.name) - 1);
        acc.name[sizeof(acc.name) - 1] = '\0';

        const char* accId = accObj["id"] | "";
        bool isCurrent = accObj["is_current"] | false;
        if (!isCurrent && currentAccountId[0] && strcmp(accId, currentAccountId) == 0) {
            isCurrent = true;
        }
        acc.isCurrent = isCurrent;

        acc.isDisabled = (accObj["disabled"] | false) ||
                         (accObj["proxy_disabled"] | false) ||
                         (accObj["validation_blocked"] | false);

        JsonObject lim = accObj["live_limited_models"].as<JsonObject>();
        acc.isLimited = !lim.isNull() && lim.size() > 0;

        // 提取 5h 和 7d (weekly) 配额
        // 默认优先匹配 Gemini 核心配额 (gemini-5h / gemini-weekly)，避免被始终为 100% 的 3p 额度覆盖
        bool preferGemini = !s.agModelLabel.equalsIgnoreCase("CLAUDE");
        bool exact5h = false;
        bool exact7d = false;
        float frac5h = -1.0f;
        float frac7d = -1.0f;
        time_t rst5h = 0;
        time_t rst7d = 0;

        JsonArray groups = accObj["quota"]["quota_groups"].as<JsonArray>();
        for (JsonObject g : groups) {
            for (JsonObject b : g["buckets"].as<JsonArray>()) {
                const char* bid = b["bucket_id"] | "";
                const char* win = b["window"] | "";
                float frac = b["remaining_fraction"] | -1.0f;
                const char* rt = b["reset_time"] | "";
                time_t rst = rt ? parseISO8601(rt) : 0;

                bool is5h = (strcmp(win, "5h") == 0) || (strstr(bid, "5h") != nullptr);
                bool is7d = (strcmp(win, "weekly") == 0) || (strstr(bid, "weekly") != nullptr) || (strstr(bid, "7d") != nullptr);
                bool isGemini = (strstr(bid, "gemini") != nullptr);
                bool is3p = (strstr(bid, "3p") != nullptr) || (strstr(bid, "claude") != nullptr);

                if (is5h) {
                    if (preferGemini) {
                        if (isGemini) {
                            frac5h = frac; rst5h = rst; exact5h = true;
                        } else if (!exact5h && frac5h < 0) {
                            frac5h = frac; rst5h = rst;
                        }
                    } else {
                        if (is3p) {
                            frac5h = frac; rst5h = rst; exact5h = true;
                        } else if (!exact5h && frac5h < 0) {
                            frac5h = frac; rst5h = rst;
                        }
                    }
                }

                if (is7d) {
                    if (preferGemini) {
                        if (isGemini) {
                            frac7d = frac; rst7d = rst; exact7d = true;
                        } else if (!exact7d && frac7d < 0) {
                            frac7d = frac; rst7d = rst;
                        }
                    } else {
                        if (is3p) {
                            frac7d = frac; rst7d = rst; exact7d = true;
                        } else if (!exact7d && frac7d < 0) {
                            frac7d = frac; rst7d = rst;
                        }
                    }
                }
            }
        }

        if (frac5h < 0) frac5h = 1.0f;
        if (frac7d < 0) frac7d = 1.0f;

        acc.primaryPct     = frac5h * 100.0f;
        acc.secondaryPct   = frac7d * 100.0f;
        acc.primaryReset   = rst5h;
        acc.secondaryReset = rst7d;

        if (acc.isCurrent && currentFoundIdx < 0) {
            currentFoundIdx = out.accountCount;
        }

        out.accountCount++;
    }

    // 设置短标签：单账号显示 "AG"，多账号分别显示 "A1", "A2", "A3"...
    for (int i = 0; i < out.accountCount; i++) {
        if (out.accountCount == 1) {
            strncpy(out.accounts[i].tag, "AG", sizeof(out.accounts[i].tag) - 1);
        } else {
            snprintf(out.accounts[i].tag, sizeof(out.accounts[i].tag), "A%d", i + 1);
        }
    }

    if (currentFoundIdx < 0 && out.accountCount > 0) {
        currentFoundIdx = 0;
    }
    out.currentIdx = currentFoundIdx;

    if (currentFoundIdx >= 0) {
        const AntigravityAccount& cur = out.accounts[currentFoundIdx];
        out.primaryPct     = cur.primaryPct;
        out.secondaryPct   = cur.secondaryPct;
        out.primaryReset   = cur.primaryReset;
        out.secondaryReset = cur.secondaryReset;
    }

    out.primaryWinSec   = 18000;
    out.secondaryWinSec = 604800;
    strncpy(out.secondaryTag, "WEEKLY", sizeof(out.secondaryTag) - 1);
    out.valid = true;
    out.err[0] = '\0';
    s_agFails = 0;
    snprintf(s_dbgAgParse, sizeof(s_dbgAgParse), "OK cnt=%d", out.accountCount);

    // 若系统尚未通过 NTP 同步时间，使用 API 携带的时间戳保底校时
    if (time(nullptr) < 1000000000L) {
        for (JsonObject a : accArray) {
            long lastUpdated = a["quota"]["last_updated"] | 0L;
            if (lastUpdated > 1000000000L) {
                struct timeval tv = { (time_t)lastUpdated, 0 };
                settimeofday(&tv, nullptr);
                break;
            }
        }
    }

    time_t t = time(nullptr);
    if (t > 1000000000L) {
        struct tm tm; localtime_r(&t, &tm);
        int h = tm.tm_hour;
        out.hourlyPct[h]   = (uint8_t)(out.primaryPct < 0 ? 0 : out.primaryPct);
        out.hourlyValid[h] = true;
    }

    return true;
}
