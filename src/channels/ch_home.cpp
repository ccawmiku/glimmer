// Home — "vital signs" with partial-redraw discipline.
//
// chHomeDraw  → full paint, seeds cache.
// chHomeTick  → 5 Hz, diffs cache vs current, repaints only changed regions.
// Per channel.h's PARTIAL REDRAW DISCIPLINE — tick MUST NOT clear/fillScreen.
//
//   y=6..82    VT323-86 HH ink, ":" coral, MM amber          [hero clock]
//   y=92..104  date row "2026-10-10"
//   y=108      dots divider
//   y=114..128 Antigravity row 1 (AG / A1)
//   y=132..146 Antigravity row 2 (A2, only if >= 2 accounts)
//   y=152      dots divider
//   y=158..172 "TODAY" + "Nh LEFT"
//   y=178..    24-hour strip with current-hour amber marker
//   y=200..    footer "HOME | IP"

#include "channel.h"
#include "display.h"
#include "theme.h"
#include "config.h"
#include <ESP8266WiFi.h>
#include <time.h>
#include <math.h>

// ── File-static cache so tick() can diff vs last paint ──
static int     s_hh = -1, s_mm = -1, s_dayHour = -1;
static float   s_ag1_5h = -2.f, s_ag1_7d = -2.f;
static float   s_ag2_5h = -2.f, s_ag2_7d = -2.f;
static int     s_renderedMode = -1;
static int     s_loadDot = -1;
static int     s_date = -1;
// Clock x-geometry cached on first paint
static bool    s_geomReady = false;
static int     s_hhX = 8, s_colonX = 0, s_mmX = 0, s_digitW = 0;

bool chHomeEnabled(const ChannelCtx& ctx) {
    return ctx.settings && ctx.settings->showHome;
}

static void clockGeom() {
    if (s_geomReady) return;
    Display::useFont("VT323-86");
    s_digitW = tft.textWidth("0");
    int colonW = tft.textWidth(":");
    s_hhX = (SCREEN_W - (s_digitW * 4 + colonW)) / 2;
    s_colonX = s_hhX + s_digitW * 2;
    s_mmX = s_colonX + colonW;
    s_geomReady = true;
}

// ── Per-region paint helpers ──

static void paintHH(int hh) {
    clockGeom();
    char b[4]; snprintf(b, sizeof(b), "%02d", hh);
    tft.fillRect(s_hhX, 6, s_digitW * 2, 86, Theme::BG);
    Display::useFont("VT323-86");
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(Theme::INK, Theme::BG);
    tft.drawString(b, s_hhX, 6);
}

static void paintColon() {
    clockGeom();
    Display::useFont("VT323-86");
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(Theme::CORAL, Theme::BG);
    tft.drawString(":", s_colonX, 6);
}

static void paintMM(int mm) {
    clockGeom();
    char b[4]; snprintf(b, sizeof(b), "%02d", mm);
    tft.fillRect(s_mmX, 6, s_digitW * 2, 86, Theme::BG);
    Display::useFont("VT323-86");
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(Theme::AMBER, Theme::BG);
    tft.drawString(b, s_mmX, 6);
}

static void paintDate(const struct tm& tmv, bool synced) {
    tft.fillRect(0, 92, SCREEN_W, 14, Theme::BG);
    char b[24];
    if (synced) strftime(b, sizeof(b), "%Y-%m-%d", &tmv);
    else snprintf(b, sizeof(b), "NTP syncing...");
    Display::useFont("DMMono-11");
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(Theme::INK_DIM, Theme::BG);
    tft.drawString(b, 10, 92);
}

static void paintMeter(int y, const char* tag, uint16_t tagColor,
                       float pct, uint16_t barColor, const char* val,
                       uint16_t valColor, int barHeight = 7) {
    tft.fillRect(0, y, SCREEN_W, 15, Theme::BG);
    Display::useFont("Silkscreen-12");
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(tagColor, Theme::BG);
    tft.drawString(tag, 10, y);

    int barY = y + (barHeight >= 7 ? 3 : 5);
    Display::pixelBar(40, barY, 150, barHeight, pct, barColor);

    Display::useFont("DMMono-11");
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(valColor, Theme::BG);
    tft.drawString(val, SCREEN_W - 10, y);
}

