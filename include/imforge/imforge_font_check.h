#pragma once

// Structural check for TrueType bytes that came over the live link, run before Dear ImGui hands them
// to stb_truetype. stb_truetype trusts the file (no bounds checks on cmap, loca, glyf or hmtx, unbounded
// composite recursion, asserts on formats it doesn't know), so this walks every structure it will read
// and rejects anything that would make it step outside the buffer. CFF (OTTO) fonts are refused because
// stb parses CFF against a fixed 512 MB window instead of the table size.

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace imforge {
namespace fontcheck {

constexpr size_t kMaxTables = 128;
constexpr int kMaxCompositeDepth = 8;
constexpr size_t kMaxComponents = 256;
// stb expands composites into one vertex array per glyph, and every glyph index can point at the same data
constexpr uint64_t kMaxGlyphVertices = uint64_t(1) << 15;
constexpr uint64_t kMaxFontVertices = uint64_t(1) << 25;

struct Reader {
    const unsigned char* d = nullptr;
    size_t n = 0;
    bool U16(size_t pos, uint32_t& v) const {
        if (pos > n || n - pos < 2) return false;
        v = (uint32_t(d[pos]) << 8) | d[pos + 1];
        return true;
    }
    bool U32(size_t pos, uint32_t& v) const {
        if (pos > n || n - pos < 4) return false;
        v = (uint32_t(d[pos]) << 24) | (uint32_t(d[pos + 1]) << 16) | (uint32_t(d[pos + 2]) << 8) | d[pos + 3];
        return true;
    }
};

struct Table {
    size_t off = 0, len = 0;
    bool found = false;
};

inline bool Fail(std::string* why, const char* text) {
    if (why) *why = text;
    return false;
}

// the subtable stb picks: the last Unicode one
struct CmapInfo {
    size_t index_map = 0, cmap_end = 0;
    uint32_t format = 0;
    // filled by ValidateFontData, for AtlasPixels
    size_t loca = 0, glyf = 0;
    uint32_t loc_format = 0, num_glyphs = 0;
    int line_height = 0;
};

// stbtt_FindGlyphIndex with every read bounded by the cmap table. false = stb would read outside it
inline bool CmapLookup(const Reader& r, const CmapInfo& ci, uint32_t cp, uint32_t& glyph) {
    Reader c = r;
    c.n = ci.cmap_end;
    const size_t im = ci.index_map;
    glyph = 0;
    if (ci.format == 0) {
        uint32_t bytes = 0;
        if (!c.U16(im + 2, bytes)) return false;
        if (cp + 6 >= bytes) return true;
        if (im + 6 + cp >= c.n) return false;
        glyph = r.d[im + 6 + cp];
        return true;
    }
    if (ci.format == 6) {
        uint32_t first = 0, count = 0;
        if (!c.U16(im + 6, first) || !c.U16(im + 8, count)) return false;
        if (cp < first || cp >= first + count) return true;
        return c.U16(im + 10 + size_t(cp - first) * 2, glyph);
    }
    if (ci.format == 4) {
        if (cp > 0xFFFF) return true;
        uint32_t segx2 = 0, sr = 0, es = 0, rs = 0;
        if (!c.U16(im + 6, segx2) || !c.U16(im + 8, sr) || !c.U16(im + 10, es) || !c.U16(im + 12, rs)) return false;
        size_t segcount = segx2 >> 1;
        if (segcount == 0 || es > 16) return false;
        sr >>= 1;
        rs >>= 1;
        size_t end_count = im + 14, search = end_count;
        uint32_t v = 0;
        if (!c.U16(search + rs * 2, v)) return false;
        if (cp >= v) search += rs * 2;
        search -= 2;
        while (es) {
            sr >>= 1;
            if (!c.U16(search + sr * 2, v)) return false;
            if (cp > v) search += sr * 2;
            --es;
        }
        search += 2;
        size_t item = (uint16_t)((search - end_count) >> 1);
        uint32_t start = 0, last = 0, offset = 0;
        if (!c.U16(im + 14 + segcount * 2 + 2 + 2 * item, start) || !c.U16(end_count + 2 * item, last)) return false;
        if (cp < start || cp > last) return true;
        if (!c.U16(im + 14 + segcount * 6 + 2 + 2 * item, offset)) return false;
        if (offset == 0) {
            uint32_t delta = 0;
            if (!c.U16(im + 14 + segcount * 4 + 2 + 2 * item, delta)) return false;
            glyph = (cp + delta) & 0xFFFF;
            return true;
        }
        return c.U16(offset + (cp - start) * 2 + im + 14 + segcount * 6 + 2 + 2 * item, glyph);
    }
    if (ci.format == 12 || ci.format == 13) {
        uint32_t groups = 0;
        if (!c.U32(im + 12, groups)) return false;
        size_t low = 0, high = groups;
        while (low < high) {
            size_t mid = low + ((high - low) >> 1);
            uint32_t first = 0, last = 0, start_glyph = 0;
            if (!c.U32(im + 16 + mid * 12, first) || !c.U32(im + 16 + mid * 12 + 4, last)) return false;
            if (cp < first) {
                high = mid;
            } else if (cp > last) {
                low = mid + 1;
            } else {
                if (!c.U32(im + 16 + mid * 12 + 8, start_glyph)) return false;
                glyph = ci.format == 12 ? start_glyph + cp - first : start_glyph;
                return true;
            }
        }
        return true;
    }
    return false;
}

inline bool CheckCmap(const Reader& r, const Table& cmap, uint32_t num_glyphs, CmapInfo& ci, std::string* why) {
    uint32_t count = 0;
    if (cmap.len < 4 || !r.U16(cmap.off + 2, count) || 4 + size_t(count) * 8 > cmap.len) return Fail(why, "cmap table is truncated");
    for (uint32_t i = 0; i < count; ++i) {
        size_t rec = cmap.off + 4 + size_t(i) * 8;
        uint32_t platform = 0, encoding = 0, off = 0;
        r.U16(rec, platform);
        r.U16(rec + 2, encoding);
        r.U32(rec + 4, off);
        if (platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10))) {
            if (off >= cmap.len) return Fail(why, "cmap subtable outside the cmap table");
            ci.index_map = cmap.off + off;
        }
    }
    if (ci.index_map == 0) return Fail(why, "no Unicode cmap subtable");
    ci.cmap_end = cmap.off + cmap.len;
    Reader c = r;
    c.n = ci.cmap_end;
    if (!c.U16(ci.index_map, ci.format)) return Fail(why, "cmap subtable is truncated");
    if (ci.format != 0 && ci.format != 4 && ci.format != 6 && ci.format != 12 && ci.format != 13)
        return Fail(why, "unsupported cmap format");
    if (ci.format == 6) {
        // stb reads first + count entries for any code point, the loop below only visits the BMP
        uint32_t first6 = 0, entries6 = 0;
        if (!c.U16(ci.index_map + 6, first6) || !c.U16(ci.index_map + 8, entries6) || first6 + entries6 > 0x10000)
            return Fail(why, "cmap subtable is malformed");
    }
    if (ci.format == 12 || ci.format == 13) {
        uint32_t groups = 0;
        if (!c.U32(ci.index_map + 12, groups) || groups > (ci.cmap_end - ci.index_map - 16) / 12)
            return Fail(why, "cmap format 12 is truncated");
        for (uint32_t i = 0; i < groups; ++i) {
            size_t g = ci.index_map + 16 + size_t(i) * 12;
            uint32_t first = 0, last = 0, glyph = 0;
            c.U32(g, first);
            c.U32(g + 4, last);
            c.U32(g + 8, glyph);
            uint64_t top = ci.format == 12 && last >= first ? uint64_t(glyph) + (last - first) : glyph;
            if (top >= num_glyphs) return Fail(why, "cmap maps to a glyph that doesn't exist");
        }
        return true;
    }
    for (uint32_t cp = 0; cp <= 0xFFFF; ++cp) {
        uint32_t glyph = 0;
        if (!CmapLookup(r, ci, cp, glyph)) return Fail(why, "cmap subtable is malformed");
        if (glyph >= num_glyphs) return Fail(why, "cmap maps to a glyph that doesn't exist");
    }
    return true;
}

