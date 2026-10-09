#include "channel.h"
#include "display.h"
#include "gallery.h"

namespace {
Gallery::Frame frame;
uint16_t pixels[Gallery::Width]; // 480 B instead of a 115 KB full framebuffer
uint32_t generatedAt = 0, generatedCount = 0, lastDecodeUs = 0;
void newUniverse(uint32_t now) {
    uint32_t seed;
    do { seed = os_random(); } while (!seed || seed == frame.seed);
    Display::releaseFont();
    uint32_t started = micros();
    Gallery::generate(frame, seed, []() { yield(); });
    lastDecodeUs = micros() - started;
    generatedAt = now; ++generatedCount;
}
void paintFrame() {
    // Each row is composed off-screen and uploaded once. The panel never sees
    // the background clear or the intermediate stripe layers.
    bool swap = tft.getSwapBytes();
    tft.setSwapBytes(true);
    for (int y = 0; y < Gallery::Height; ++y) {
        Gallery::scanline(frame, y, pixels);
        tft.pushImage(0, y, Gallery::Width, 1, pixels);
        if ((y & 7) == 0) yield();
    }
    tft.setSwapBytes(swap);
}
}
bool chGalleryEnabled(const ChannelCtx& ctx) { return ctx.settings && ctx.settings->showGallery; }
void chGalleryDraw(const ChannelCtx& ctx) {
    if (!frame.seed || ctx.now_ms - generatedAt >= ctx.settings->galleryRefreshSec * 1000UL)
        newUniverse(ctx.now_ms);
    paintFrame();
}
void chGalleryTick(const ChannelCtx& ctx) {
    if (ctx.now_ms - generatedAt < ctx.settings->galleryRefreshSec * 1000UL) return;
    newUniverse(ctx.now_ms);
    paintFrame(); // region repaint; never clear the screen during tick
}
uint32_t gallerySeed() { return frame.seed; }
uint32_t galleryCount() { return generatedCount; }
uint32_t galleryDecodeUs() { return lastDecodeUs; }

const Gallery::Frame* galleryFrame() { return &frame; }
