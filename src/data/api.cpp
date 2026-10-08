#include "api.h"
#include "display.h"
#include "usage_parse.h"
#include "timeutil.h"
#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>
#include <ArduinoJson.h>

String Api::formatCountdown(time_t t) {
    time_t now = time(nullptr);
    if (now < 1000000000L) return "--";                 // clock not synced yet
    long diff = (long)(t - now);
    if (diff <= 0) return "now";
    long h = diff / 3600;
    long m = (diff % 3600) / 60;
    if (h >= 24) { char b[8]; snprintf_P(b, sizeof(b), PSTR("%ldd"), (h + 12) / 24); return b; }
    if (h >= 1)  { char b[10]; snprintf_P(b, sizeof(b), PSTR("%ldh %ldm"), h, m);   return b; }
    char b[8]; snprintf_P(b, sizeof(b), PSTR("%ldm"), m); return b;
}

bool Api::isStale(time_t lastOk, const Settings& s) {
    time_t now = time(nullptr);
    if (lastOk <= 0 || now < 1000000000L) return false;
    long limit = (long)s.refreshMin * 60L * 3L;
    if (limit < 15L * 60L) limit = 15L * 60L;
    return (long)(now - lastOk) > limit;
}

void Api::staleText(time_t lastOk, const Settings& s, char* buf, size_t n) {
    if (!isStale(lastOk, s)) { if (n) buf[0] = '\0'; return; }
    char d[8]; TimeUtil::shortDuration((long)(time(nullptr) - lastOk), d, sizeof(d));
    snprintf_P(buf, n, PSTR("STALE %s"), d);
}

static const char* provName(bool isAg, bool token) {
    return isAg ? (token ? "ANTIGRAVITY TOKEN" : "ANTIGRAV") : (token ? "CODEX TOKEN" : "CODEX");
}

void Api::adviceText(const AntigravityData& ag, const CodexData& cx, char* buf, size_t n) {
    if (n) buf[0] = '\0';
    // 1. A credential problem outranks any advice: the numbers aren't live.
    if (CredState::bad(ag.cred) || CredState::bad(cx.cred)) {
        bool isAg = CredState::bad(ag.cred);
        Cred c = isAg ? ag.cred : cx.cred;
        if (c == Cred::BLOCKED) snprintf_P(buf, n, PSTR("%s BLOCKED \xC2\xB7 RETRYING"), provName(isAg, false));
        else snprintf_P(buf, n, PSTR("%s %s"), provName(isAg, true), c == Cred::EXPIRED ? "EXPIRED" : "REJECTED");
        return;
    }
    if (!ag.valid || !cx.valid || ag.secondaryPct < 0 || cx.primaryPct < 0) return;
    time_t now = time(nullptr);
    if (now < 1000000000L) return;
    struct Cand { const char* name; float pct; time_t reset; const ResetGrant* g; } c[2] = {
        {"ANTIGRAV", ag.secondaryPct, ag.secondaryReset, &ag.resets},
        {"CODEX",    cx.primaryPct,   cx.primaryReset,   &cx.resets},
    };
    // 2. Nearly out, but a limit-reset credit can be spent right now.
    for (auto& k : c) {
        if (k.pct < 15.0f && k.g->left && k.g->usable) {
            snprintf_P(buf, n, PSTR("%s: %u RESET%s AVAILABLE"), k.name, k.g->left, k.g->left > 1 ? "S" : "");
            return;
        }
    }
    // 3. Use-it-or-lose-it: an allowance that resets soon with plenty unused.
    int pick = -1;
    for (int i = 0; i < 2; i++) {
        long left = (long)(c[i].reset - now);
        if (c[i].reset <= now || left > 48L * 3600L || c[i].pct < 25.0f) continue;
        if (pick < 0 || c[i].reset < c[pick].reset) pick = i;
    }
    if (pick >= 0) {
        char d[8]; TimeUtil::shortDuration((long)(c[pick].reset - now), d, sizeof(d));
        snprintf_P(buf, n, PSTR("USE %s \xC2\xB7 RESETS %s"), c[pick].name, d);
        return;
    }
    int most = c[0].pct >= c[1].pct ? 0 : 1;
    if (c[most].pct < 15.0f) { snprintf_P(buf, n, PSTR("BOTH LOW")); return; }
    snprintf_P(buf, n, PSTR("MOST ROOM: %s %.0f%%"), c[most].name, c[most].pct);
}