// lone_flag: when the last contour is a single off-curve point stb reads one vertex past its buffer.
// Consolas and Calibri have such glyphs, so instead of refusing them we report the flag byte and
// the caller marks the point on-curve (a one point contour has no area either way). 0 = none
inline bool CheckSimpleGlyph(const Reader& g, int contours, size_t len, size_t& lone_flag, uint64_t& weight) {
    size_t p = 10;
    uint32_t prev = 0, end_pt = 0, ins = 0;
    for (int i = 0; i < contours; ++i) {
        if (!g.U16(p + size_t(i) * 2, end_pt)) return false;
        if (i > 0 && end_pt <= prev) return false;
        prev = end_pt;
    }
    size_t points = size_t(end_pt) + 1;
    weight = points + size_t(contours) * 2;
    p += size_t(contours) * 2;
    if (!g.U16(p, ins)) return false;
    p += 2 + ins;
    size_t xy = 0, repeat = 0, flag_at = 0;
    bool shared = false;
    unsigned char flags = 0, last_flags = 0;
    for (size_t i = 0; i < points; ++i) {
        if (repeat == 0) {
            if (p >= len) return false;
            flag_at = p;
            flags = g.d[p++];
            shared = false;
            if (flags & 8) {
                if (p >= len) return false;
                repeat = g.d[p++];
                shared = repeat > 0;
            }
        } else {
            --repeat;
        }
        xy += (flags & 2) ? 1 : (flags & 16) ? 0 : 2;
        xy += (flags & 4) ? 1 : (flags & 32) ? 0 : 2;
        last_flags = flags;
    }
    if (p > len || len - p < xy) return false;
    // stb peeks one point past a contour that starts off-curve, past the end for a 1-point last contour
    size_t last_start = 0;
    if (contours > 1) {
        uint32_t before = 0;
        g.U16(10 + size_t(contours - 2) * 2, before);
        last_start = size_t(before) + 1;
    }
    lone_flag = 0;
    if (last_start == points - 1 && !(last_flags & 1)) {
        if (shared) return false;
        lone_flag = flag_at;
    }
    return true;
}

