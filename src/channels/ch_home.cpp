// Home — "vital signs" with partial-redraw discipline.
//
// chHomeDraw  → full paint, seeds cache.
// chHomeTick  → 5 Hz, diffs cache vs current, repaints only changed regions.
// Per channel.h's PARTIAL REDRAW DISCIPLINE — tick MUST NOT clear/fillScreen.
//
//   y=6..82    VT323-86 HH ink, ":" coral, MM amber          [hero clock]
//   y=92..104  date row "SUN MAY 17"
//   y=108      dots divider
//   y=114..128 CL meter row
//   y=132..146 CX meter row
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
static float   s_ag = -2.f, s_cx = -2.f;
static int     s_loadDot = -1;
static int s_date = -1;
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
                       float pct, uint16_t barColor, const char* val) {
    tft.fillRect(0, y, SCREEN_W, 16, Theme::BG);
    Display::useFont("Silkscreen-12");
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(tagColor, Theme::BG);
    tft.drawString(tag, 10, y);

    Display::pixelBar(40, y + 3, 150, 7, pct, barColor);

    Display::useFont("DMMono-11");
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(barColor, Theme::BG);
    tft.drawString(val, SCREEN_W - 10, y);
}

// Loading variant: tag on the left, a compact 3-dot chaser where the value
// would be. No bar — reads clearly as "not here yet".
static void paintMeterLoading(int y, const char* tag, uint16_t tagColor,
                              int lit, uint16_t accent) {
    tft.fillRect(0, y, SCREEN_W, 16, Theme::BG);
    Display::useFont("Silkscreen-12");
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(tagColor, Theme::BG);
    tft.drawString(tag, 10, y);
    Display::loadingDots(SCREEN_W - 10 - 24, y + 4, lit, accent, 3);
}

static void paintAG(float ag, bool loading, int lit) {
    if (loading) { paintMeterLoading(114, "AG", Theme::BLUE, lit, Theme::BLUE); return; }
    char buf[8];
    if (ag >= 0) snprintf(buf, sizeof(buf), "%.0f%%", ag);
    else         snprintf(buf, sizeof(buf), "--");
    paintMeter(114, "AG", Theme::BLUE, ag < 0 ? 0 : ag,
               Display::usageColor(ag), buf);
}

static void paintCX(float cx, bool loading, int lit) {
    if (loading) { paintMeterLoading(132, "CX", Theme::LILAC, lit, Theme::LILAC); return; }
    char buf[8];
    if (cx >= 0) snprintf(buf, sizeof(buf), "%.0f%%", cx);
    else         snprintf(buf, sizeof(buf), "--");
    paintMeter(132, "CX", Theme::LILAC, cx < 0 ? 0 : cx,
               Display::usageColor(cx), buf);
}

static void paintHourStrip(int curHour) {
    // Layout: TODAY label at y=154 (Silkscreen-12, spans y=154..167).
    // Strip baseline at y=180; current-hour marker at y=172..175; tallest tick
    // up to 6 px above baseline (y=174..180). Clear region y=170..182 keeps
    // 3 px clearance below TODAY.
    int stripX = 10, stripY = 180, stripW = SCREEN_W - 20;
    tft.fillRect(stripX, stripY - 10, stripW, 12, Theme::BG);
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

    // "Nh LEFT" right at y=154 (same line as TODAY label)
    char leftBuf[24];
    snprintf(leftBuf, sizeof(leftBuf), "%dh LEFT", 23 - curHour);
    tft.fillRect(SCREEN_W - 80, 152, 76, 14, Theme::BG);
    Display::useFont("DMMono-11");
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(Theme::INK_DIM, Theme::BG);
    tft.drawString(leftBuf, SCREEN_W - 10, 154);
}

// ── Full repaint ──

