#pragma once
#include <Arduino.h>
#include <time.h>
#include "storage.h"

struct ModelSlot {
    float pct = -1.0f;
    char  label[12] = "";
};

struct AntigravityData {
    float  primaryPct     = -1.0f;
    float  secondaryPct   = -1.0f;
    time_t primaryReset   = 0;
    time_t secondaryReset = 0;
    long   primaryWinSec  = 18000;   // 5h default
    long   secondaryWinSec = 604800; // weekly default
    char   secondaryTag[16] = "";
    float  creditsRemain  = -1.0f;
    bool   valid = false;
    char   err[24] = "";
    uint8_t hourlyPct[24] = {};
    bool    hourlyValid[24] = {};
};

struct CodexData {
    float  primaryPct     = -1.0f;
    float  secondaryPct   = -1.0f;
    time_t primaryReset   = 0;
    time_t secondaryReset = 0;
    long   primaryWinSec   = 0;      // primary window length (s) → drives label
    long   secondaryWinSec = 0;      // secondary window length (s) → drives label
    char   secondaryTag[16] = "";    // non-empty when the secondary row comes from
                                     // an additional model limit (e.g. "SPARK")
    float  creditsRemain  = -1.0f;
    bool   valid = false;
    char   err[24] = "";
    uint8_t hourlyPct[24] = {};
    bool    hourlyValid[24] = {};
};

// "Loading" = configured but never successfully fetched, with no error yet.
inline bool antigravityLoading(const AntigravityData& d) { return !d.valid && !d.err[0]; }
inline bool codexLoading      (const CodexData&       d) { return !d.valid && !d.err[0]; }

namespace Api {
    inline float antigravityHeroPct(const Settings& s, const AntigravityData& d) {
        bool realSecondary = d.secondaryPct >= 0 && d.secondaryTag[0] == '\0';
        return (realSecondary && s.agWeeklyHero) ? d.secondaryPct : d.primaryPct;
    }

    inline float codexHeroPct(const Settings& s, const CodexData& d) {
        bool realSecondary = d.secondaryPct >= 0 && d.secondaryTag[0] == '\0';
        return (realSecondary && s.codexWeeklyHero) ? d.secondaryPct : d.primaryPct;
    }

    // Pulls Google Antigravity retrieveUserQuotaSummary.
    bool fetchAntigravity(const Settings& s, AntigravityData& out);

    // Pulls chatgpt.com/backend-api/wham/usage.
    bool fetchCodex(const Settings& s, CodexData& out);

    // Helpers for displaying countdowns.
    String formatCountdown(time_t t);

    // Debug telemetry from the last Antigravity usage fetch (surfaced in /api/state).
    int  lastAgHttp();
    int  lastAgBodyLen();
    const char* lastAgParse();
}