inline bool CheckCompositeGlyph(const Reader& g, size_t len, uint32_t num_glyphs, std::vector<uint32_t>& children) {
    size_t p = 10;
    uint32_t flags = 0, child = 0;
    do {
        if (children.size() >= kMaxComponents || !g.U16(p, flags) || !g.U16(p + 2, child)) return false;
        if (!(flags & 2) || child >= num_glyphs) return false;
        p += 4 + ((flags & 1) ? 4 : 2);
        if (flags & 8) p += 2;
        else if (flags & 0x40) p += 4;
        else if (flags & 0x80) p += 8;
        if (p > len) return false;
        children.push_back(child);
    } while (flags & 0x20);
    return true;
}

inline int Signed16(uint32_t v) { return int(int16_t(uint16_t(v))); }

// line_height = hhea ascender - descender, what stb scales by. Boxes that are inverted or absurdly
// large turn into negative or huge rectangles in the atlas packer
inline bool CheckGlyphs(const Reader& r, const Table& loca, const Table& glyf, uint32_t num_glyphs, uint32_t loc_format,
                        int line_height, std::vector<size_t>* fixes, std::string* why) {
    size_t entry = loc_format ? 4 : 2;
    if (loca.len / entry < size_t(num_glyphs) + 1) return Fail(why, "loca table is too short");
    std::vector<std::vector<uint32_t>> kids(num_glyphs);
    std::vector<uint64_t> weight(num_glyphs, 0);
    for (uint32_t i = 0; i < num_glyphs; ++i) {
        uint32_t a = 0, b = 0;
        if (loc_format) {
            r.U32(loca.off + size_t(i) * 4, a);
            r.U32(loca.off + size_t(i) * 4 + 4, b);
        } else {
            r.U16(loca.off + size_t(i) * 2, a);
            r.U16(loca.off + size_t(i) * 2 + 2, b);
            a *= 2;
            b *= 2;
        }
        if (a > b || b > glyf.len) return Fail(why, "loca points outside the glyf table");
        if (a == b) continue;
        size_t len = b - a;
        if (len < 10) return Fail(why, "glyph header is truncated");
        Reader g;
        g.d = r.d + glyf.off + a;
        g.n = len;
        uint32_t raw = 0, x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        g.U16(0, raw);
        g.U16(2, x0);
        g.U16(4, y0);
        g.U16(6, x1);
        g.U16(8, y1);
        int w = Signed16(x1) - Signed16(x0), h = Signed16(y1) - Signed16(y0);
        if (w < 0 || h < 0 || w > 16 * line_height || h > 16 * line_height) return Fail(why, "glyph bounding box is invalid");
        int contours = Signed16(raw);
        size_t lone = 0;
        if (contours > 0 && !CheckSimpleGlyph(g, contours, len, lone, weight[i])) return Fail(why, "glyph outline is malformed");
        if (lone && fixes) fixes->push_back(glyf.off + a + lone);
        if (contours < 0 && !CheckCompositeGlyph(g, len, num_glyphs, kids[i])) return Fail(why, "composite glyph is malformed");
    }
    // stb recurses into composite parts without a limit, so no cycles and no deep nesting.
    // height: levels below and including a glyph, 0 = not done yet
    std::vector<unsigned char> height(num_glyphs, 0), on_stack(num_glyphs, 0);
    for (uint32_t i = 0; i < num_glyphs; ++i) {
        if (kids[i].empty() || height[i]) continue;
        std::vector<std::pair<uint32_t, size_t>> stack(1, std::make_pair(i, size_t(0)));
        on_stack[i] = 1;
        while (!stack.empty()) {
            uint32_t cur = stack.back().first;
            size_t next = stack.back().second;
            if (next < kids[cur].size()) {
                ++stack.back().second;
                uint32_t ch = kids[cur][next];
                if (on_stack[ch]) return Fail(why, "composite glyph refers to itself");
                if (!kids[ch].empty() && !height[ch]) {
                    on_stack[ch] = 1;
                    stack.push_back(std::make_pair(ch, size_t(0)));
                }
                continue;
            }
            int h = 1;
            for (uint32_t ch : kids[cur]) {
                int below = kids[ch].empty() ? 1 : height[ch];
                if (below + 1 > h) h = below + 1;
            }
            if (h > kMaxCompositeDepth) return Fail(why, "composite glyphs nest too deep");
            uint64_t sum = 0;
            for (uint32_t ch : kids[cur]) sum += weight[ch];
            if (sum > kMaxGlyphVertices) return Fail(why, "composite glyph is too complex");
            weight[cur] = sum;
            height[cur] = (unsigned char)h;
            on_stack[cur] = 0;
            stack.pop_back();
        }
    }
    uint64_t total = 0;
    for (uint64_t w : weight) total += w;
    if (total > kMaxFontVertices) return Fail(why, "glyph outlines are too complex");
    return true;
}

