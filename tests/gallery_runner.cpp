#include "gallery.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <cmath>

int main(int argc, char** argv) {
    static Gallery::Frame f;
    Gallery::generate(f, argc > 1 ? uint32_t(strtoul(argv[1], nullptr, 10)) : 1);
    assert(f.count >= 2 && f.count <= Gallery::MaxStripes);
    uint16_t row[Gallery::Width];
    if (argc > 2) {
        FILE* out = fopen(argv[2], "wb"); assert(out);
        fprintf(out, "P6\n240 240\n255\n");
        for (int y = 0; y < Gallery::Height; ++y) {
            Gallery::scanline(f, y, row);
            for (auto c : row) {
                unsigned char rgb[] = { (unsigned char)(((c >> 11) & 31) * 255 / 31), (unsigned char)(((c >> 5) & 63) * 255 / 63), (unsigned char)((c & 31) * 255 / 31) };
                fwrite(rgb, 1, 3, out);
            }
        }
        fclose(out);
    }
    uint32_t pixelHash = 2166136261u;
    for (int y = 0; y < Gallery::Height; ++y) {
        Gallery::scanline(f, y, row);
        for (uint16_t c : row) {
            pixelHash = (pixelHash ^ (c >> 8)) * 16777619u;
            pixelHash = (pixelHash ^ (c & 255)) * 16777619u;
        }
    }
    printf("{\"pixel_hash\":%u,", pixelHash);
    printf("\"seed\":%u,\"frame_bytes\":%zu,\"background\":%u,\"edge\":%u,\"raw\":[", f.seed, sizeof(f), Gallery::Background, f.edgeExpand);
    for (int i = 0; i < 64; ++i) { assert(std::isfinite(f.raw[i])); printf("%s%.9g", i ? "," : "", f.raw[i]); }
    printf("],\"stripes\":[");
    for (int i = 0; i < f.count; ++i) {
        const auto& s = f.stripes[i];
        assert(s.pos < 240 && s.size > 0 && s.pos + s.size <= 240);
        if (i) assert(f.stripes[i - 1].z <= s.z);
        printf("%s[%u,%u,%u,%u]", i ? "," : "", s.pos, s.size, s.color, s.vertical ? 1 : 0);
    }
    printf("]}\n");
}