// ── shared TLS request ────────────────────────────────────────────────────────

static Api::TlsResult tlsRequestStreamOnce(const char* method, const char* url,
                                           const std::function<void(HTTPClient&)>& addHeaders,
                                           const String& postData,
                                           const std::function<bool(Stream&)>& onBody) {
    Api::TlsResult r;
    BearSSL::WiFiClientSecure sc;
    sc.setInsecure();
    sc.setBufferSizes(4096, 1024);                      // 4K rx (cert chain), 1K tx
    HTTPClient http;
    http.useHTTP10(true);                               // no chunked encoding → streamable
    http.setTimeout(15000);
    if (!http.begin(sc, url)) {
        Serial.printf_P(PSTR("[tls] http.begin failed, heap=%u, maxblk=%u\n"), ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());
        r.code = -2;                                    // -2 = begin() failed (URL/TLS init)
        return r;
    }
    static const char* kHeaders[] = {"Retry-After"};
    http.collectHeaders(kHeaders, 1);
    addHeaders(http);
    if (strcmp(method, "POST") == 0) {
        r.code = http.POST(postData);
    } else {
        r.code = http.GET();                            // negative = HTTPClient error
    }
    Serial.printf_P(PSTR("[tls] %s → %d, heap=%u, maxblk=%u\n"), method, r.code, ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());
    r.retryAfter = FetchPolicy::retryAfterSec(http.header("Retry-After").c_str(), time(nullptr));
    if (r.code == HTTP_CODE_OK) {
        r.parsed = onBody(http.getStream());
    } else if (r.code == 401 || r.code == 403) {
        Stream& s = http.getStream();
        uint32_t until = millis() + 2000;
        while (millis() < until) {
            int c = s.read();
            if (c < 0) { if (!http.connected()) break; delay(5); continue; }
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
            r.markup = (c == '<');
            break;
        }
    }
    http.end();
    return r;
}

Api::TlsResult Api::tlsRequestStream(const char* method, const char* url,
                                     const std::function<void(HTTPClient&)>& addHeaders,
                                     const String& postData,
                                     const std::function<bool(Stream&)>& onBody) {
    Display::releaseFont();
    yield(); delay(20);
    Serial.printf_P(PSTR("[tls] heap=%u maxblk=%u url=%s\n"), ESP.getFreeHeap(), ESP.getMaxFreeBlockSize(), url);

    TlsResult r = tlsRequestStreamOnce(method, url, addHeaders, postData, onBody);
    if (r.code < 0) {
        Serial.printf_P(PSTR("[tls] retry after %d ...\n"), r.code);
        yield(); delay(200);
        r = tlsRequestStreamOnce(method, url, addHeaders, postData, onBody);
        Serial.printf_P(PSTR("[tls] retry → %d\n"), r.code);
    }
    return r;
}

Api::TlsResult Api::tlsGetStream(const char* url,
                                 const std::function<void(HTTPClient&)>& addHeaders,
                                 const std::function<bool(Stream&)>& onBody) {
    return tlsRequestStream("GET", url, addHeaders, "", onBody);
}

// ── Per-source policy + telemetry ────────────────────────────────────────────

static FetchPolicy::State s_agPol;
static FetchPolicy::State s_codexPol;
static int  s_dbgAgHttp = 0;
static int  s_dbgAgBodyLen = 0;
static char s_dbgAgParse[24] = "";

namespace Api {
    const FetchPolicy::State& antigravityPolicy() { return s_agPol; }
    const FetchPolicy::State& codexPolicy()       { return s_codexPol; }
    int  lastAgHttp()          { return s_dbgAgHttp; }
    int  lastAgBodyLen()       { return s_dbgAgBodyLen; }
    const char* lastAgParse()  { return s_dbgAgParse; }
}

static uint32_t hashStr(const String& s) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < s.length(); i++) { h ^= (uint8_t)s[i]; h *= 16777619u; }
    return h;
}