static void paintMeterLoading(int y, const char* tag, uint16_t tagColor,
                              int lit, uint16_t accent) {
    tft.fillRect(0, y, SCREEN_W, 15, Theme::BG);
    Display::useFont("Silkscreen-12");
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(tagColor, Theme::BG);
    tft.drawString(tag, 10, y);
    Display::loadingDots(SCREEN_W - 10 - 24, y + 4, lit, accent, 3);
}

static void paint5hMeter(int y, const char* tag, uint16_t tagColor,
                         float pct, bool loading, int lit) {
    if (loading) {
        paintMeterLoading(y, tag, tagColor, lit, tagColor);
        return;
    }
    char buf[8];
    if (pct >= 0) snprintf(buf, sizeof(buf), "%.0f%%", pct);
    else         snprintf(buf, sizeof(buf), "--");
    uint16_t c = (pct >= 0) ? Display::usageColor(pct) : Theme::MUTED;
    paintMeter(y, tag, tagColor, pct < 0 ? 0 : pct, c, buf, c, 7);
}

static void paint7dMeter(int y, const char* tag, float pct, bool loading, int lit) {
    if (loading) {
        paintMeterLoading(y, tag, Theme::AMBER, lit, Theme::AMBER);
        return;
    }
    char buf[8];
    if (pct >= 0) snprintf(buf, sizeof(buf), "%.0f%%", pct);
    else         snprintf(buf, sizeof(buf), "--");
    uint16_t c = (pct >= 0) ? Theme::AMBER : Theme::MUTED;
    paintMeter(y, tag, Theme::AMBER, pct < 0 ? 0 : pct, c, buf, c, 3);
}

static void paintHourStrip(int curHour) {
    int stripX = 10, stripY = 220, stripW = SCREEN_W - 20;
    tft.fillRect(stripX, stripY - 10, stripW, 14, Theme::BG);
    tft.drawFastHLine(stripX, stripY, stripW, Theme::LINE);
    for (int h = 0; h < 24; h++) {
        int tx = stripX + (stripW * h) / 24;
        uint16_t c = (h == curHour) ? Theme::CORAL
                   : (h % 6 == 0)   ? Theme::INK_DIM
                                    : Theme::LINE;
        int th = (h == curHour) ? 6 : (h % 6 == 0 ? 3 : 2);
        tft.drawFastVLine(tx, stripY - th, th, c);
    }
    int curX = stripX + (stripW * curHour) / 24;
    tft.fillRect(curX - 1, stripY - 8, 3, 3, Theme::AMBER);

    char leftBuf[24];
    snprintf(leftBuf, sizeof(leftBuf), "%dh LEFT", 23 - curHour);
    tft.fillRect(SCREEN_W - 80, 194, 76, 14, Theme::BG);
    Display::useFont("DMMono-11");
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(Theme::INK_DIM, Theme::BG);
    tft.drawString(leftBuf, SCREEN_W - 10, 194);
}

// ── Full repaint ──