void chHomeDraw(const ChannelCtx& ctx) {
    Display::clear();
    // Note: the "vital signs" Home design has no top status bar — the clock
    // takes the top of the canvas, and the chrome is the footer (HOME | IP).

    time_t now = time(nullptr);
    struct tm tmv; localtime_r(&now, &tmv);

    // Hero clock
    paintHH(now > 1000000000L ? tmv.tm_hour : 0);
    paintColon();
    paintMM(now > 1000000000L ? tmv.tm_min : 0);

    paintDate(tmv, now > 1000000000L);
    s_date = now > 1000000000L ? tmv.tm_yday : -1;

    Display::dotsDivider(10, 108, SCREEN_W - 20);

    // AI meters — "loading" only for a *configured* side (an unconfigured slot
    // stays valid=false/err="" and must read as "--", not a perpetual loader).
    const bool agLoading = ctx.antigravity && !ctx.settings->agToken.isEmpty()   && antigravityLoading(*ctx.antigravity);
    const bool cxLoading = ctx.codex       && !ctx.settings->codexToken.isEmpty() && codexLoading(*ctx.codex);
    float ag = ctx.antigravity ? Api::antigravityHeroPct(*ctx.settings, *ctx.antigravity) : -1.f;
    float cx = ctx.codex       ? Api::codexHeroPct(*ctx.settings, *ctx.codex) : -1.f;
    const int lit = (ctx.now_ms / 150) % 3;
    paintAG(ag, agLoading, lit);
    paintCX(cx, cxLoading, lit);
    s_loadDot = (agLoading || cxLoading) ? lit : -1;

    Display::dotsDivider(10, 152, SCREEN_W - 20);

    // "TODAY" label at y=154 — paintHourStrip clears from y=170 so no overlap.
    Display::useFont("Silkscreen-12");
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(Theme::MUTED, Theme::BG);
    tft.drawString("TODAY", 10, 154);
    paintHourStrip(tmv.tm_hour);

    // Footer
    Display::useFont("DMMono-11");
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(Theme::MUTED, Theme::BG);
    tft.drawString("HOME", 10, 200);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(WiFi.localIP().toString(), SCREEN_W - 10, 200);

    // ── Seed cache ──
    s_hh = now > 1000000000L ? tmv.tm_hour : -1; s_mm = now > 1000000000L ? tmv.tm_min : -1; s_dayHour = tmv.tm_hour;
    s_ag = (ag < 0) ? -2.f : ag;
    s_cx = (cx < 0) ? -2.f : cx;

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

    // AI meters — chase dots while a side is still loading; else hysteresis on
    // ±0.4% so noise doesn't thrash.
    const bool agLoading = ctx.antigravity && !ctx.settings->agToken.isEmpty()  && antigravityLoading(*ctx.antigravity);
    const bool cxLoading = ctx.codex       && !ctx.settings->codexToken.isEmpty() && codexLoading(*ctx.codex);
    const float ag = ctx.antigravity ? Api::antigravityHeroPct(*ctx.settings, *ctx.antigravity) : -1.f;
    const float cx = ctx.codex       ? Api::codexHeroPct(*ctx.settings, *ctx.codex) : -1.f;
    const int lit = (ctx.now_ms / 150) % 3;

    if (agLoading) {
        if (lit != s_loadDot) paintAG(ag, true, lit);
        s_ag = -2.f;                                  // force repaint when data lands
    } else {
        float ag_eff = (ag < 0) ? -2.f : ag;
        if (fabsf(ag_eff - s_ag) > 0.4f) { paintAG(ag, false, lit); s_ag = ag_eff; }
    }
    if (cxLoading) {
        if (lit != s_loadDot) paintCX(cx, true, lit);
        s_cx = -2.f;
    } else {
        float cx_eff = (cx < 0) ? -2.f : cx;
        if (fabsf(cx_eff - s_cx) > 0.4f) { paintCX(cx, false, lit); s_cx = cx_eff; }
    }
    if (agLoading || cxLoading) s_loadDot = lit;
}
