// AI Dashboard — combined Antigravity+Codex glance.
//
// Layout:
//   - StatusBar "AI today" with total credits right
//   - Two big % side-by-side (ANTIGRAVITY blue, CODEX lilac)
//   - PixelBars stacked
//   - Reset countdown rows (real data from API)

#include "channel.h"
#include "display.h"
#include "theme.h"
#include "config.h"
#include "api.h"
#include <math.h>
#include <time.h>

// tick cache
static float s_ag = -2.f, s_cx = -2.f;
static float s_credits = -2.f;
static char  s_agReset[12] = "";
static char  s_cxReset[12] = "";
static int   s_loadDot = -1;

bool chAiDashEnabled(const ChannelCtx& ctx) {
    return ctx.settings && ctx.settings->showAiDash
        && !ctx.settings->agToken.isEmpty()
        && !ctx.settings->codexToken.isEmpty();
}

static void paintAGBlock(float ag, bool loading, int lit) {
    uint16_t uc = Display::usageColor(ag);
    tft.fillRect(10, 48, 100, 28, Theme::BG);
    if (loading) {
        Display::loadingDots(14, 58, lit, Theme::BLUE, 3);
    } else {
        char buf[8];
        if (ag < 0) snprintf(buf, sizeof(buf), "--%%");
        else        snprintf(buf, sizeof(buf), "%.0f%%", ag);
        Display::useFont("VT323-32");
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(uc, Theme::BG);
        tft.drawString(buf, 12, 50);
    }
    Display::pixelBar(12, 92, SCREEN_W - 24, 10, (loading || ag < 0) ? 0 : ag, uc);
}

static void paintCXBlock(float cx, bool loading, int lit) {
    uint16_t uc = Display::usageColor(cx);
    tft.fillRect(SCREEN_W - 110, 48, 100, 28, Theme::BG);
    if (loading) {
        Display::loadingDots(SCREEN_W - 12 - 24, 58, lit, Theme::LILAC, 3);
    } else {
        char buf[8];
        if (cx < 0) snprintf(buf, sizeof(buf), "--%%");
        else        snprintf(buf, sizeof(buf), "%.0f%%", cx);
        Display::useFont("VT323-32");
        tft.setTextDatum(TR_DATUM);
        tft.setTextColor(uc, Theme::BG);
        tft.drawString(buf, SCREEN_W - 12, 50);
    }
    Display::pixelBar(12, 110, SCREEN_W - 24, 10, (loading || cx < 0) ? 0 : cx, uc);
}

static void paintResetRow(int y, const char* tag, uint16_t tagColor, time_t resetEpoch) {
    tft.fillRect(0, y, SCREEN_W, 14, Theme::BG);
    Display::useFont("Silkscreen-12");
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(tagColor, Theme::BG);
    tft.drawString(tag, 12, y);

    String cd = Api::formatCountdown(resetEpoch);
    Display::useFont("DMMono-11");
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(Theme::INK_DIM, Theme::BG);
    tft.drawString(cd, SCREEN_W - 12, y);
}

void chAiDashDraw(const ChannelCtx& ctx) {
    Display::clear();

    // Total spent right meta
    char rmeta[16] = "";
    const float credits = ctx.codex && ctx.codex->creditsRemain >= 0 ? ctx.codex->creditsRemain : -1;
    if (credits >= 0) snprintf(rmeta, sizeof(rmeta), "$%.2f", credits);
    Display::statusBar("AI today", rmeta, Theme::INK_DIM);

    const float ag = ctx.antigravity ? Api::antigravityHeroPct(*ctx.settings, *ctx.antigravity) : -1;
    const float cx = ctx.codex       ? Api::codexHeroPct(*ctx.settings, *ctx.codex) : -1;

    // ── Two big numbers side by side, design-true VLW typography ──
    Display::useFont("Silkscreen-12");
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(Theme::BLUE, Theme::BG);
    tft.drawString("ANTIGRAVITY", 12, 32);

    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(Theme::LILAC, Theme::BG);
    tft.drawString("CODEX", SCREEN_W - 12, 32);

    // Hero %s + segmented bars (via helpers so tick can reuse)
    const bool agLoading = ctx.antigravity && antigravityLoading(*ctx.antigravity);
    const bool cxLoading = ctx.codex       && codexLoading(*ctx.codex);
    const int lit = (ctx.now_ms / 150) % 3;
    paintAGBlock(ag, agLoading, lit);
    paintCXBlock(cx, cxLoading, lit);
    s_loadDot = (agLoading || cxLoading) ? lit : -1;

    Display::dotsDivider(12, 130, SCREEN_W - 24);

    // Reset countdowns — real data from API
    Display::useFont("DMMono-11");
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(Theme::MUTED, Theme::BG);
    tft.drawString("RESETS", 12, 138);

    time_t agReset = ctx.antigravity ? ((ctx.settings && ctx.settings->agWeeklyHero) ? ctx.antigravity->secondaryReset : ctx.antigravity->primaryReset) : 0;
    time_t cxReset = ctx.codex       ? ctx.codex->primaryReset : 0;
    paintResetRow(158, "AG", Theme::BLUE, agReset);
    paintResetRow(174, "CX", Theme::LILAC, cxReset);

    // Seed cache
    s_ag = (ag < 0) ? -2.f : ag;
    s_cx = (cx < 0) ? -2.f : cx;
    s_credits = credits;
    strncpy(s_agReset, Api::formatCountdown(agReset).c_str(), sizeof(s_agReset) - 1);
    strncpy(s_cxReset, Api::formatCountdown(cxReset).c_str(), sizeof(s_cxReset) - 1);
}