void chHomeDraw(const ChannelCtx& ctx) {
    Display::clear();

    time_t now = time(nullptr);
    struct tm tmv; localtime_r(&now, &tmv);

    // Hero clock
    paintHH(now > 1000000000L ? tmv.tm_hour : 0);
    paintColon();
    paintMM(now > 1000000000L ? tmv.tm_min : 0);

    paintDate(tmv, now > 1000000000L);
    s_date = now > 1000000000L ? tmv.tm_yday : -1;

    Display::dotsDivider(10, 106, SCREEN_W - 20);

    // Antigravity meters — dynamic 1 pair (single) or 2 pairs (dual)
    const AntigravityData* agData = ctx.antigravity;
    const bool loading = agData && antigravityLoading(*agData);
    const int lit = (ctx.now_ms / 150) % 3;

    int accCount = (agData && agData->valid) ? agData->accountCount : 0;

    if (loading) {
        paint5hMeter(120, "5H", Theme::BLUE, -1.f, true, lit);
        paint7dMeter(142, "7D", -1.f, true, lit);
        tft.fillRect(0, 158, SCREEN_W, 28, Theme::BG);
        s_renderedMode = 0;
        s_loadDot = lit;
        s_ag1_5h = -2.f; s_ag1_7d = -2.f;
        s_ag2_5h = -2.f; s_ag2_7d = -2.f;
    } else if (accCount <= 1) {
        // 单账号模式 (5h 额度 + 7d 细黄条周额度)
        float p1_5h = (accCount == 1) ? agData->accounts[0].primaryPct : -1.f;
        float p1_7d = (accCount == 1) ? agData->accounts[0].secondaryPct : -1.f;
        paint5hMeter(120, "5H", Theme::BLUE, p1_5h, false, lit);
        paint7dMeter(142, "7D", p1_7d, false, lit);
        // 清空下方空余行
        tft.fillRect(0, 158, SCREEN_W, 28, Theme::BG);
        s_renderedMode = 1;
        s_loadDot = -1;
        s_ag1_5h = (p1_5h < 0) ? -2.f : p1_5h;
        s_ag1_7d = (p1_7d < 0) ? -2.f : p1_7d;
        s_ag2_5h = -2.f; s_ag2_7d = -2.f;
    } else {
        // 双账号模式 (A1 5h + 7d, A2 5h + 7d)
        float p1_5h = agData->accounts[0].primaryPct;
        float p1_7d = agData->accounts[0].secondaryPct;
        float p2_5h = agData->accounts[1].primaryPct;
        float p2_7d = agData->accounts[1].secondaryPct;

        uint16_t col1 = agData->accounts[0].isCurrent ? Theme::BLUE : Theme::SKY;
        uint16_t col2 = agData->accounts[1].isCurrent ? Theme::BLUE : Theme::LILAC;

        paint5hMeter(112, "A1", col1, p1_5h, false, lit);
        paint7dMeter(128, "7D", p1_7d, false, lit);
        paint5hMeter(150, "A2", col2, p2_5h, false, lit);
        paint7dMeter(166, "7D", p2_7d, false, lit);

        s_renderedMode = 2;
        s_loadDot = -1;
        s_ag1_5h = (p1_5h < 0) ? -2.f : p1_5h;
        s_ag1_7d = (p1_7d < 0) ? -2.f : p1_7d;
        s_ag2_5h = (p2_5h < 0) ? -2.f : p2_5h;
        s_ag2_7d = (p2_7d < 0) ? -2.f : p2_7d;
    }

    Display::dotsDivider(10, 188, SCREEN_W - 20);

    // "TODAY" label (下移)
    Display::useFont("Silkscreen-12");
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(Theme::MUTED, Theme::BG);
    tft.drawString("TODAY", 10, 194);
    paintHourStrip(tmv.tm_hour);

    // ── Seed cache ──
    s_hh = now > 1000000000L ? tmv.tm_hour : -1;
    s_mm = now > 1000000000L ? tmv.tm_min : -1;
    s_dayHour = tmv.tm_hour;
}

// ── Tick: 5 Hz, region-only repaints ──