bool Api::refreshCred(const Settings& s, AntigravityData& ag, CodexData& cx) {
    static bool     s_seeded = false;
    static uint32_t s_agHash = 0, s_cxHash = 0;
    bool changed = false;
    uint32_t h = hashStr(s.agToken);
    if (!s_seeded || h != s_agHash) {
        changed |= s_seeded;
        s_agHash = h; s_agPol = FetchPolicy::State(); ag.err[0] = '\0';
    }
    h = hashStr(s.codexToken);
    if (!s_seeded || h != s_cxHash) {
        changed |= s_seeded;
        s_cxHash = h; s_codexPol = FetchPolicy::State(); cx.err[0] = '\0';
        cx.jwtExp = CredState::jwtExp(s.codexToken.c_str());
        cx.resets = ResetGrant{};
    }
    s_seeded = true;
    time_t now = time(nullptr);
    ag.cred = CredState::derive(!s.agToken.isEmpty(),    s_agPol,    0,         now, ag.valid);
    cx.cred = CredState::derive(!s.codexToken.isEmpty(), s_codexPol, cx.jwtExp, now, cx.valid);
    return changed;
}

static void transportErr(int code, char* e, size_t n) {
    if (code < 0) snprintf_P(e, n, PSTR("Offline"));
    else          snprintf_P(e, n, PSTR("HTTP %d"), code);
}

template <class D>
static bool failSource(FetchPolicy::State& pol, D& out, const Api::TlsResult& r,
                       const char* err, const char* tag) {
    FetchPolicy::onFailure(pol, r.code, r.retryAfter, r.markup);
    Serial.printf_P(PSTR("[%s] fail (%s) streak=%u auth=%u blocked=%u wait=%us\n"), tag, err,
                    pol.fails, pol.authLatched, pol.blockedStreak, (unsigned)pol.waitS);
    bool credIssue = pol.authLatched || FetchPolicy::blocked(pol);
    if (credIssue || !FetchPolicy::shouldSurface(pol)) {
        out.err[0] = '\0';
        return false;
    }
    strncpy(out.err, err, sizeof(out.err) - 1);
    out.err[sizeof(out.err) - 1] = '\0';
    return false;
}

template <class D>
static void okSource(FetchPolicy::State& pol, D& out) {
    FetchPolicy::onSuccess(pol, time(nullptr));
    out.err[0] = '\0';
    out.valid = true;
    out.lastOk = pol.lastOk;
}

// ── Antigravity ─────────────────────────────────────────────────────────────

static String s_agAccessToken;
static time_t s_agTokenExpires = 0;
static String s_agCachedRefreshToken;

static inline String getAgClientId() {
    return String("1071006060591-tmhssin2h21lcre235vtolojh4g403ep") + String(".apps.") + String("googleusercontent.com");
}
static inline String getAgClientSecret() {
    return String("GOC") + String("SPX-K58FWR486LdLJ1mLB8sXC4z6qDAf");
}

static bool refreshAgAccessToken(const String& refreshToken, String& outAccessToken, char errBuf[], size_t errBufLen) {
    String postBody = "client_id=" + getAgClientId() +
                      "&client_secret=" + getAgClientSecret() +
                      "&refresh_token=" + refreshToken +
                      "&grant_type=refresh_token";

    auto addH = [&](HTTPClient& h) {
        h.addHeader("Content-Type", "application/x-www-form-urlencoded");
        h.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) Antigravity/1.0");
    };

    String tokenResult;
    long exp = 3600;
    Api::TlsResult r = Api::tlsRequestStream("POST", "https://oauth2.googleapis.com/token", addH, postBody,
        [&](Stream& body) {
            JsonDocument filter;
            filter["access_token"] = true;
            filter["expires_in"]   = true;
            JsonDocument doc;
            DeserializationError jerr = deserializeJson(doc, body, DeserializationOption::Filter(filter));
            if (jerr) {
                snprintf(errBuf, errBufLen, "JSON %s", jerr.c_str());
                return false;
            }
            const char* at = doc["access_token"].as<const char*>();
            if (!at || !*at) {
                snprintf(errBuf, errBufLen, "No token");
                return false;
            }
            tokenResult = at;
            exp = doc["expires_in"] | 3600;
            return true;
        });

    if (r.code != 200 || !r.parsed) {
        if (!errBuf[0]) snprintf(errBuf, errBufLen, "Auth %d", r.code);
        return false;
    }

    outAccessToken = tokenResult;
    time_t now = time(nullptr);
    s_agTokenExpires = (now > 1000000000L) ? (now + exp - 120) : (now + 3480);
    return true;
}

