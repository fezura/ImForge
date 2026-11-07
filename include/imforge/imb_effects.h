#pragma once
// DrawList effects: gradients, shadows, glows, animated borders, particles, noise
#include "imb_anim.h"

namespace imb {

enum class BorderStyle { Solid, Rainbow, Sweep, Pulse };

struct ParticleStyle {
    int count = 40;
    float speed = 20.f;
    float size = 2.f;
    ImU32 color = 0x40FFFFFF;
    bool connect = false;
    float connect_dist = 80.f;
};

namespace detail {

inline void StrokeRect(ImDrawList* dl, const ImVec2& a, const ImVec2& b, ImU32 col, float rounding, float thickness) {
#if IMGUI_VERSION_NUM >= 19280
    dl->AddRect(a, b, col, rounding, thickness, 0);
#else
    dl->AddRect(a, b, col, rounding, 0, thickness);
#endif
}

const int kMaxBorderPoints = 192;
const float kPi = 3.14159265358979f;

inline float ColAlpha(ImU32 c) { return (float)((c >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f; }

inline ImU32 WithAlpha(ImU32 c, float mul) {
    float a = ColAlpha(c) * Clamp01(mul);
    return (c & ~IM_COL32_A_MASK) | ((ImU32)(a * 255.0f + 0.5f) << IM_COL32_A_SHIFT);
}

inline ImVec2 WhiteUv() { return ImGui::GetIO().Fonts->TexUvWhitePixel; }

// recolors vertices written since vtx0, keeping the AA fringe alpha
template <class F>
inline void Recolor(ImDrawList* dl, int vtx0, F color_at) {
    for (int i = vtx0; i < dl->VtxBuffer.Size; i++) {
        ImDrawVert& v = dl->VtxBuffer[i];
        ImU32 c = color_at(v.pos);
        ImU32 a = (v.col >> IM_COL32_A_SHIFT) & 0xFF;
        ImU32 ca = (c >> IM_COL32_A_SHIFT) & 0xFF;
        v.col = (c & ~IM_COL32_A_MASK) | (((a * ca + 127) / 255) << IM_COL32_A_SHIFT);
    }
}

// 0..1 along the perimeter, clockwise from top-left
inline float PerimeterParam(const ImVec2& p, const ImVec2& min, const ImVec2& max) {
    float w = max.x - min.x, h = max.y - min.y, per = 2.0f * (w + h);
    if (per <= 0.0f) return 0.0f;
    float dl_ = std::fabs(p.x - min.x), dr = std::fabs(p.x - max.x);
    float dt = std::fabs(p.y - min.y), db = std::fabs(p.y - max.y);
    float px = p.x < min.x ? min.x : (p.x > max.x ? max.x : p.x);
    float py = p.y < min.y ? min.y : (p.y > max.y ? max.y : p.y);
    if ((dt < db ? dt : db) <= (dl_ < dr ? dl_ : dr)) {
        if (dt <= db) return (px - min.x) / per;
        return (w + h + (max.x - px)) / per;
    }
    if (dr <= dl_) return (w + (py - min.y)) / per;
    return (2.0f * w + h + (max.y - py)) / per;
}

// PathRect outline resampled to evenly spaced points
inline int ResampleRect(ImDrawList* dl, const ImVec2& min, const ImVec2& max, float rounding, ImVec2* out,
                        int max_out) {
    int base = dl->_Path.Size;
    dl->PathRect(min, max, rounding);
    const ImVec2* src = dl->_Path.Data + base;
    int src_n = dl->_Path.Size - base;
    float per = 0.0f;
    for (int i = 0; i < src_n; i++) {
        ImVec2 a = src[i], b = src[(i + 1) % src_n];
        per += std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
    }
    int count = 0;
    if (src_n >= 3 && per > 0.0f) {
        int n = (int)(per / 6.0f);
        if (n < 16) n = 16;
        if (n > max_out) n = max_out;
        float step = per / (float)n, carry = 0.0f;
        for (int i = 0; i < src_n && count < n; i++) {
            ImVec2 a = src[i], b = src[(i + 1) % src_n];
            float seg = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
            if (seg <= 0.0f) continue;
            float d = carry;
            while (d < seg && count < n) {
                out[count++] = Lerp(a, b, d / seg);
                d += step;
            }
            carry = d - seg;
        }
    }
    dl->_Path.Size = base;
    return count;
}

inline void SoftRings(ImDrawList* dl, const ImVec2& min, const ImVec2& max, float rounding, ImU32 color,
                      float size, const ImVec2& offset, float falloff) {
    float base_a = ColAlpha(color);
    if (size <= 0.0f || base_a <= 0.0f) return;
    int n = (int)(size / 1.5f);
    if (n < 2) n = 2;
    if (n > 16) n = 16;
    float prev = 0.0f;
    for (int i = 1; i <= n; i++) {
        float e = size * (float)i / (float)n;
        float mid = 0.5f * (prev + e), th = e - prev;
        float k = std::pow(1.0f - mid / size, falloff);
        ImVec2 a(min.x + offset.x - mid, min.y + offset.y - mid);
        ImVec2 b(max.x + offset.x + mid, max.y + offset.y + mid);
        StrokeRect(dl, a, b, WithAlpha(color, k), rounding > 0.0f ? rounding + mid : 0.0f, th);
        prev = e;
    }
}

inline unsigned Hash3(unsigned seed, int x, int y) {
    unsigned h = seed * 0x9E3779B1u;
    h ^= (unsigned)x * 0x8DA6B343u;
    h ^= (unsigned)y * 0xD8163841u;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}

inline float Rand01(unsigned& st) {
    st ^= st << 13; st ^= st >> 17; st ^= st << 5;
    return (float)(st >> 8) * (1.0f / 16777216.0f);
}

inline int AcquireParticleSystem(ImGuiID id) {
    Runtime& s = State();
    int free_slot = -1, lru = 0;
    for (int i = 0; i < kMaxParticleSystems; i++) {
        if (s.psys[i].key == id) return i;
        if (s.psys[i].key == 0) { if (free_slot < 0) free_slot = i; }
        else if (s.psys[i].last_time < s.psys[lru].last_time || s.psys[lru].key == 0) lru = i;
    }
    int slot = free_slot >= 0 ? free_slot : lru;
    memset(&s.psys[slot], 0, sizeof(ParticleSystem));
    s.psys[slot].key = id;
    s.psys[slot].last_frame = -1;
    if (s.ppool.Size < (slot + 1) * kMaxParticles) s.ppool.resize((slot + 1) * kMaxParticles);
    return slot;
}

}  // namespace detail

inline void GradientRect(ImDrawList* dl, const ImVec2& min, const ImVec2& max, ImU32 c0, ImU32 c1, bool vertical,
                         float rounding = 0) {
    if (max.x <= min.x || max.y <= min.y) return;
    if (rounding <= 0.0f) {
        if (vertical) dl->AddRectFilledMultiColor(min, max, c0, c0, c1, c1);
        else dl->AddRectFilledMultiColor(min, max, c0, c1, c1, c0);
        return;
    }
    int vtx0 = dl->VtxBuffer.Size;
    dl->PathRect(min, max, rounding);
    dl->PathFillConvex(IM_COL32_WHITE);
    float a0 = vertical ? min.y : min.x, len = vertical ? max.y - min.y : max.x - min.x;
    detail::Recolor(dl, vtx0, [&](const ImVec2& p) {
        return LerpCol(c0, c1, detail::Clamp01(((vertical ? p.y : p.x) - a0) / len));
    });
}

inline void RadialGradient(ImDrawList* dl, const ImVec2& center, float radius, ImU32 inner, ImU32 outer,
                           int segments = 48) {
    if (radius <= 0.0f) return;
    if (segments < 3) segments = 3;
    if (segments > 512) segments = 512;
#if IMGUI_VERSION_NUM >= 19300
    bool aa = (dl->Flags & ImDrawFlags_AAFill) != 0;
#else
    bool aa = (dl->Flags & ImDrawListFlags_AntiAliasedFill) != 0;
#endif
    int n = segments;
    dl->PrimReserve(n * 3 + (aa ? n * 6 : 0), 1 + n + (aa ? n : 0));
    ImVec2 uv = detail::WhiteUv();
    ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
    dl->PrimWriteVtx(center, uv, inner);
    for (int i = 0; i < n; i++) {
        float a = (float)i / (float)n * 2.0f * detail::kPi;
        dl->PrimWriteVtx(ImVec2(center.x + std::cos(a) * radius, center.y + std::sin(a) * radius), uv, outer);
    }
    if (aa) {
        ImU32 trans = outer & ~IM_COL32_A_MASK;
        for (int i = 0; i < n; i++) {
            float a = (float)i / (float)n * 2.0f * detail::kPi;
            ImVec2 p(center.x + std::cos(a) * (radius + 1.0f), center.y + std::sin(a) * (radius + 1.0f));
            dl->PrimWriteVtx(p, uv, trans);
        }
    }
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        dl->PrimWriteIdx(base);
        dl->PrimWriteIdx((ImDrawIdx)(base + 1 + i));
        dl->PrimWriteIdx((ImDrawIdx)(base + 1 + j));
    }
    if (aa) {
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            dl->PrimWriteIdx((ImDrawIdx)(base + 1 + i));
            dl->PrimWriteIdx((ImDrawIdx)(base + 1 + j));
            dl->PrimWriteIdx((ImDrawIdx)(base + 1 + n + j));
            dl->PrimWriteIdx((ImDrawIdx)(base + 1 + i));
            dl->PrimWriteIdx((ImDrawIdx)(base + 1 + n + j));
            dl->PrimWriteIdx((ImDrawIdx)(base + 1 + n + i));
        }
    }
}

// rings outside the rect only, so it works before or after the item
inline void Shadow(ImDrawList* dl, const ImVec2& min, const ImVec2& max, float rounding, ImU32 color, float size,
                   const ImVec2& offset = ImVec2(0, 2)) {
    detail::SoftRings(dl, min, max, rounding, color, size, offset, 2.0f);
}

inline void Glow(ImDrawList* dl, const ImVec2& min, const ImVec2& max, float rounding, ImU32 color, float size) {
    detail::SoftRings(dl, min, max, rounding, color, size, ImVec2(0, 0), 1.5f);
    detail::StrokeRect(dl, min, max, color, rounding, 1.0f);
}

inline void AnimatedBorder(ImDrawList* dl, const ImVec2& min, const ImVec2& max, float rounding, float thickness,
                           BorderStyle style, ImU32 color, float speed = 1.f) {
    if (max.x <= min.x || max.y <= min.y) return;
    if (thickness <= 0.0f) thickness = 1.0f;
    float time = (float)ImGui::GetTime() * speed;
    switch (style) {
    case BorderStyle::Solid:
        detail::StrokeRect(dl, min, max, color, rounding, thickness);
        return;
    case BorderStyle::Pulse:
        detail::StrokeRect(dl, min, max, detail::WithAlpha(color, 0.55f + 0.45f * std::sin(time * 4.0f)), rounding,
                           thickness);
        return;
    case BorderStyle::Rainbow:
    case BorderStyle::Sweep:
        break;
    }
    ImVec2 pts[detail::kMaxBorderPoints];
    int n = detail::ResampleRect(dl, min, max, rounding, pts, detail::kMaxBorderPoints);
    if (n < 3) return;
    if (style == BorderStyle::Sweep) detail::StrokeRect(dl, min, max, detail::WithAlpha(color, 0.25f), rounding, thickness);
    int vtx0 = dl->VtxBuffer.Size;
#if IMGUI_VERSION_NUM >= 19280
    dl->AddPolyline(pts, n, IM_COL32_WHITE, thickness, ImDrawFlags_Closed);
#else
    dl->AddPolyline(pts, n, IM_COL32_WHITE, ImDrawFlags_Closed, thickness);
#endif
    if (style == BorderStyle::Rainbow) {
        detail::Recolor(dl, vtx0, [&](const ImVec2& p) {
            float hue = detail::PerimeterParam(p, min, max) + time * 0.2f;
            hue -= std::floor(hue);
            float r, g, b;
            ImGui::ColorConvertHSVtoRGB(hue, 0.85f, 1.0f, r, g, b);
            return IM_COL32((int)(r * 255.0f + 0.5f), (int)(g * 255.0f + 0.5f), (int)(b * 255.0f + 0.5f),
                            (int)((color >> IM_COL32_A_SHIFT) & 0xFF));
        });
    } else {
        float head = time * 0.35f;
        head -= std::floor(head);
        detail::Recolor(dl, vtx0, [&](const ImVec2& p) {
            float d = std::fabs(detail::PerimeterParam(p, min, max) - head);
            if (d > 0.5f) d = 1.0f - d;
            float k = 1.0f - d / 0.15f;
            if (k < 0.0f) k = 0.0f;
            return detail::WithAlpha(color, k * k);
        });
    }
}

inline void Particles(ImDrawList* dl, ImGuiID id, const ImVec2& min, const ImVec2& max, const ParticleStyle& st) {
    float w = max.x - min.x, h = max.y - min.y;
    int count = st.count < detail::kMaxParticles ? st.count : detail::kMaxParticles;
    if (w <= 0.0f || h <= 0.0f || count <= 0) return;
    if (id == 0) id = 1;
    detail::BeginFrame();
    detail::Runtime& s = detail::State();
    int slot = detail::AcquireParticleSystem(id);
    detail::ParticleSystem& ps = s.psys[slot];
    detail::Particle* p = s.ppool.Data + slot * detail::kMaxParticles;
    if (ps.count != count) {
        unsigned rng = (unsigned)id * 0x9E3779B1u + 0x7F4A7C15u;
        if (rng == 0) rng = 1;
        for (int i = 0; i < count; i++) {
            p[i].x = detail::Rand01(rng);
            p[i].y = detail::Rand01(rng);
            float ang = detail::Rand01(rng) * 2.0f * detail::kPi, spd = 0.5f + detail::Rand01(rng);
            p[i].vx = std::cos(ang) * spd;
            p[i].vy = std::sin(ang) * spd;
            p[i].scale = 0.6f + 0.8f * detail::Rand01(rng);
        }
        ps.count = count;
    }
    if (ps.last_frame != s.frame) {
        float dt = DeltaTime();
        for (int i = 0; i < count; i++) {
            p[i].x += p[i].vx * st.speed * dt / w;
            p[i].y += p[i].vy * st.speed * dt / h;
            p[i].x -= std::floor(p[i].x);
            p[i].y -= std::floor(p[i].y);
        }
        ps.last_frame = s.frame;
    }
    ps.last_time = s.now;
    dl->PushClipRect(min, max, true);
    if (st.connect && st.connect_dist > 0.0f) {
        float cd2 = st.connect_dist * st.connect_dist;
        for (int i = 0; i < count; i++) {
            ImVec2 a(min.x + p[i].x * w, min.y + p[i].y * h);
            for (int j = i + 1; j < count; j++) {
                ImVec2 b(min.x + p[j].x * w, min.y + p[j].y * h);
                float d2 = (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y);
                if (d2 >= cd2) continue;
                dl->AddLine(a, b, detail::WithAlpha(st.color, 1.0f - std::sqrt(d2) / st.connect_dist), 1.0f);
            }
        }
    }
    for (int i = 0; i < count; i++)
        dl->AddCircleFilled(ImVec2(min.x + p[i].x * w, min.y + p[i].y * h), st.size * p[i].scale, st.color, 8);
    dl->PopClipRect();
}

// density = fraction of 6px cells that get a dot, same seed gives the same pattern
inline void NoiseBackground(ImDrawList* dl, const ImVec2& min, const ImVec2& max, ImU32 color, float density,
                            ImGuiID seed) {
    float w = max.x - min.x, h = max.y - min.y;
    if (w <= 0.0f || h <= 0.0f || density <= 0.0f) return;
    if (density > 1.0f) density = 1.0f;
    float cell = 6.0f;
    if (w * h / (cell * cell) > 20000.0f) cell = std::sqrt(w * h / 20000.0f);
    int nx = (int)std::ceil(w / cell), ny = (int)std::ceil(h / cell);
    dl->PushClipRect(min, max, true);
    for (int cy = 0; cy < ny; cy++) {
        for (int cx = 0; cx < nx; cx++) {
            unsigned hs = detail::Hash3((unsigned)seed, cx, cy);
            if ((float)(hs & 0xFFFF) * (1.0f / 65535.0f) >= density) continue;
            float jx = (float)((hs >> 16) & 0xFF) * (1.0f / 255.0f), jy = (float)((hs >> 24) & 0xFF) * (1.0f / 255.0f);
            float a = 0.4f + 0.6f * (float)((hs >> 9) & 0x7F) * (1.0f / 127.0f);
            ImVec2 p(min.x + ((float)cx + jx) * cell, min.y + ((float)cy + jy) * cell);
            dl->AddRectFilled(p, ImVec2(p.x + 1.5f, p.y + 1.5f), detail::WithAlpha(color, a));
        }
    }
    dl->PopClipRect();
}

// clip rects are axis-aligned, so `rounding` is ignored for now
inline void CursorHighlight(ImDrawList* dl, const ImVec2& min, const ImVec2& max, float rounding, ImU32 color,
                            float radius) {
    (void)rounding;
    if (radius <= 0.0f || max.x <= min.x || max.y <= min.y) return;
    ImVec2 m = ImGui::GetMousePos();
    if (m.x < min.x - radius || m.x > max.x + radius || m.y < min.y - radius || m.y > max.y + radius) return;
    dl->PushClipRect(min, max, true);
    RadialGradient(dl, m, radius, color, color & ~IM_COL32_A_MASK, 48);
    dl->PopClipRect();
}

}  // namespace imb
