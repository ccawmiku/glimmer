#pragma once
#include <Arduino.h>
#include <time.h>
#include "storage.h"

struct AntigravityAccount {
    char   email[48] = "";
    char   name[24]  = "";
    char   tag[8]    = "";       // 短标签，用于 Home 显示，如 "AG" 或 "A1"、"A2"
    float  primaryPct     = -1.0f; // 5h 剩余配额百分比
    float  secondaryPct   = -1.0f; // 7d (weekly) 剩余配额百分比
    time_t primaryReset   = 0;
    time_t secondaryReset = 0;
    bool   isCurrent      = false;
    bool   isDisabled     = false;
    bool   isLimited      = false;
};

static constexpr int kMaxAgAccounts = 4;

struct AntigravityData {
    int  accountCount = 0;
    AntigravityAccount accounts[kMaxAgAccounts];

    int    currentIdx     = 0;
    float  primaryPct     = -1.0f;
    float  secondaryPct   = -1.0f;
    time_t primaryReset   = 0;
    time_t secondaryReset = 0;
    long   primaryWinSec  = 18000;   // 5h default
    long   secondaryWinSec = 604800; // weekly default
    char   secondaryTag[16] = "WEEKLY";
    float  creditsRemain  = -1.0f;
    bool   valid = false;
    char   err[24] = "";
    uint8_t hourlyPct[24] = {};
    bool    hourlyValid[24] = {};
};

// "Loading" = configured but never successfully fetched, with no error yet.
inline bool antigravityLoading(const AntigravityData& d) { return !d.valid && !d.err[0]; }

namespace Api {
    inline float antigravityHeroPct(const Settings& s, const AntigravityData& d) {
        if (!d.valid) return -1.f;
        bool realSecondary = d.secondaryPct >= 0 && d.secondaryTag[0] == '\0';
        return (realSecondary && s.agWeeklyHero) ? d.secondaryPct : d.primaryPct;
    }

    inline float accountHeroPct(const Settings& s, const AntigravityAccount& acc) {
        if (s.agWeeklyHero && acc.secondaryPct >= 0) return acc.secondaryPct;
        return acc.primaryPct;
    }

    // Pulls Antigravity Tools (/api/accounts).
    bool fetchAntigravity(const Settings& s, AntigravityData& out);

    // Helpers for displaying countdowns.
    String formatCountdown(time_t t);

    // Debug telemetry from the last Antigravity usage fetch (surfaced in /api/state).
    int  lastAgHttp();
    int  lastAgBodyLen();
    const char* lastAgParse();
}
