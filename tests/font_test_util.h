#pragma once

// a tiny but complete TrueType font for the font tests: glyph 1 is a box mapped to 'A', glyph 2 a
// composite of glyph 1 mapped to 'B'

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace imforge {
namespace test {

struct TinyFont {
    std::vector<unsigned char> bytes;
    // offsets of a few fields so tests can break them
    size_t loca = 0, glyf = 0, cmap = 0, composite_child = 0, hhea = 0, dir = 12;
};

inline void Put16(std::vector<unsigned char>& v, uint32_t x) {
    v.push_back((unsigned char)(x >> 8));
    v.push_back((unsigned char)x);
}
inline void Put32(std::vector<unsigned char>& v, uint32_t x) {
    Put16(v, x >> 16);
    Put16(v, x & 0xFFFF);
}
inline void Set16(std::vector<unsigned char>& v, size_t at, uint32_t x) {
    v[at] = (unsigned char)(x >> 8);
    v[at + 1] = (unsigned char)x;
}

inline TinyFont MakeTinyFont() {
    std::vector<unsigned char> head, hhea, maxp, hmtx, loca, glyf, cmap;
    Put32(head, 0x00010000);
    Put32(head, 0x00010000);
    Put32(head, 0);
    Put32(head, 0x5F0F3CF5);
    Put16(head, 0);
    Put16(head, 1000);
    for (int i = 0; i < 16; ++i) head.push_back(0);
    Put16(head, 0);
    Put16(head, 0);
    Put16(head, 600);
    Put16(head, 800);
    Put16(head, 0);
    Put16(head, 8);
    Put16(head, 2);
    Put16(head, 0);
    Put16(head, 0);

    Put32(hhea, 0x00010000);
    Put16(hhea, 800);
    Put16(hhea, 0x10000 - 200);
    Put16(hhea, 0);
    Put16(hhea, 600);
    for (int i = 0; i < 11; ++i) Put16(hhea, 0);
    Put16(hhea, 3);

    Put32(maxp, 0x00005000);
    Put16(maxp, 3);

    for (int i = 0; i < 3; ++i) {
        Put16(hmtx, 600);
        Put16(hmtx, 0);
    }

    // glyph 1: one contour, four on-curve points, 2-byte deltas
    Put16(glyf, 1);
    Put16(glyf, 0);
    Put16(glyf, 0);
    Put16(glyf, 500);
    Put16(glyf, 700);
    Put16(glyf, 3);
    Put16(glyf, 0);
    for (int i = 0; i < 4; ++i) glyf.push_back(0x01);
    const int dx[4] = {0, 500, 0, -500}, dy[4] = {0, 0, 700, 0};
    for (int x : dx) Put16(glyf, (uint32_t)(x & 0xFFFF));
    for (int y : dy) Put16(glyf, (uint32_t)(y & 0xFFFF));
    size_t g2 = glyf.size();
    // glyph 2: composite of glyph 1, word xy offsets
    Put16(glyf, 0xFFFF);
    Put16(glyf, 0);
    Put16(glyf, 0);
    Put16(glyf, 600);
    Put16(glyf, 700);
    Put16(glyf, 0x0003);
    size_t child_at = glyf.size();
    Put16(glyf, 1);
    Put16(glyf, 100);
    Put16(glyf, 0);

    Put16(loca, 0);
    Put16(loca, 0);
    Put16(loca, (uint32_t)(g2 / 2));
    Put16(loca, (uint32_t)(glyf.size() / 2));

    Put16(cmap, 0);
    Put16(cmap, 1);
    Put16(cmap, 3);
    Put16(cmap, 1);
    Put32(cmap, 12);
    Put16(cmap, 4);
    Put16(cmap, 32);
    Put16(cmap, 0);
    Put16(cmap, 4);
    Put16(cmap, 4);
    Put16(cmap, 1);
    Put16(cmap, 0);
    Put16(cmap, 0x42);
    Put16(cmap, 0xFFFF);
    Put16(cmap, 0);
    Put16(cmap, 0x41);
    Put16(cmap, 0xFFFF);
    Put16(cmap, (1u - 0x41u) & 0xFFFF);
    Put16(cmap, 1);
    Put16(cmap, 0);
    Put16(cmap, 0);

    struct T {
        const char* tag;
        std::vector<unsigned char>* data;
    } tables[] = {{"cmap", &cmap}, {"glyf", &glyf}, {"head", &head}, {"hhea", &hhea},
                  {"hmtx", &hmtx}, {"loca", &loca}, {"maxp", &maxp}};
    const size_t count = sizeof(tables) / sizeof(tables[0]);
    TinyFont f;
    std::vector<unsigned char>& out = f.bytes;
    Put32(out, 0x00010000);
    Put16(out, (uint32_t)count);
    Put16(out, 64);
    Put16(out, 2);
    Put16(out, 48);
    size_t off = 12 + 16 * count;
    for (const T& t : tables) {
        out.insert(out.end(), t.tag, t.tag + 4);
        Put32(out, 0);
        Put32(out, (uint32_t)off);
        Put32(out, (uint32_t)t.data->size());
        off += (t.data->size() + 3) & ~size_t(3);
    }
    for (const T& t : tables) {
        if (t.data == &loca) f.loca = out.size();
        if (t.data == &glyf) {
            f.glyf = out.size();
            f.composite_child = out.size() + child_at;
        }
        if (t.data == &cmap) f.cmap = out.size();
        if (t.data == &hhea) f.hhea = out.size();
        out.insert(out.end(), t.data->begin(), t.data->end());
        while (out.size() % 4) out.push_back(0);
    }
    return f;
}

// a font around raw glyf bytes and 32-bit loca offsets (one entry per glyph plus the end), unit box
// metrics and the same 'A'/'B' cmap as the tiny font. glyph 0 is expected to be empty
inline std::vector<unsigned char> BuildFont(const std::vector<unsigned char>& glyf, const std::vector<uint32_t>& offsets) {
    const uint32_t glyphs = (uint32_t)offsets.size() - 1;
    std::vector<unsigned char> head(54, 0), hhea(36, 0), maxp(6, 0), hmtx, loca, cmap, g = glyf;
    head[1] = 1;
    head[12] = 0x5F;
    head[13] = 0x0F;
    head[14] = 0x3C;
    head[15] = 0xF5;
    head[18] = 0x03;
    head[19] = 0xE8;
    head[51] = 1;
    hhea[1] = 1;
    hhea[4] = 0x03;
    hhea[5] = 0x20;
    hhea[6] = 0xFF;
    hhea[7] = 0x38;
    hhea[34] = (unsigned char)(glyphs >> 8);
    hhea[35] = (unsigned char)glyphs;
    maxp[2] = 0x50;
    maxp[4] = (unsigned char)(glyphs >> 8);
    maxp[5] = (unsigned char)glyphs;
    for (uint32_t i = 0; i < glyphs; ++i) {
        Put16(hmtx, 600);
        Put16(hmtx, 0);
    }
    for (uint32_t o : offsets) Put32(loca, o);
    const uint32_t cmap_words[] = {0, 1, 3, 1, 0, 12, 4, 32, 0, 4, 4, 1, 0, 0x42, 0xFFFF, 0, 0x41, 0xFFFF,
                                   (1u - 0x41u) & 0xFFFF, 1, 0, 0};
    for (uint32_t w : cmap_words) Put16(cmap, w);
    struct T {
        const char* tag;
        std::vector<unsigned char>* d;
    } tables[] = {{"cmap", &cmap}, {"glyf", &g}, {"head", &head}, {"hhea", &hhea},
                  {"hmtx", &hmtx}, {"loca", &loca}, {"maxp", &maxp}};
    std::vector<unsigned char> font;
    Put32(font, 0x00010000);
    Put16(font, 7);
    Put16(font, 0);
    Put16(font, 0);
    Put16(font, 0);
    size_t off = 12 + 16 * 7;
    for (const T& t : tables) {
        font.insert(font.end(), t.tag, t.tag + 4);
        Put32(font, 0);
        Put32(font, (uint32_t)off);
        Put32(font, (uint32_t)t.d->size());
        off += (t.d->size() + 3) & ~size_t(3);
    }
    for (const T& t : tables) {
        font.insert(font.end(), t.d->begin(), t.d->end());
        while (font.size() % 4) font.push_back(0);
    }
    return font;
}

// the 34 byte box glyph of the tiny font
inline std::vector<unsigned char> BoxGlyph() {
    TinyFont tf = MakeTinyFont();
    return std::vector<unsigned char>(tf.bytes.begin() + (long)tf.glyf, tf.bytes.begin() + (long)(tf.glyf + 34));
}

// a composite glyph made of count references to glyph child
inline std::vector<unsigned char> CompositeOf(uint32_t child, size_t count) {
    std::vector<unsigned char> g;
    Put16(g, 0xFFFF);
    Put16(g, 0);
    Put16(g, 0);
    Put16(g, 600);
    Put16(g, 700);
    for (size_t i = 0; i < count; ++i) {
        Put16(g, i + 1 < count ? 0x0023 : 0x0003);
        Put16(g, child);
        Put16(g, 0);
        Put16(g, 0);
    }
    return g;
}

inline std::string BytesToString(const std::vector<unsigned char>& v) { return std::string(v.begin(), v.end()); }

}  // namespace test
}  // namespace imforge