void chHomeTick(const ChannelCtx& ctx) {
    time_t now = time(nullptr);
    if (now < 1000000000L) return;
    struct tm tmv; localtime_r(&now, &tmv);

    // Clock
    if (tmv.tm_min != s_mm) { paintMM(tmv.tm_min); s_mm = tmv.tm_min; }
    if (tmv.tm_hour != s_hh) { paintHH(tmv.tm_hour); s_hh = tmv.tm_hour; }
    if (tmv.tm_hour != s_dayHour) {
        paintHourStrip(tmv.tm_hour);
        s_dayHour = tmv.tm_hour;
    }

    if (tmv.tm_yday != s_date) { paintDate(tmv, true); s_date = tmv.tm_yday; }

    // Antigravity meters tick
    const AntigravityData* agData = ctx.antigravity;
    const bool loading = agData && antigravityLoading(*agData);
    const int lit = (ctx.now_ms / 150) % 3;

    int accCount = (agData && agData->valid) ? agData->accountCount : 0;

    if (loading) {
        if (s_renderedMode != 0) {
            tft.fillRect(0, 110, SCREEN_W, 76, Theme::BG);
            s_renderedMode = 0;
            s_ag1_5h = -2.f; s_ag1_7d = -2.f;
            s_ag2_5h = -2.f; s_ag2_7d = -2.f;
        }
        if (lit != s_loadDot) {
            paint5hMeter(120, "5H", Theme::BLUE, -1.f, true, lit);
            paint7dMeter(142, "7D", -1.f, true, lit);
            s_loadDot = lit;
        }
    } else if (accCount <= 1) {
        // 单账号模式
        if (s_renderedMode != 1) {
            tft.fillRect(0, 110, SCREEN_W, 76, Theme::BG);
            s_renderedMode = 1;
            s_ag1_5h = -2.f; s_ag1_7d = -2.f;
            s_ag2_5h = -2.f; s_ag2_7d = -2.f;
        }
        float p1_5h = (accCount == 1) ? agData->accounts[0].primaryPct : -1.f;
        float p1_7d = (accCount == 1) ? agData->accounts[0].secondaryPct : -1.f;
        float p1_5h_eff = (p1_5h < 0) ? -2.f : p1_5h;
        float p1_7d_eff = (p1_7d < 0) ? -2.f : p1_7d;

        if (fabsf(p1_5h_eff - s_ag1_5h) > 0.4f) {
            paint5hMeter(120, "5H", Theme::BLUE, p1_5h, false, lit);
            s_ag1_5h = p1_5h_eff;
        }
        if (fabsf(p1_7d_eff - s_ag1_7d) > 0.4f) {
            paint7dMeter(142, "7D", p1_7d, false, lit);
            s_ag1_7d = p1_7d_eff;
        }
    } else {
        // 多账号模式 (>= 2)
        if (s_renderedMode != 2) {
            tft.fillRect(0, 110, SCREEN_W, 76, Theme::BG);
            s_renderedMode = 2;
            s_ag1_5h = -2.f; s_ag1_7d = -2.f;
            s_ag2_5h = -2.f; s_ag2_7d = -2.f;
        }
        float p1_5h = agData->accounts[0].primaryPct;
        float p1_7d = agData->accounts[0].secondaryPct;
        float p2_5h = agData->accounts[1].primaryPct;
        float p2_7d = agData->accounts[1].secondaryPct;

        float p1_5h_eff = (p1_5h < 0) ? -2.f : p1_5h;
        float p1_7d_eff = (p1_7d < 0) ? -2.f : p1_7d;
        float p2_5h_eff = (p2_5h < 0) ? -2.f : p2_5h;
        float p2_7d_eff = (p2_7d < 0) ? -2.f : p2_7d;

        uint16_t col1 = agData->accounts[0].isCurrent ? Theme::BLUE : Theme::SKY;
        uint16_t col2 = agData->accounts[1].isCurrent ? Theme::BLUE : Theme::LILAC;

        if (fabsf(p1_5h_eff - s_ag1_5h) > 0.4f) {
            paint5hMeter(112, "A1", col1, p1_5h, false, lit);
            s_ag1_5h = p1_5h_eff;
        }
        if (fabsf(p1_7d_eff - s_ag1_7d) > 0.4f) {
            paint7dMeter(128, "7D", p1_7d, false, lit);
            s_ag1_7d = p1_7d_eff;
        }
        if (fabsf(p2_5h_eff - s_ag2_5h) > 0.4f) {
            paint5hMeter(150, "A2", col2, p2_5h, false, lit);
            s_ag2_5h = p2_5h_eff;
        }
        if (fabsf(p2_7d_eff - s_ag2_7d) > 0.4f) {
            paint7dMeter(166, "7D", p2_7d, false, lit);
            s_ag2_7d = p2_7d_eff;
        }
    }
}