bool Api::fetchAntigravity(const Settings& s, AntigravityData& out) {
    if (s.agToken.isEmpty()) return false;

    String accessToken;
    if (s.agToken.startsWith("ya29.")) {
        accessToken = s.agToken;
    } else {
        time_t now = time(nullptr);
        if (s_agAccessToken.length() && s_agCachedRefreshToken == s.agToken && (now < 1000000000L || now < s_agTokenExpires)) {
            accessToken = s_agAccessToken;
        } else {
            char authErr[24] = "";
            if (!refreshAgAccessToken(s.agToken, accessToken, authErr, sizeof(authErr))) {
                Api::TlsResult r; r.code = 401;
                return failSource(s_agPol, out, r, authErr, "antigravity");
            }
            s_agAccessToken = accessToken;
            s_agCachedRefreshToken = s.agToken;
            yield(); delay(50);
        }
    }

    auto addH = [&](HTTPClient& h) {
        h.addHeader("Authorization", "Bearer " + accessToken);
        h.addHeader("Content-Type",  "application/json");
        h.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) Antigravity/1.0");
    };

    AntigravityData next = out;
    bool parsedOk = false;
    String postBody = "{\"project\":\"aicode-consumers\"}";

    Api::TlsResult r = tlsRequestStream("POST", "https://cloudcode-pa.googleapis.com/v1internal:retrieveUserQuotaSummary",
        addH, postBody,
        [&](Stream& body) {
            JsonDocument filter;
            filter["groups"][0]["displayName"] = true;
            filter["groups"][0]["buckets"][0]["bucketId"] = true;
            filter["groups"][0]["buckets"][0]["remainingFraction"] = true;
            filter["groups"][0]["buckets"][0]["resetTime"] = true;

            JsonDocument doc;
            DeserializationError jerr = deserializeJson(doc, body, DeserializationOption::Filter(filter));
            strncpy(s_dbgAgParse, jerr.c_str(), sizeof(s_dbgAgParse) - 1);
            if (jerr) {
                Serial.printf_P(PSTR("[antigravity] parse: %s (heap=%u maxblk=%u)\n"), jerr.c_str(),
                                ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());
                return false;
            }

            bool want3P = s.agModelLabel.equalsIgnoreCase("3p") || s.agModelLabel.equalsIgnoreCase("claude");
            JsonArrayConst groups = doc["groups"].as<JsonArrayConst>();
            JsonObjectConst chosenGroup;
            for (JsonObjectConst g : groups) {
                const char* name = g["displayName"] | "";
                if (want3P && (strstr(name, "3p") || strstr(name, "Claude"))) {
                    chosenGroup = g; break;
                } else if (!want3P && (strstr(name, "Gemini") || strstr(name, "gemini"))) {
                    chosenGroup = g; break;
                }
            }
            if (chosenGroup.isNull() && groups.size() > 0) {
                chosenGroup = groups[0];
            }
            if (chosenGroup.isNull()) return false;

            auto rem = [](float fraction) {
                float v = fraction * 100.0f;
                if (v < 0.0f) v = 0.0f;
                if (v > 100.0f) v = 100.0f;
                return v;
            };

            for (JsonObjectConst b : chosenGroup["buckets"].as<JsonArrayConst>()) {
                const char* bid = b["bucketId"] | "";
                float fraction = b["remainingFraction"] | 0.0f;
                const char* rt = b["resetTime"] | "";
                time_t rst = rt ? TimeUtil::parseIso8601(rt) : 0;

                if (strstr(bid, "5h")) {
                    next.primaryPct    = rem(fraction);
                    next.primaryReset  = rst;
                    next.primaryWinSec = 18000;
                } else if (strstr(bid, "weekly")) {
                    next.secondaryPct    = rem(fraction);
                    next.secondaryReset  = rst;
                    next.secondaryWinSec = 604800;
                }
            }
            parsedOk = true;
            return true;
        });

    s_dbgAgHttp = r.code;
    if (r.code == 401) {
        s_agAccessToken = "";
        s_agTokenExpires = 0;
    }
    if (r.code != 200) {
        char e[24]; transportErr(r.code, e, sizeof(e));
        return failSource(s_agPol, out, r, e, "antigravity");
    }
    if (!parsedOk) {
        char e[24]; snprintf_P(e, sizeof(e), PSTR("JSON %s"), s_dbgAgParse[0] ? s_dbgAgParse : "err");
        TlsResult p; p.code = 0;
        return failSource(s_agPol, out, p, e, "antigravity");
    }

    time_t t = time(nullptr);
    if (t > 1000000000L) {
        struct tm tm; localtime_r(&t, &tm);
        int h = tm.tm_hour;
        next.hourlyPct[h]   = (uint8_t)(next.primaryPct < 0 ? 0 : next.primaryPct);
        next.hourlyValid[h] = true;
    }

    out = next;
    okSource(s_agPol, out);
    return true;
}