// fixes: offsets of flag bytes to OR with 1 before the font is used (see CheckSimpleGlyph)
inline bool ValidateFontData(const void* data, size_t size, std::string* why = nullptr, CmapInfo* cmap_out = nullptr,
                             std::vector<size_t>* fixes = nullptr) {
    Reader r;
    r.d = static_cast<const unsigned char*>(data);
    r.n = size;
    uint32_t tag = 0, count = 0;
    if (!r.d || !r.U32(0, tag) || !r.U16(4, count)) return Fail(why, "too small to be a font");
    if (tag == 0x74746366u) return Fail(why, "font collections (.ttc) are not supported");
    if (tag == 0x4F54544Fu) return Fail(why, "CFF/OpenType outlines are not supported over the live link, use a .ttf");
    if (tag != 0x00010000u && tag != 0x74727565u) return Fail(why, "not a TrueType font");
    if (count == 0 || count > kMaxTables || 12 + size_t(count) * 16 > size) return Fail(why, "bad table directory");
    Table cmap, head, hhea, hmtx, maxp, loca, glyf;
    struct Want {
        uint32_t tag;
        Table* t;
    } want[] = {{0x636D6170u, &cmap}, {0x68656164u, &head}, {0x68686561u, &hhea}, {0x686D7478u, &hmtx},
                {0x6D617870u, &maxp}, {0x6C6F6361u, &loca}, {0x676C7966u, &glyf}};
    for (uint32_t i = 0; i < count; ++i) {
        size_t rec = 12 + size_t(i) * 16;
        uint32_t t = 0, off = 0, len = 0;
        r.U32(rec, t);
        r.U32(rec + 8, off);
        r.U32(rec + 12, len);
        if (off > size || len > size - off) return Fail(why, "table outside the file");
        // stb takes the first match
        for (Want& w : want) {
            if (w.tag != t || w.t->found) continue;
            w.t->off = off;
            w.t->len = len;
            w.t->found = true;
        }
    }
    for (const Want& w : want)
        if (!w.t->found) return Fail(why, "a required table is missing (cmap, head, hhea, hmtx, maxp, loca, glyf)");
    uint32_t magic = 0, loc_format = 0, glyphs = 0, long_metrics = 0, ascent = 0, descent = 0;
    if (head.len < 54 || !r.U32(head.off + 12, magic) || magic != 0x5F0F3CF5u) return Fail(why, "bad head table");
    r.U16(head.off + 50, loc_format);
    if (loc_format > 1) return Fail(why, "bad head table");
    if (maxp.len < 6 || !r.U16(maxp.off + 4, glyphs) || glyphs == 0) return Fail(why, "bad maxp table");
    if (hhea.len < 36 || !r.U16(hhea.off + 34, long_metrics) || long_metrics == 0 || long_metrics > glyphs)
        return Fail(why, "bad hhea table");
    r.U16(hhea.off + 4, ascent);
    r.U16(hhea.off + 6, descent);
    int line_height = Signed16(ascent) - Signed16(descent);
    if (line_height <= 0) return Fail(why, "bad hhea table (ascender below descender)");
    if (hmtx.len < size_t(long_metrics) * 4 + size_t(glyphs - long_metrics) * 2) return Fail(why, "hmtx table is too short");
    CmapInfo ci;
    if (!CheckCmap(r, cmap, glyphs, ci, why)) return false;
    if (fixes) fixes->clear();
    if (!CheckGlyphs(r, loca, glyf, glyphs, loc_format, line_height, fixes, why)) return false;
    if (cmap_out) {
        ci.loca = loca.off;
        ci.glyf = glyf.off;
        ci.loc_format = loc_format;
        ci.num_glyphs = glyphs;
        ci.line_height = line_height;
        *cmap_out = ci;
    }
    return true;
}