void chAiDashTick(const ChannelCtx& ctx) {
    const bool agLoading = ctx.antigravity && antigravityLoading(*ctx.antigravity);
    const bool cxLoading = ctx.codex       && codexLoading(*ctx.codex);
    const float ag = ctx.antigravity ? Api::antigravityHeroPct(*ctx.settings, *ctx.antigravity) : -1.f;
    const float cx = ctx.codex       ? Api::codexHeroPct(*ctx.settings, *ctx.codex) : -1.f;
    const int lit = (ctx.now_ms / 150) % 3;

    if (agLoading) {
        if (lit != s_loadDot) paintAGBlock(ag, true, lit);
        s_ag = -2.f;                                  // force repaint when data lands
    } else {
        float ag_eff = (ag < 0) ? -2.f : ag;
        if (fabsf(ag_eff - s_ag) > 0.4f) { paintAGBlock(ag, false, lit); s_ag = ag_eff; }
    }
    if (cxLoading) {
        if (lit != s_loadDot) paintCXBlock(cx, true, lit);
        s_cx = -2.f;
    } else {
        float cx_eff = (cx < 0) ? -2.f : cx;
        if (fabsf(cx_eff - s_cx) > 0.4f) { paintCXBlock(cx, false, lit); s_cx = cx_eff; }
    }
    if (agLoading || cxLoading) s_loadDot = lit;

    // Credits — repaint just the right meta in the status bar
    const float credits = ctx.codex && ctx.codex->creditsRemain >= 0 ? ctx.codex->creditsRemain : -1.f;
    if (fabsf(credits - s_credits) > 0.005f) {
        char rmeta[16] = "";
        if (credits >= 0) snprintf(rmeta, sizeof(rmeta), "$%.2f", credits);
        tft.fillRect(SCREEN_W - 80, 0, 80, 21, Theme::BG);
        Display::useFont("DMMono-11");
        tft.setTextDatum(MR_DATUM);
        tft.setTextColor(Theme::MUTED, Theme::BG);
        tft.drawString(rmeta, SCREEN_W - 4, 11);
        tft.drawFastHLine(SCREEN_W - 80, 21, 80, Theme::INK_DIM);
        s_credits = credits;
    }

    // Reset countdowns — text has minute granularity, so only recompute (and
    // heap-allocate) the strings when the wall-clock minute rolls over.
    time_t t = time(nullptr);
    struct tm tm; localtime_r(&t, &tm);
    static int s_cdMin = -1;
    if (tm.tm_min != s_cdMin) {
        s_cdMin = tm.tm_min;
        time_t agReset = ctx.antigravity ? ((ctx.settings && ctx.settings->agWeeklyHero) ? ctx.antigravity->secondaryReset : ctx.antigravity->primaryReset) : 0;
        time_t cxReset = ctx.codex       ? ctx.codex->primaryReset : 0;
        String agFresh = Api::formatCountdown(agReset);
        String cxFresh = Api::formatCountdown(cxReset);
        if (strcmp(agFresh.c_str(), s_agReset) != 0) {
            paintResetRow(158, "AG", Theme::BLUE, agReset);
            strncpy(s_agReset, agFresh.c_str(), sizeof(s_agReset) - 1);
        }
        if (strcmp(cxFresh.c_str(), s_cxReset) != 0) {
            paintResetRow(174, "CX", Theme::LILAC, cxReset);
            strncpy(s_cxReset, cxFresh.c_str(), sizeof(s_cxReset) - 1);
        }
    }
}