// ── Codex ────────────────────────────────────────────────────────────────────

static void codexHeaders(HTTPClient& h, const Settings& s, const String& authVal) {
    h.addHeader("Authorization", authVal);
    h.addHeader("Accept",        "application/json");
    // Clean M2M user-agent matching official CLI client behavior (avoid fake browser headers)
    h.setUserAgent("Codex-Monitor/1.0");
    if (!s.codexDeviceId.isEmpty()) h.addHeader("oai-device-id", s.codexDeviceId);
}

static bool codexJwtExpired(const CodexData& d) {
    time_t now = time(nullptr);
    return d.jwtExp && now > 1000000000L && now >= d.jwtExp;
}

bool Api::fetchCodex(const Settings& s, CodexData& out) {
    if (s.codexToken.isEmpty() || codexJwtExpired(out)) return false;

    String authVal = "Bearer " + s.codexToken;
    CodexData next = out;
    TlsResult r = tlsGetStream("https://chatgpt.com/backend-api/wham/usage",
        [&](HTTPClient& h){ codexHeaders(h, s, authVal); },
        [&](Stream& body) {
            JsonDocument filter;
            UsageParse::codexFilter(filter);
            JsonDocument doc;
            DeserializationError e = deserializeJson(doc, body, DeserializationOption::Filter(filter));
            if (e) { Serial.printf_P(PSTR("[codex] parse: %s\n"), e.c_str()); return false; }
            return UsageParse::codex(doc.as<JsonVariantConst>(), next);
        });
    if (r.code != 200) {
        char e[24]; transportErr(r.code, e, sizeof(e));
        return failSource(s_codexPol, out, r, e, "codex");
    }
    if (!r.parsed) {
        TlsResult p; p.code = 0;
        return failSource(s_codexPol, out, p, "No data", "codex");
    }
    next.resets = out.resets;                // owned by fetchCodexResets
    out = next;
    okSource(s_codexPol, out);
    return true;
}

bool Api::fetchCodexResets(const Settings& s, CodexData& out) {
    if (s.codexToken.isEmpty() || codexJwtExpired(out)) return false;
    String authVal = "Bearer " + s.codexToken;
    ResetGrant g;
    TlsResult r = tlsGetStream("https://chatgpt.com/backend-api/wham/rate-limit-reset-credits",
        [&](HTTPClient& h){ codexHeaders(h, s, authVal); },
        [&](Stream& body) {
            JsonDocument filter;
            UsageParse::codexResetsFilter(filter);
            JsonDocument doc;
            if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;
            return UsageParse::codexResets(doc.as<JsonVariantConst>(), g);
        });
    if (r.code != 200 || !r.parsed) {
        Serial.printf_P(PSTR("[codex] resets fetch → %d\n"), r.code);
        return false;
    }
    out.resets = g;
    return true;
}