// checks the bytes and applies the fixes, what the app runs on every font file it gets
inline bool PrepareFontData(std::vector<unsigned char>& bytes, std::string* why = nullptr) {
    std::vector<size_t> fixes;
    if (!ValidateFontData(bytes.data(), bytes.size(), why, nullptr, &fixes)) return false;
    for (size_t at : fixes) bytes[at] |= 1;
    return true;
}

// Dear ImGui < 1.92 asserts (and reads an empty vector in release) when a font has no glyph in its ranges.
// ranges: zero terminated {lo, hi} pairs, like ImWchar ranges
template <class W> inline bool MapsAnyGlyph(const void* data, size_t size, const W* ranges) {
    CmapInfo ci;
    if (!ranges || !ValidateFontData(data, size, nullptr, &ci)) return false;
    Reader r;
    r.d = static_cast<const unsigned char*>(data);
    r.n = size;
    for (; ranges[0] && ranges[1]; ranges += 2) {
        for (uint32_t cp = uint32_t(ranges[0]); cp <= uint32_t(ranges[1]) && cp <= 0x10FFFF; ++cp) {
            uint32_t glyph = 0;
            if (CmapLookup(r, ci, cp, glyph) && glyph != 0) return true;
        }
    }
    return false;
}

// Atlas pixels the legacy builder needs for the glyphs in ranges (same format as MapsAnyGlyph): one rect per
// mapped glyph, sized from the glyph box, two samples wide. Stops counting past limit and returns limit + 1.
template <class W> inline uint64_t AtlasPixels(const void* data, size_t size, const W* ranges, float px, uint64_t limit) {
    CmapInfo ci;
    if (!ranges || !(px > 0.0f) || !ValidateFontData(data, size, nullptr, &ci)) return limit + 1;
    Reader r;
    r.d = static_cast<const unsigned char*>(data);
    r.n = size;
    const double scale = double(px) / double(ci.line_height);
    uint64_t total = 0;
    for (; ranges[0] && ranges[1]; ranges += 2) {
        for (uint32_t cp = uint32_t(ranges[0]); cp <= uint32_t(ranges[1]) && cp <= 0x10FFFF; ++cp) {
            uint32_t glyph = 0;
            if (!CmapLookup(r, ci, cp, glyph) || glyph == 0 || glyph >= ci.num_glyphs) continue;
            uint32_t a = 0, b = 0;
            size_t at = ci.loca + size_t(glyph) * (ci.loc_format ? 4 : 2);
            if (ci.loc_format) {
                r.U32(at, a);
                r.U32(at + 4, b);
            } else {
                r.U16(at, a);
                r.U16(at + 2, b);
                a *= 2;
            }
            uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            r.U16(ci.glyf + a + 2, x0);
            r.U16(ci.glyf + a + 4, y0);
            r.U16(ci.glyf + a + 6, x1);
            r.U16(ci.glyf + a + 8, y1);
            double w = double(Signed16(x1) - Signed16(x0)) * scale, h = double(Signed16(y1) - Signed16(y0)) * scale;
            if (a == (ci.loc_format ? b : b * 2)) w = h = 0;
            total += uint64_t(w * 2.0 + 3.0) * uint64_t(h + 3.0);
            if (total > limit) return limit + 1;
        }
    }
    return total;
}

}  // namespace fontcheck
}  // namespace imforge
