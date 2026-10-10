#pragma once
#include <Arduino.h>
#include "api.h"
#include "storage.h"

// Context passed to every channel render.
struct ChannelCtx {
    const Settings*        settings;
    const AntigravityData* antigravity;
    uint32_t               now_ms;     // millis() at draw time
};

// A channel = name + enabled-predicate + full-draw + optional tick.
//
// PARTIAL REDRAW DISCIPLINE (the core architectural rule):
//
//   draw(ctx)  — Full repaint. Called once when the channel becomes active.
//                Calls Display::clear() and paints every pixel from scratch.
//
//   tick(ctx)  — Optional. Called ~1 Hz while the channel is active. MUST NOT
//                call Display::clear() or fillScreen. Only repaints the
//                specific pixel regions whose value changed since the last
//                tick/draw. Gallery uploads composed scanlines without a
//                preliminary clear; other pages only repaint changed text.
//
// To add a new channel: drop one .cpp into src/channels/ + add a row to
// main.cpp's kChannels[] table. No other code changes.
struct Channel {
    const char*  name;
    bool       (*enabled)(const ChannelCtx&);
    void       (*draw)   (const ChannelCtx&);
    void       (*tick)   (const ChannelCtx&);   // may be nullptr
};
