#pragma once
#include <stdint.h>
#include <stddef.h>

// Portable CPU renderer, derived from latent-art-wallpaper (MIT).
// No framebuffer and no retained weight matrices: weights are replayed from a seed.
namespace Gallery {
constexpr int Width = 240;
constexpr int Height = 240;
constexpr int MaxStripes = Width + Height;
constexpr uint16_t Background = 0xffdf; // #fafafa in RGB565
struct Stripe {
    float z;
    uint16_t pos, size, color;
    bool vertical;
};
struct Frame {
    Stripe stripes[MaxStripes];
    float raw[64];
    uint32_t seed = 0;
    uint16_t count = 0;
    uint8_t edgeExpand = 0;
};
using YieldFn = void (*)();
void generate(Frame& frame, uint32_t seed, YieldFn cooperate = nullptr);
void scanline(const Frame& frame, int y, uint16_t* pixels);
uint16_t paletteColor(unsigned index);
}
