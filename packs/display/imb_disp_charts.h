#pragma once
// Sparkline, MiniBars, FrameGraph
#include "imb_disp_core.h"

namespace imb {
namespace disp {

struct SparklineStyle {
    ImVec2 size = ImVec2(0, 0);  // 0 width fills, 0 height = 2 x font size
    float v_min = FLT_MAX;       // FLT_MAX = auto
    float v_max = FLT_MAX;
    float thickness = 0.0f;
    ImU32 color = 0;
    float fill_alpha = 0.32f;    // 0 turns the fill off
    bool show_last = true;
    bool hover = true;           // guide line + tooltip under the mouse
    const char* format = "%.2f";
    int offset = 0;              // ring buffer start, like ImGui::PlotLines
};

namespace detail {

const int kMaxSparkPoints = 512;

inline float SampleAt(const float* v, int count, int offset, int i) { return v[(offset + i) % count]; }

inline void Range(const float* v, int count, int offset, float& lo, float& hi) {
    lo = FLT_MAX;
    hi = -FLT_MAX;
    for (int i = 0; i < count; ++i) {
        float x = SampleAt(v, count, offset, i);
        if (x != x || x - x != 0.0f) continue;
        if (x < lo) lo = x;
        if (x > hi) hi = x;
    }
    if (lo > hi) lo = hi = 0.0f;
}

// draws a sparkline into [mn, mx], returns the hovered sample index or -1
inline int DrawSpark(ImDrawList* dl, ImGuiID id, const ImVec2& mn, const ImVec2& mx, const float* values,
                     int count, const SparklineStyle& ss, bool hovered) {
    if (!values || count <= 0) {
        dl->AddLine(ImVec2(mn.x, mx.y - 1.0f), ImVec2(mx.x, mx.y - 1.0f), TextDim(0.4f), 1.0f);
        return -1;
    }
    float font = ImGui::GetFontSize();
    float th = ss.thickness > 0.0f ? ss.thickness : Max(1.5f, font * 0.11f);
    int offset = ss.offset > 0 ? ss.offset % count : 0;
    float lo, hi;
    Range(values, count, offset, lo, hi);
    if (ss.v_min != FLT_MAX) lo = ss.v_min;
    if (ss.v_max != FLT_MAX) hi = ss.v_max;
    lo = Fin(lo);
    hi = Fin(hi);
    if (ss.v_min == FLT_MAX && ss.v_max == FLT_MAX && hi > lo) {
        float pad = (hi - lo) * 0.08f;
        lo -= pad;
        hi += pad;
    }
    // auto ranges glide instead of jumping
    if (ss.v_min == FLT_MAX) lo = imb::AnimateTo(id ^ 0x51ed27u, lo, 0.25f);
    if (ss.v_max == FLT_MAX) hi = imb::AnimateTo(id ^ 0x3c6ef3u, hi, 0.25f);
    if (hi - lo < 1e-6f) {
        hi += 0.5f;
        lo -= 0.5f;
    }
    float inset = th * 2.0f;
    float x0 = mn.x + inset, x1 = mx.x - inset, y0 = mn.y + inset, y1 = mx.y - th;
    int n = MinI(count, kMaxSparkPoints);
    ImU32 col = ss.color ? ImGui::GetColorU32(ss.color) : Accent();
    auto px = [&](int k) {
        int i = n > 1 ? (int)((long long)k * (count - 1) / (n - 1)) : 0;
        float v = Fin(SampleAt(values, count, offset, i));
        float t = n > 1 ? (float)k / (float)(n - 1) : 0.5f;
        return ImVec2(Lerp(x0, x1, t), Lerp(y1, y0, Clamp01((v - lo) / (hi - lo))));
    };
    if (ss.fill_alpha > 0.0f && n > 1) {
        ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
        ImU32 top = WithAlpha(col, ss.fill_alpha), bottom = WithAlpha(col, 0.0f);
        dl->PrimReserve((n - 1) * 6, n * 2);
        unsigned base = dl->_VtxCurrentIdx;
        for (int k = 0; k < n; ++k) {
            ImVec2 p = px(k);
            float t = Clamp01((mx.y - p.y) / Max(mx.y - mn.y, 1.0f));
            dl->PrimWriteVtx(p, uv, Mix(bottom, top, 0.35f + 0.65f * t));
            dl->PrimWriteVtx(ImVec2(p.x, mx.y), uv, bottom);
        }
        for (int k = 0; k < n - 1; ++k) {
            ImDrawIdx a = (ImDrawIdx)(base + (unsigned)k * 2);
            dl->PrimWriteIdx(a);
            dl->PrimWriteIdx((ImDrawIdx)(a + 2));
            dl->PrimWriteIdx((ImDrawIdx)(a + 3));
            dl->PrimWriteIdx(a);
            dl->PrimWriteIdx((ImDrawIdx)(a + 3));
            dl->PrimWriteIdx((ImDrawIdx)(a + 1));
        }
    }
    if (n == 1) {
        ImVec2 p = px(0);
        dl->AddLine(ImVec2(x0, p.y), ImVec2(x1, p.y), col, th);
    } else {
        for (int k = 0; k < n; ++k) dl->PathLineTo(px(k));
        Stroke(dl, col, th);
    }
    if (ss.show_last) {
        ImVec2 p = px(n - 1);
        dl->AddCircleFilled(p, th * 3.2f, WithAlpha(col, 0.22f));
        dl->AddCircleFilled(p, th * 1.7f, col);
    }
    int hover_idx = -1;
    if (hovered && ss.hover) {
        float mxp = ImGui::GetMousePos().x;
        float t = Clamp01((mxp - x0) / Max(x1 - x0, 1.0f));
        int k = (int)(t * (float)(n - 1) + 0.5f);
        hover_idx = n > 1 ? (int)((long long)k * (count - 1) / (n - 1)) : 0;
        ImVec2 p = px(k);
        DashedLine(dl, ImVec2(p.x, mn.y), ImVec2(p.x, mx.y), TextDim(0.6f), 1.0f, 3.0f, 3.0f);
        dl->AddCircleFilled(p, th * 2.2f, TextCol());
        dl->AddCircleFilled(p, th * 1.3f, col);
    }
    return hover_idx;
}

}  // namespace detail

inline void Sparkline(const char* id_label, const float* values, int count,
                      const SparklineStyle& ss = SparklineStyle()) {
    using namespace detail;
    float font = ImGui::GetFontSize();
    ImVec2 sz(ResolveW(ss.size.x, font * 2.0f), ss.size.y > 0.0f ? ss.size.y : font * 2.0f);
    ImGuiID id = ImGui::GetID(id_label);
    ImVec2 mn = ImGui::GetCursorScreenPos();
    ImGui::PushID(id_label);
    ImGui::InvisibleButton("##spark", sz);
    ImGui::PopID();
    if (!ImGui::IsItemVisible()) return;
    bool hovered = ImGui::IsItemHovered();
    int idx = DrawSpark(ImGui::GetWindowDrawList(), id, mn, Add(mn, sz), values, count, ss, hovered);
    if (idx >= 0 && values && count > 0) {
        char buf[48];
        Format(buf, sizeof(buf), ss.format, SampleAt(values, count, ss.offset > 0 ? ss.offset % count : 0, idx));
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(buf);
        ImGui::EndTooltip();
    }
}

inline void Sparkline(const char* id_label, const float* values, int count, const ImVec2& size, ImU32 color = 0,
                      bool fill = true) {
    SparklineStyle ss;
    ss.size = size;
    ss.color = color;
    if (!fill) ss.fill_alpha = 0.0f;
    Sparkline(id_label, values, count, ss);
}

// unbound arrays in generated code come through as nullptr, nullptr
inline void Sparkline(const char* id_label, std::nullptr_t, std::nullptr_t, const ImVec2& size = ImVec2(0, 0),
                      ImU32 color = 0, bool fill = true) {
    Sparkline(id_label, (const float*)nullptr, 0, size, color, fill);
}

// std::vector<float>, ImVector<float> or anything else with data() and size()
template <class C>
inline auto Sparkline(const char* id_label, const C& values, const SparklineStyle& ss = SparklineStyle())
    -> decltype((void)values.data(), (void)values.size()) {
    Sparkline(id_label, values.data(), (int)values.size(), ss);
}

struct MiniBarsStyle {
    ImVec2 size = ImVec2(0, 0);  // 0 width fills, 0 height = 3 x font size
    float v_min = 0.0f;
    float v_max = FLT_MAX;       // FLT_MAX = auto
    float gap = 0.0f;            // 0 = a fifth of the bar width
    ImU32 color = 0;
    ImU32 highlight = 0;
    const char* format = "%.2f";
    bool tooltip = true;
    bool highlight_max = true;   // the tallest bar is lit while nothing is hovered
    int highlight_index = -1;    // keeps one bar lit, e.g. "today"
};

// returns the hovered bar or -1
inline int MiniBars(const char* id_label, const float* values, int count, const char* const* labels = nullptr,
                    int label_count = 0, const MiniBarsStyle& ms = MiniBarsStyle()) {
    using namespace detail;
    float font = ImGui::GetFontSize();
    ImVec2 sz(ResolveW(ms.size.x, font * 2.0f), ms.size.y > 0.0f ? ms.size.y : font * 3.0f);
    ImGuiID id = ImGui::GetID(id_label);
    ImVec2 mn = ImGui::GetCursorScreenPos();
    ImGui::PushID(id_label);
    ImGui::InvisibleButton("##bars", sz);
    ImGui::PopID();
    bool hovered = ImGui::IsItemHovered();
    if (!values) count = 0;
    if (count > 1024) count = 1024;
    if (!ImGui::IsItemVisible() || count <= 0) {
        if (ImGui::IsItemVisible())
            ImGui::GetWindowDrawList()->AddLine(ImVec2(mn.x, mn.y + sz.y - 1.0f), ImVec2(mn.x + sz.x, mn.y + sz.y - 1.0f),
                                                TextDim(0.4f), 1.0f);
        return -1;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    int max_idx = 0;
    for (int i = 1; i < count; ++i)
        if (Fin(values[i]) > Fin(values[max_idx])) max_idx = i;
    float hi = ms.v_max;
    if (hi == FLT_MAX) hi = imb::AnimateTo(id ^ 0x7f4a7c15u, Fin(values[max_idx]), 0.3f);
    float lo = Fin(ms.v_min);
    hi = Fin(hi);
    if (hi - lo < 1e-6f) hi = lo + 1.0f;
    float slot = sz.x / (float)count;
    float g = ms.gap > 0.0f ? ms.gap : Max(1.0f, slot * 0.2f);
    float bw = Max(1.0f, slot - g);
    int hover_idx = -1;
    if (hovered) {
        int i = (int)((ImGui::GetMousePos().x - mn.x) / slot);
        if (i >= 0 && i < count) hover_idx = i;
    }
    ImU32 col = ms.color ? ImGui::GetColorU32(ms.color) : Accent();
    ImU32 hl = ms.highlight ? ImGui::GetColorU32(ms.highlight) : Lighten(col, 0.3f);
    float base_y = mn.y + sz.y;
    dl->AddLine(ImVec2(mn.x, base_y - 0.5f), ImVec2(mn.x + sz.x, base_y - 0.5f), TextDim(0.25f), 1.0f);
    float r = Min(Rounding(), bw * 0.5f);
    for (int i = 0; i < count; ++i) {
        float f = Clamp01((Fin(values[i]) - lo) / (hi - lo));
        if (i < 256) f = Tween(id + 1u + (ImGuiID)i, f);
        float x = mn.x + (float)i * slot + g * 0.5f;
        float bh = Max(1.0f, f * (sz.y - 1.0f));
        bool lit = i == hover_idx || i == ms.highlight_index || (ms.highlight_max && hover_idx < 0 && i == max_idx);
        ImU32 c = lit ? hl : (hover_idx >= 0 ? WithAlpha(col, 0.55f) : col);
        ImVec2 a(x, base_y - bh), b(x + bw, base_y);
        if (r > 0.5f && bh > r * 2.0f) {
            // rounded cap on top, square foot on the baseline
            imb::GradientRect(dl, ImVec2(a.x, a.y + r), b, c, WithAlpha(c, 0.55f), true, 0.0f);
            dl->AddRectFilled(a, ImVec2(b.x, a.y + r + 1.0f), c, r, kRoundTop);
        } else {
            imb::GradientRect(dl, a, b, c, WithAlpha(c, 0.55f), true, 0.0f);
        }
    }
    if (hover_idx >= 0 && ms.tooltip) {
        char buf[48];
        Format(buf, sizeof(buf), ms.format, Fin(values[hover_idx]));
        ImGui::BeginTooltip();
        if (labels && hover_idx < label_count && labels[hover_idx]) {
            ImGui::TextUnformatted(labels[hover_idx]);
            ImGui::SameLine();
        }
        ImGui::TextDisabled("%s", buf);
        ImGui::EndTooltip();
    }
    return hover_idx;
}

inline int MiniBars(const char* id_label, const float* values, int count, const char* const* labels, int label_count,
                    const ImVec2& size, ImU32 color = 0) {
    MiniBarsStyle ms;
    ms.size = size;
    ms.color = color;
    return MiniBars(id_label, values, count, labels, label_count, ms);
}

inline int MiniBars(const char* id_label, std::nullptr_t, std::nullptr_t, const char* const* labels, int label_count,
                    const ImVec2& size, ImU32 color = 0) {
    return MiniBars(id_label, (const float*)nullptr, 0, labels, label_count, size, color);
}

// ring buffer of frame times in milliseconds
struct FrameSamples {
    enum { kCapacity = 240 };
    float ms[kCapacity];
    int head = 0;
    int count = 0;
    void Push(float v) {
        ms[head] = v;
        head = (head + 1) % kCapacity;
        if (count < kCapacity) ++count;
    }
    float At(int i) const { return ms[(head - count + i + kCapacity * 2) % kCapacity]; }
    void Clear() { head = count = 0; }
};

struct FrameGraphStyle {
    ImVec2 size = ImVec2(0, 0);  // 0 width fills, 0 height = 4.5 x font size
    float budget_ms = 1000.0f / 60.0f;  // 0 hides the budget line
    float budget2_ms = 0.0f;     // optional second target line, 1000 / 144 for example
    float max_ms = 0.0f;         // 0 = auto, at least 1.5 x budget
    bool bars = true;            // false draws a line
    bool show_stats = true;
    bool auto_sample = true;     // FrameGraph(label) records io.DeltaTime when nothing was pushed
    ImU32 good = 0;
    ImU32 warn = 0;
    ImU32 bad = 0;
    ImU32 bg = 0;
};

namespace detail {

struct FrameSlot {
    ImGuiID id;
    int used_frame;
    int push_frame;
    FrameSamples s;
};

inline FrameSlot* FrameHistory(ImGuiID id) {
    static FrameSlot slots[8];
    FrameSlot* lru = &slots[0];
    for (FrameSlot& s : slots) {
        if (s.id == id && id != 0) {
            s.used_frame = ImGui::GetFrameCount();
            return &s;
        }
        if (s.used_frame < lru->used_frame) lru = &s;
    }
    lru->id = id;
    lru->used_frame = ImGui::GetFrameCount();
    lru->push_frame = -1;
    lru->s.Clear();
    return lru;
}

template <class Get>
inline void DrawFrameGraph(const char* label, int count, int capacity, Get get, const FrameGraphStyle& fs) {
    float font = ImGui::GetFontSize();
    ImVec2 sz(ResolveW(fs.size.x, font * 4.0f), fs.size.y > 0.0f ? fs.size.y : font * 4.5f);
    ImGuiID id = ImGui::GetID(label);
    ImVec2 mn = ImGui::GetCursorScreenPos();
    ImGui::Dummy(sz);
    if (!ImGui::IsItemVisible()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 mx = Add(mn, sz);
    float rounding = Rounding();
    dl->AddRectFilled(mn, mx, fs.bg ? ImGui::GetColorU32(fs.bg) : CardBg(0.05f), rounding);
    const Style& st = GetStyle();
    auto sample = [&](int i) {
        float v = Fin(get(i));
        return v < 0.0f ? 0.0f : v;
    };
    float budget = Fin(fs.budget_ms);
    float peak = 0.0f, sum = 0.0f;
    // 1% low: the k-th slowest frame, k = 1% of the samples (8 at most)
    float slow[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int k = MinI(MaxI(count / 100, 1), 8);
    for (int i = 0; i < count; ++i) {
        float v = sample(i);
        peak = Max(peak, v);
        sum += v;
        if (v > slow[k - 1]) {
            int j = k - 1;
            while (j > 0 && slow[j - 1] < v) {
                slow[j] = slow[j - 1];
                --j;
            }
            slow[j] = v;
        }
    }
    float top = fs.max_ms > 0.0f ? fs.max_ms : imb::AnimateTo(id ^ 0x2f0b3c1du, Max(budget * 1.5f, peak * 1.15f), 0.4f);
    if (top <= 0.0f) top = 1.0f;
    float pad = Max(2.0f, font * 0.2f);
    ImVec2 g0(mn.x + pad, mn.y + pad), g1(mx.x - pad, mx.y - pad);
    float gw = g1.x - g0.x, gh = g1.y - g0.y;
    ImU32 good = Pick(fs.good, st.success), warn = Pick(fs.warn, st.warning), bad = Pick(fs.bad, st.danger);
    auto color_for = [&](float v) {
        if (budget <= 0.0f) return good;
        if (v <= budget) return good;
        if (v <= budget * 2.0f) return Mix(warn, bad, Clamp01((v - budget * 1.5f) / budget));
        return bad;
    };
    dl->PushClipRect(g0, g1, true);
    if (count > 0) {
        float slot = gw / (float)capacity;
        float x_end = g1.x;
        if (fs.bars) {
            float bw = Max(1.0f, slot - (slot > 3.0f ? 1.0f : 0.0f));
            for (int i = 0; i < count; ++i) {
                float v = sample(i);
                float x = x_end - (float)(count - i) * slot;
                float y = g1.y - Clamp01(v / top) * gh;
                ImU32 c = color_for(v);
                dl->AddRectFilledMultiColor(ImVec2(x, y), ImVec2(x + bw, g1.y), c, c, WithAlpha(c, 0.35f),
                                            WithAlpha(c, 0.35f));
            }
        } else {
            for (int i = 0; i < count; ++i) {
                float v = sample(i);
                dl->PathLineTo(ImVec2(x_end - (float)(count - 1 - i) * slot, g1.y - Clamp01(v / top) * gh));
            }
            Stroke(dl, Accent(), Max(1.5f, font * 0.1f));
        }
    }
    const float lines[2] = {budget, Fin(fs.budget2_ms)};
    for (int li = 0; li < 2; ++li) {
        float b = lines[li];
        if (b <= 0.0f || b >= top) continue;
        float y = g1.y - b / top * gh;
        DashedLine(dl, ImVec2(g0.x, y), ImVec2(g1.x, y), TextDim(li == 0 ? 0.8f : 0.5f), 1.0f, font * 0.45f, font * 0.3f);
        char bbuf[24];
        snprintf(bbuf, sizeof(bbuf), "%.1f ms", (double)b);
        float sf = font * 0.78f;
        ImVec2 ts = TextSize(sf, bbuf);
        ImVec2 tp(g1.x - ts.x - pad, y - ts.y - 1.0f);
        if (tp.y < g0.y) tp.y = y + 1.0f;
        Text(dl, sf, tp, TextDim(), bbuf);
    }
    dl->PopClipRect();
    if (fs.show_stats) {
        float avg = count > 0 ? sum / (float)count : 0.0f;
        char sbuf[96];
        const char* end = LabelEnd(label);
        int n = 0;
        if (end > label) n = snprintf(sbuf, sizeof(sbuf), "%.*s  ", (int)(end - label), label);
        if (n < 0 || n >= (int)sizeof(sbuf)) n = 0;
        float low = slow[k - 1];
        snprintf(sbuf + n, sizeof(sbuf) - (size_t)n, "%.1f ms  %d fps  1%% low %d", (double)avg,
                 avg > 0.0f ? (int)(1000.0f / avg + 0.5f) : 0, low > 0.0f ? (int)(1000.0f / low + 0.5f) : 0);
        float sf = font * 0.8f;
        ImVec2 ts = TextSize(sf, sbuf);
        ImVec2 tp(mn.x + pad * 2.0f, mn.y + pad);
        dl->AddRectFilled(ImVec2(tp.x - pad, tp.y), ImVec2(tp.x + ts.x + pad, tp.y + ts.y),
                          WithAlpha(CardBg(0.02f), 0.75f), rounding);
        Text(dl, sf, tp, TextCol(0.9f), sbuf);
    }
}

}  // namespace detail

// ms[] is a ring buffer starting at offset (oldest first), like ImGui::PlotLines
inline void FrameGraph(const char* label, const float* ms, int count, int offset = 0,
                       const FrameGraphStyle& fs = FrameGraphStyle()) {
    if (!ms || count < 0) count = 0;
    int off = count > 0 ? ((offset % count) + count) % count : 0;
    detail::DrawFrameGraph(label, count, detail::MaxI(count, 1), [&](int i) { return ms[(off + i) % count]; }, fs);
}

inline void FrameGraph(const char* label, const FrameSamples& s, const FrameGraphStyle& fs = FrameGraphStyle()) {
    detail::DrawFrameGraph(label, s.count, (int)FrameSamples::kCapacity, [&](int i) { return s.At(i); }, fs);
}

// pushes into the history FrameGraph(label) draws; call it in the same ID scope
inline void FrameGraphPush(const char* label, float ms) {
    detail::FrameSlot* slot = detail::FrameHistory(ImGui::GetID(label));
    slot->push_frame = slot->used_frame;
    slot->s.Push(ms);
}

// keeps its own history (8 graphs at most); records io.DeltaTime unless FrameGraphPush fed it this frame
inline void FrameGraph(const char* label, const FrameGraphStyle& fs = FrameGraphStyle()) {
    detail::FrameSlot* slot = detail::FrameHistory(ImGui::GetID(label));
    if (fs.auto_sample && slot->push_frame != slot->used_frame) {
        slot->s.Push(imb::DeltaTime() * 1000.0f);
        slot->push_frame = slot->used_frame;
    }
    FrameGraph(label, slot->s, fs);
}

inline int FrameGraphSampleCount(const char* label) { return detail::FrameHistory(ImGui::GetID(label))->s.count; }

inline void FrameGraph(const char* label, const float* ms, int count, float budget_ms, const ImVec2& size) {
    FrameGraphStyle fs;
    fs.budget_ms = budget_ms;
    fs.size = size;
    FrameGraph(label, ms, count, 0, fs);
}

// generated code with no array bound: live frame times
inline void FrameGraph(const char* label, std::nullptr_t, std::nullptr_t, float budget_ms, const ImVec2& size) {
    FrameGraphStyle fs;
    fs.budget_ms = budget_ms;
    fs.size = size;
    FrameGraph(label, fs);
}

}  // namespace disp
}  // namespace imb
