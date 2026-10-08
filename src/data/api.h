#pragma once
#include <Arduino.h>
#include <ESP8266HTTPClient.h>
#include <functional>
#include <time.h>
#include "storage.h"
#include "usage_types.h"
#include "fetch_policy.h"

namespace Api {
    inline float antigravityHeroPct(const Settings& s, const AntigravityData& d) {
        bool realSecondary = d.secondaryPct >= 0 && d.secondaryTag[0] == '\0';
        return (realSecondary && s.agWeeklyHero) ? d.secondaryPct : d.primaryPct;
    }

    inline float codexHeroPct(const Settings& s, const CodexData& d) {
        bool realSecondary = d.secondaryPct >= 0 && d.secondaryTag[0] == '\0';
        return (realSecondary && s.codexWeeklyHero) ? d.secondaryPct : d.primaryPct;
    }

    // Data counts as stale once it is older than 3 refresh intervals (with a
    // 15-minute floor so backoff on a 1-minute cadence doesn't flap). Stale
    // data stays on screen, dimmed, with a STALE badge.
    bool isStale(time_t lastOk, const Settings& s);
    // Writes "STALE 14M" into buf when stale, "" otherwise.
    void staleText(time_t lastOk, const Settings& s, char* buf, size_t n);

    // Which weekly allowance to spend next, for the AI dashboard:
    //   "USE ANTIGRAV · RESETS 20H" — resets within 48 h with ≥ 25% unused
    //   "MOST ROOM: CODEX 80%"       — otherwise the one with more left
    // "" unless both providers have data.
    void adviceText(const AntigravityData& ag, const CodexData& cx, char* buf, size_t n);

    // One fetch job each. Update the data passed in and the source's policy
    // state (backoff / auth latch / edge block). Return true on success.
    bool fetchAntigravity(const Settings& s, AntigravityData& out);
    bool fetchCodex(const Settings& s, CodexData& out);
    // Hourly: Codex limit-reset credits (separate endpoint).
    bool fetchCodexResets(const Settings& s, CodexData& out);

    // Re-derive the credential state (cheap; call after a fetch, on settings
    // change, and periodically so EXPIRING/EXPIRED track the clock). Returns
    // true when a credential itself changed (a new key was pasted).
    bool refreshCred(const Settings& s, AntigravityData& ag, CodexData& cx);

    const FetchPolicy::State& antigravityPolicy();
    const FetchPolicy::State& codexPolicy();
    const FetchPolicy::State& codexResetsPolicy();

    // Helpers for displaying countdowns.
    String formatCountdown(time_t t);

    // Debug telemetry from the last Antigravity usage fetch (surfaced in /api/state).
    int  lastAgHttp();
    int  lastAgBodyLen();
    const char* lastAgParse();

    struct TlsResult {
        int  code = 0;          // HTTP code, negative = transport error
        bool parsed = false;    // what onBody returned (200 only)
        long retryAfter = -1;   // Retry-After in seconds, -1 when absent
        bool markup = false;    // a 401/403 answered with HTML (edge challenge)
    };

    // Shared TLS request streaming the body into `onBody` (called only on 200).
    TlsResult tlsRequestStream(const char* method, const char* url,
                               const std::function<void(HTTPClient&)>& addHeaders,
                               const String& postData,
                               const std::function<bool(Stream&)>& onBody);

    // Shared TLS GET that streams the body into `onBody` (called only on 200).
    TlsResult tlsGetStream(const char* url,
                           const std::function<void(HTTPClient&)>& addHeaders,
                           const std::function<bool(Stream&)>& onBody);

    // Minimum contiguous heap block a TLS handshake needs. The scheduler
    // refuses to start a TLS job below this (refusals don't count as failures).
    constexpr uint32_t kTlsFloor = 20000;
}
