// Derived from ccawmiku/latent-art-wallpaper, Copyright (c) 2026 王茂. MIT.
// Full attribution and license: THIRD_PARTY.md, tests/reference/LICENSE.
#include "gallery.h"
#include <math.h>
#include <stdio.h>
#include <algorithm>

namespace Gallery {
namespace {
constexpr int L = 192, H1 = 96, H2 = 64, O = 64;
constexpr int Weights = L * H1 + H1 * H2 + H2 * O;
constexpr float Tau = 6.2831853071795864769f;
struct Rng {
    uint32_t state;
    uint32_t next() {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return state;
    }
    double unit() { return next() / 4294967296.0; }
    float uniform(float limit) { return float((unit() * 2 - 1) * limit); }
    float normal() {
        double u = std::max(unit(), 1e-12), v = std::max(unit(), 1e-12);
        return float(sqrt(-2 * log(u)) * cos(6.2831853071795864769 * v));
    }
};
float sigmoid(float v) { return 1.f / (1.f + expf(-v)); }
float clamp(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }
void dense(const float* in, int ni, float* out, int no, float gain,
           Rng& weights, int activation, const float* bias, YieldFn cooperate) {
    float limit = gain * sqrtf(6.f / (ni + no));
    for (int i = 0; i < no; ++i) {
        // JS stores Float32 weights but accumulates in double.
        double sum = bias ? bias[i] : 0;
        for (int j = 0; j < ni; ++j) sum += double(in[j]) * weights.uniform(limit);
        if (activation == 1) sum = tanh(sum);
        if (activation == 2) sum = std::max(0.0, sum);
        out[i] = float(sum);
        if (cooperate && (i & 7) == 0) cooperate();
    }
}
uint32_t hash(const char* text) {
    uint32_t h = 2166136261u;
    for (; *text; ++text) { h ^= uint8_t(*text); h *= 16777619u; }
    return h;
}
float sample(uint32_t seed, bool vertical, int phase, const char* salt, int index) {
    char text[112];
    snprintf(text, sizeof(text), "%lu:%s:%d:%s:%d", (unsigned long)seed,
             vertical ? "vertical" : "horizontal", phase, salt, index);
    uint32_t x = hash(text);
    x ^= x >> 16; x *= 2246822507u; x ^= x >> 13; x *= 3266489909u; x ^= x >> 16;
    return float(x / 4294967296.0);
}
struct Parameters {
    float density, widthMean, widthJitter, skipRate, minWidth, clusterBias, zBias, paletteBias;
};
Parameters family(const float* raw, int offset, float balance) {
    return {
        (0.006f + sigmoid(raw[offset]) * 0.11f) * 0.62f * balance,
        0.45f + sigmoid(raw[offset + 1]) * 1.85f,
        0.25f + sigmoid(raw[offset + 2]) * 1.75f,
        sigmoid(raw[offset + 3]) * 0.28f,
        0.003f + sigmoid(raw[offset + 4]) * 0.014f,
        sigmoid(raw[offset + 5]) * 2.2f,
        tanhf(raw[offset + 6]) * 0.35f,
        tanhf(raw[offset + 7])
    };
}
void stripes(Frame& f, bool vertical, Parameters p, const uint8_t* indices, int n,
             const float* energies, int phase) {
    const int span = vertical ? Width : Height;
    const float skip = clamp(p.skipRate + sigmoid(f.raw[7]) * 0.08f, 0, 0.42f);
    const float zSpread = 0.15f + sigmoid(f.raw[4]) * 0.85f;
    const float globalZBias = tanhf(f.raw[5]) * 0.25f;
    const float accent = sigmoid(f.raw[6]);
    const int minWidth = std::max(1, int(roundf(span * p.minWidth)));
    float weights[32], total = 0;
    float exponent = 1 + p.paletteBias * (p.paletteBias >= 0 ? 1.8f : 0.6f);
    for (int i = 0; i < n; ++i) {
        weights[i] = powf(sigmoid(std::max(energies[indices[i]], -4.f) * 1.4f) + 0.001f, exponent);
        total += weights[i];
    }
    float current = 0;
    for (int index = 0; current < span && index < span; ++index) {
        float sampled = span * p.density * p.widthMean *
                        (0.35f + powf(sample(f.seed, vertical, phase, "width", index),
                                       1 + p.clusterBias) * p.widthJitter);
        float end = std::min(float(span), current + std::max(sampled, float(minWidth)));
        int startPx = int(roundf(current)), endPx = int(roundf(end));
        endPx = std::max(startPx + 1, endPx);
        if (startPx < span && sample(f.seed, vertical, phase, "keep", index) > skip && f.count < MaxStripes) {
            // Upstream reuses the same salt for both accent and weighted pick.
            float u = sample(f.seed, vertical, phase, "palette:color", index);
            int selected = 0;
            if (u >= accent) {
                float r = u * total;
                for (; selected < n - 1; ++selected) { r -= weights[selected]; if (r <= 0) break; }
            }
            f.stripes[f.count++] = {
                sample(f.seed, vertical, phase, "z", index) * zSpread + p.zBias + globalZBias,
                uint16_t(startPx), uint16_t(endPx - startPx), paletteColor(indices[selected]), vertical
            };
        }
        current = end;
    }
}
} // namespace

uint16_t paletteColor(unsigned index) {
    static const uint32_t colors[32] = {
        0xF8F9FA,0xFDF5E6,0xF0E68C,0xFFD700,0xDAA520,0xFF8C00,0xFF4500,0xE32636,
        0xB22222,0xA0522D,0x8B4513,0x6B4423,0xC71585,0x800080,0x4B0082,0x120A8F,
        0x003399,0x0F52BA,0x48CAE4,0x008080,0x50C878,0x00FA9A,0x228B22,0x808000,
        0x556B2F,0xD2B48C,0xBC8F8F,0x708090,0x778899,0xA9A9A9,0x2F4F4F,0x191919
    };
    uint32_t c = colors[index % 32];
    return ((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x001f);
}

void generate(Frame& f, uint32_t seed, YieldFn cooperate) {
    seed = seed ? seed : 1;
    f.seed = seed; f.count = 0;
    Rng weights{seed}, entity{seed};
    // Replay the original DecoderNetwork constructor, including b3.
    for (int i = 0; i < Weights; ++i) {
        entity.next();
        if (cooperate && (i & 1023) == 0) cooperate();
    }
    float bias[O], latent[L], h1[H1], h2[H2];
    for (int i = 0; i < O; ++i) bias[i] = entity.uniform(0.2f);
    for (int i = 0; i < L; ++i) {
        latent[i] = entity.normal() * 1.2f;
        // Upstream allocates a random velocity; the independent-universe mode
        // never evolves it, but advance the stream to keep the same latent.
        entity.next(); entity.next();
    }
    dense(latent, L, h1, H1, 1.15f, weights, 1, nullptr, cooperate);
    dense(h1, H1, h2, H2, 1.20f, weights, 2, nullptr, cooperate);
    dense(h2, H2, f.raw, O, 1.10f, weights, 0, bias, cooperate);

    float temperature = 0.70f + sigmoid(f.raw[1]) * 1.50f;
    float threshold = f.raw[0] * 0.85f;
    int cap = 1 + int(sigmoid(f.raw[2]) * 31.999f);
    uint8_t indices[32]; float energies[32]; int n = 0, maxIdx = 0;
    for (int i = 0; i < 32; ++i) {
        energies[i] = f.raw[32 + i] / temperature;
        if (energies[i] > energies[maxIdx]) maxIdx = i;
        if (energies[i] > threshold) indices[n++] = i;
    }
    if (!n) indices[n++] = maxIdx;
    std::sort(indices, indices + n, [&](uint8_t a, uint8_t b) { return energies[a] > energies[b]; });
    n = std::min(n, cap);
    float balance = sigmoid(f.raw[25]);
    // Reset phase each independent universe (generation 0, randomness 0.4).
    float tempo = 0.035f + sigmoid(f.raw[26]) * 0.62f;
    float phaseBias = tanhf(f.raw[27]) * 8.f;
    float phaseWarp = sigmoid(f.raw[28]) * 22.f;
    int phase = int(floorf(sinf(phaseBias) * phaseWarp * powf(0.4f, 1.35f) * tempo + phaseBias * 11));
    stripes(f, true, family(f.raw, 8, 0.75f + balance * 0.5f), indices, n, energies, phase);
    stripes(f, false, family(f.raw, 16, 1.25f - balance * 0.5f), indices, n, energies, phase);
    if (f.count < 3) {
        f.stripes[f.count++] = {0, 0, uint16_t(roundf(Width * 0.28f)), paletteColor(0), true};
        f.stripes[f.count++] = {1, uint16_t(roundf(Height * 0.2f)), uint16_t(roundf(Height * 0.38f)), paletteColor(6), false};
    }
    // Insertion sort is stable (same layering as the JS renderer), with no heap.
    for (int i = 1; i < f.count; ++i) {
        Stripe part = f.stripes[i]; int j = i;
        while (j > 0 && f.stripes[j - 1].z > part.z) { f.stripes[j] = f.stripes[j - 1]; --j; }
        f.stripes[j] = part;
    }
    f.edgeExpand = uint8_t(roundf(sigmoid(f.raw[24])));
}

void scanline(const Frame& f, int y, uint16_t* pixels) {
    std::fill(pixels, pixels + Width, Background);
    for (int i = 0; i < f.count; ++i) {
        const Stripe& s = f.stripes[i];
        if (s.vertical) {
            int end = std::min(Width, int(s.pos + s.size + f.edgeExpand));
            std::fill(pixels + s.pos, pixels + end, s.color);
        } else if (y >= s.pos && y < s.pos + s.size + f.edgeExpand) {
            std::fill(pixels, pixels + Width, s.color);
        }
    }
}
} // namespace Gallery
