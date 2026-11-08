#pragma once
// keyframe motions: scale/rotate/skew/translate/tint applied to an item's vertices after it draws
#include "imgui.h"
#include "imb_runtime.h"

#include <cmath>
#include <cstring>

namespace imb {

enum class Prop : unsigned char {
    Opacity, TranslateX, TranslateY, Scale, ScaleX, ScaleY, Rotate, SkewX, SkewY,
    Tint, Reveal, TextReveal, Glow, Shadow, Count
};

// p[] by kind: CubicBezier x1,y1,x2,y2 / Steps n / Spring stiffness,damping
enum class EaseKind : unsigned char { Named, CubicBezier, Steps, Spring };
struct EaseSpec {
    EaseKind kind = EaseKind::Named;
    Ease named = Ease::Linear;
    float p[4] = {0, 0, 0, 0};
};

// ease applies to the segment from this key to the next one
struct Keyframe {
    float t;
    float v[4];
    EaseSpec ease;
};

struct Track {
    Prop prop;
    const Keyframe* keys;
    int count;
};

enum class Direction : unsigned char { Normal, Reverse, Alternate, AlternateReverse };
enum class Fill : unsigned char { None, Forwards, Backwards, Both };
// edge the visible area is anchored to: Left grows from the left edge to the right as Reveal goes
// 0 to 1 (and shrinks back toward it going 1 to 0), Up grows down from the top, Center from the middle
enum class RevealDir : unsigned char { Left, Right, Up, Down, Center };
enum class Trigger : unsigned char { Enter, Exit, Hover, Press, Focus, Loop, Change, Attention };

// rotate/skew in degrees, translate in px at 100% (scaled by FontSize/13), origin is a fraction of the item rect
struct AnimDef {
    const char* name = nullptr;
    const char* category = nullptr;
    float duration = 1.0f;
    float delay = 0.0f;
    int iterations = 1;   // 0 = infinite
    Direction direction = Direction::Normal;
    Fill fill = Fill::Both;
    ImVec2 origin = ImVec2(0.5f, 0.5f);
    RevealDir reveal_dir = RevealDir::Left;
    Trigger default_trigger = Trigger::Enter;
    const Track* tracks = nullptr;
    int track_count = 0;
};

struct Pose {
    float opacity = 1, scale_x = 1, scale_y = 1, rotate = 0, skew_x = 0, skew_y = 0;
    ImVec2 translate = ImVec2(0, 0);
    ImVec4 tint = ImVec4(1, 1, 1, 1);
    float reveal = 1, text_reveal = 1, glow = 0, shadow = 0;
};

struct MotionBinding {
    const AnimDef* def = nullptr;
    Trigger trigger = Trigger::Enter;
    float duration_scale = 1.0f;
    float delay = 0.0f;
    int iterations = -1;   // -1 = use def, 0 = infinite
    float stagger = 0.0f;  // extra delay per stagger_index
};

struct MotionOptions {
    bool visible = true;
    int stagger_index = 0;
    bool force_hover = false, force_press = false;
    ImU32 glow_color = 0, shadow_color = 0;   // 0 = ImGuiCol_CheckMark / translucent black
};

namespace detail {

const int kMaxMotionDepth = 32;
const int kMaxMotionBindings = 8;
const int kMaxAnims = 4096;
const int kAnimIndexCap = 8192;
const float kMotionPi = 3.14159265358979f;

// NaN comes out as 0 (or 0 for Sat), so broken keyframe data can't reach the vertices
inline float Sat(float v) { return v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f; }
inline float Lim(float v, float m) { return v > m ? m : (v < -m ? -m : (v == v ? v : 0.0f)); }

inline float SampleCubicBezier(float x1, float y1, float x2, float y2, float x) {
    x1 = Sat(x1);
    x2 = Sat(x2);
    y1 = Lim(y1, 100.0f);
    y2 = Lim(y2, 100.0f);
    float cx = 3.0f * x1, bx = 3.0f * (x2 - x1) - cx, ax = 1.0f - cx - bx;
    float cy = 3.0f * y1, by = 3.0f * (y2 - y1) - cy, ay = 1.0f - cy - by;
    float s = x;
    bool solved = false;
    for (int i = 0; i < 8; i++) {
        float err = ((ax * s + bx) * s + cx) * s - x;
        if (std::fabs(err) < 1e-6f) { solved = true; break; }
        float d = (3.0f * ax * s + 2.0f * bx) * s + cx;
        if (std::fabs(d) < 1e-6f) break;
        s -= err / d;
    }
    if (!solved || s < 0.0f || s > 1.0f) {
        float lo = 0.0f, hi = 1.0f;
        s = x;
        for (int i = 0; i < 40; i++) {
            float v = ((ax * s + bx) * s + cx) * s;
            if (std::fabs(v - x) < 1e-6f) break;
            if (v < x) lo = s; else hi = s;
            s = (lo + hi) * 0.5f;
        }
    }
    return ((ay * s + by) * s + cy) * s;
}

// unit step response of a mass 1 spring, stretched so the envelope is down to 0.1% at t = 1
inline float SampleSpring(float stiffness, float damping, float t) {
    if (!(stiffness > 0.0f)) stiffness = 170.0f;
    if (!(damping > 0.0f)) damping = 26.0f;
    if (stiffness < 0.01f) stiffness = 0.01f;
    if (stiffness > 1e6f) stiffness = 1e6f;
    if (damping > 1e6f) damping = 1e6f;
    float w = std::sqrt(stiffness), z = damping / (2.0f * w);
    // almost no damping would ring for ages, and huge ratios overflow z * z
    if (z < 0.005f) z = 0.005f;
    if (z > 1e4f) z = 1e4f;
    const float k = 6.9078f;   // ln(1000)
    float settle;
    if (z < 0.999f) settle = k / (z * w);
    else if (z <= 1.001f) settle = (k + 2.33f) / w;
    else settle = k * (z + std::sqrt(z * z - 1.0f)) / w;
    auto x = [&](float T) {
        if (z < 0.999f) {
            float wd = w * std::sqrt(1.0f - z * z);
            return 1.0f - std::exp(-z * w * T) * (std::cos(wd * T) + (z * w / wd) * std::sin(wd * T));
        }
        if (z <= 1.001f) return 1.0f - std::exp(-w * T) * (1.0f + w * T);
        // r1 as w / (z + r) because z - r cancels badly for big z
        float r = std::sqrt(z * z - 1.0f), r1 = -w / (z + r), r2 = -w * (z + r);
        return 1.0f - (r2 * std::exp(r1 * T) - r1 * std::exp(r2 * T)) / (r2 - r1);
    };
    return x(t * settle) + (1.0f - x(settle)) * t;
}

}  // namespace detail

inline float SampleEase(const EaseSpec& e, float t) {
    if (!(t > 0.0f)) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    switch (e.kind) {
    case EaseKind::Named: return EaseApply(e.named, t);
    case EaseKind::CubicBezier: return detail::SampleCubicBezier(e.p[0], e.p[1], e.p[2], e.p[3], t);
    case EaseKind::Steps: {
        float n = std::floor(e.p[0]);
        if (!(n >= 1.0f)) n = 1.0f;
        if (n > 1e6f) n = 1e6f;
        return std::floor(t * n) / n;
    }
    case EaseKind::Spring: return detail::SampleSpring(e.p[0], e.p[1], t);
    }
    return t;
}

inline void ComposePose(Pose& into, const Pose& p) {
    into.opacity *= p.opacity;
    into.translate.x += p.translate.x;
    into.translate.y += p.translate.y;
    into.scale_x *= p.scale_x;
    into.scale_y *= p.scale_y;
    into.rotate += p.rotate;
    into.skew_x += p.skew_x;
    into.skew_y += p.skew_y;
    into.tint.x *= p.tint.x;
    into.tint.y *= p.tint.y;
    into.tint.z *= p.tint.z;
    into.tint.w *= p.tint.w;
    if (p.reveal < into.reveal) into.reveal = p.reveal;
    if (p.text_reveal < into.text_reveal) into.text_reveal = p.text_reveal;
    if (p.glow > into.glow) into.glow = p.glow;
    if (p.shadow > into.shadow) into.shadow = p.shadow;
}

namespace detail {

struct MotionTiming {
    float delay, dur;
    int iters;   // 0 = infinite
    Direction dir;
    Fill fill;
};

// NaN, negatives and huge values from hostile data end up as 0 or the cap
inline float SaneSeconds(float v) { return v >= 0.0f ? (v < 3600.0f ? v : 3600.0f) : 0.0f; }

inline MotionTiming DefTiming(const AnimDef& d, float duration_scale) {
    MotionTiming m;
    m.dur = SaneSeconds(d.duration * (duration_scale > 0.0f ? duration_scale : 0.0f));
    if (m.dur < 1e-4f) m.dur = 0.0f;
    m.delay = SaneSeconds(d.delay);
    m.iters = d.iterations < 0 ? 1 : d.iterations;
    m.dir = d.direction;
    m.fill = d.fill;
    return m;
}

inline bool TimingInfinite(const MotionTiming& m) { return m.iters == 0 && m.dur > 0.0f; }
inline float TimingEnd(const MotionTiming& m) { return m.delay + m.dur * (float)(m.iters > 0 ? m.iters : 0); }
inline float TimingCycle(const MotionTiming& m) {
    bool alt = m.dir == Direction::Alternate || m.dir == Direction::AlternateReverse;
    return m.dur * (alt ? 2.0f : 1.0f);
}

inline bool IterForward(Direction d, long long iter) {
    switch (d) {
    case Direction::Normal: return true;
    case Direction::Reverse: return false;
    case Direction::Alternate: return (iter & 1) == 0;
    case Direction::AlternateReverse: return (iter & 1) != 0;
    }
    return true;
}

// false means the animation has no effect at this time (outside its active phase, no fill)
inline bool TimingProgress(const MotionTiming& m, float time, float* progress) {
    float local = time - m.delay;
    if (local < 0.0f) {
        if (m.fill != Fill::Backwards && m.fill != Fill::Both) return false;
        *progress = IterForward(m.dir, 0) ? 0.0f : 1.0f;
        return true;
    }
    if (!TimingInfinite(m) && local >= m.dur * (float)(m.iters > 0 ? m.iters : 0)) {
        if (m.fill != Fill::Forwards && m.fill != Fill::Both) return false;
        *progress = IterForward(m.dir, (m.iters > 0 ? m.iters : 1) - 1) ? 1.0f : 0.0f;
        return true;
    }
    float x = local / m.dur;
    if (!(x < 1e9f)) x = 1e9f;   // keeps the iteration count castable, also for tiny durations and NaN
    float fi = std::floor(x), frac = x - fi;
    *progress = IterForward(m.dir, (long long)fi) ? frac : 1.0f - frac;
    return true;
}

inline void SampleTrack(const Track& tr, float p, float* out) {
    const Keyframe* k = tr.keys;
    int n = tr.count;
    if (n == 1 || p <= k[0].t) { memcpy(out, k[0].v, sizeof(float) * 4); return; }
    if (p >= k[n - 1].t) { memcpy(out, k[n - 1].v, sizeof(float) * 4); return; }
    int i = 0;
    while (i < n - 2 && p >= k[i + 1].t) i++;
    float seg = k[i + 1].t - k[i].t;
    float u = seg > 0.0f ? (p - k[i].t) / seg : 1.0f;
    float e = SampleEase(k[i].ease, u);
    for (int c = 0; c < 4; c++) out[c] = k[i].v[c] + (k[i + 1].v[c] - k[i].v[c]) * e;
}

inline void ApplyProp(Pose& p, Prop prop, const float* v) {
    switch (prop) {
    case Prop::Opacity: p.opacity *= v[0]; break;
    case Prop::TranslateX: p.translate.x += v[0]; break;
    case Prop::TranslateY: p.translate.y += v[0]; break;
    case Prop::Scale: p.scale_x *= v[0]; p.scale_y *= v[0]; break;
    case Prop::ScaleX: p.scale_x *= v[0]; break;
    case Prop::ScaleY: p.scale_y *= v[0]; break;
    case Prop::Rotate: p.rotate += v[0]; break;
    case Prop::SkewX: p.skew_x += v[0]; break;
    case Prop::SkewY: p.skew_y += v[0]; break;
    case Prop::Tint: p.tint.x *= v[0]; p.tint.y *= v[1]; p.tint.z *= v[2]; p.tint.w *= v[3]; break;
    case Prop::Reveal: if (v[0] < p.reveal) p.reveal = v[0]; break;
    case Prop::TextReveal: if (v[0] < p.text_reveal) p.text_reveal = v[0]; break;
    case Prop::Glow: if (v[0] > p.glow) p.glow = v[0]; break;
    case Prop::Shadow: if (v[0] > p.shadow) p.shadow = v[0]; break;
    case Prop::Count: break;
    }
}

inline Pose SampleProgress(const AnimDef& def, float progress) {
    Pose p;
    for (int i = 0; i < def.track_count; i++) {
        const Track& tr = def.tracks[i];
        if (!tr.keys || tr.count <= 0) continue;
        float v[4];
        SampleTrack(tr, progress, v);
        ApplyProp(p, tr.prop, v);
    }
    return p;
}

inline Pose SampleTiming(const AnimDef& def, const MotionTiming& m, float time) {
    float progress;
    if (!TimingProgress(m, time, &progress)) return Pose();
    return SampleProgress(def, progress);
}

}  // namespace detail

inline Pose SampleAnim(const AnimDef& def, float time_s, float duration_scale = 1.0f) {
    return detail::SampleTiming(def, detail::DefTiming(def, duration_scale), time_s);
}

inline bool AnimFinished(const AnimDef& def, float time_s, float duration_scale = 1.0f) {
    detail::MotionTiming m = detail::DefTiming(def, duration_scale);
    if (detail::TimingInfinite(m)) return false;
    return time_s >= detail::TimingEnd(m);
}

// registry

namespace detail {

typedef void (*MotionPresetsFn)();

// imb_motion_presets.h stores its RegisterAll here during static init
inline MotionPresetsFn& MotionPresetsHook() {
    static MotionPresetsFn fn = nullptr;
    return fn;
}

struct AnimRegistry {
    const AnimDef* defs[kMaxAnims];
    int count;
    int index[kAnimIndexCap];   // defs index + 1, 0 = empty
    bool presets_done;
};

inline AnimRegistry& Registry() {
    static AnimRegistry r;
    return r;
}

inline unsigned NameHash(const char* s) {
    unsigned h = 2166136261u;
    for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

inline int* IndexSlot(AnimRegistry& r, const char* name) {
    unsigned mask = (unsigned)kAnimIndexCap - 1, i = NameHash(name) & mask;
    for (;;) {
        int& s = r.index[i];
        if (s == 0 || std::strcmp(r.defs[s - 1]->name, name) == 0) return &s;
        i = (i + 1) & mask;
    }
}

inline void LoadPresetsOnce() {
    AnimRegistry& r = Registry();
    if (r.presets_done) return;
    r.presets_done = true;
    if (MotionPresetsHook()) MotionPresetsHook()();
}

}  // namespace detail

// replaces an entry with the same name in place, so the order stays stable
inline void RegisterAnim(const AnimDef* def) {
    if (!def || !def->name) return;
    detail::LoadPresetsOnce();
    detail::AnimRegistry& r = detail::Registry();
    int* slot = detail::IndexSlot(r, def->name);
    if (*slot != 0) {
        r.defs[*slot - 1] = def;
        return;
    }
    if (r.count >= detail::kMaxAnims) return;
    r.defs[r.count++] = def;
    *slot = r.count;
}

inline const AnimDef* FindAnim(const char* name) {
    if (!name) return nullptr;
    detail::LoadPresetsOnce();
    detail::AnimRegistry& r = detail::Registry();
    int s = *detail::IndexSlot(r, name);
    return s ? r.defs[s - 1] : nullptr;
}

inline bool UnregisterAnim(const char* name) {
    if (!name) return false;
    detail::LoadPresetsOnce();
    detail::AnimRegistry& r = detail::Registry();
    int s = *detail::IndexSlot(r, name);
    if (!s) return false;
    for (int i = s; i < r.count; i++) r.defs[i - 1] = r.defs[i];
    r.count--;
    memset(r.index, 0, sizeof(r.index));
    for (int i = 0; i < r.count; i++) *detail::IndexSlot(r, r.defs[i]->name) = i + 1;
    return true;
}

inline int AnimCount() {
    detail::LoadPresetsOnce();
    return detail::Registry().count;
}

inline const AnimDef* AnimAt(int i) {
    detail::LoadPresetsOnce();
    detail::AnimRegistry& r = detail::Registry();
    return (i >= 0 && i < r.count) ? r.defs[i] : nullptr;
}

// playback

namespace detail {

enum MotionPhase : unsigned char { kPhaseShown, kPhaseExiting, kPhaseHidden };

struct MotionSlot {
    const AnimDef* def;
    float t;
    Trigger trigger;
    unsigned char started, done;
};

struct MotionEntry {
    ImGuiID key;
    int state;   // 0 empty, 1 live, 2 tombstone
    int last_frame;
    double last_time;
    float w, h;   // last frame group size, plain floats so the entry stays memset-able
    unsigned pending;   // TriggerMotion bits, applied at the next BeginMotion
    float text_reveal;
    unsigned char seen, has_size, visible, phase, hovered, active, focused, playing;
    MotionSlot slots[kMaxMotionBindings];
};

struct MotionTable {
    ImVector<MotionEntry> slots;
    int live = 0;
    int used = 0;

    MotionEntry* Find(ImGuiID key) {
        if (slots.Size == 0) return nullptr;
        unsigned mask = (unsigned)slots.Size - 1, i = AnimTable::Hash(key) & mask;
        for (;;) {
            MotionEntry& s = slots[(int)i];
            if (s.state == 0) return nullptr;
            if (s.state == 1 && s.key == key) return &s;
            i = (i + 1) & mask;
        }
    }
    MotionEntry* Place(ImGuiID key) {
        unsigned mask = (unsigned)slots.Size - 1, i = AnimTable::Hash(key) & mask;
        for (;;) {
            MotionEntry& s = slots[(int)i];
            if (s.state != 1) {
                if (s.state == 0) used++;
                live++;
                memset(&s, 0, sizeof(s));
                s.key = key;
                s.state = 1;
                s.last_frame = -1;
                s.text_reveal = 1.0f;
                return &s;
            }
            i = (i + 1) & mask;
        }
    }
    void Rehash(int cap) {
        ImVector<MotionEntry> old;
        old.swap(slots);
        slots.resize(cap);
        memset(slots.Data, 0, sizeof(MotionEntry) * (size_t)cap);
        live = used = 0;
        for (int i = 0; i < old.Size; i++)
            if (old[i].state == 1) *Place(old[i].key) = old[i];
    }
    MotionEntry* Insert(ImGuiID key) {
        if (slots.Size == 0 || (used + 1) * 4 > slots.Size * 3) {
            int cap = slots.Size < 64 ? 64 : slots.Size;
            while ((live + 1) * 2 > cap) cap *= 2;
            Rehash(cap);
        }
        return Place(key);
    }
    void Sweep(double now) {
        for (int i = 0; i < slots.Size; i++) {
            MotionEntry& s = slots[i];
            if (s.state == 1 && now - s.last_time > kGcAge) { s.state = 2; live--; }
        }
    }
    void Clear() {
        slots.clear();
        live = used = 0;
    }
};

struct MotionFrame {
    ImGuiID id;
    ImDrawList* dl;
    int vtx_start;
    Pose pose;
    ImVec2 origin;
    bool clip, hide;
};

struct MotionRuntime {
    MotionTable table;
    MotionFrame stack[kMaxMotionDepth];
    int depth = 0;
    int overflow = 0;
    int frame = -1;
    double now = 0.0;
    double last_sweep = 0.0;
};

inline MotionRuntime& MotionState() {
    static MotionRuntime s;
    return s;
}

inline void MotionTick(MotionRuntime& r) {
    int f = ImGui::GetFrameCount();
    if (r.frame == f) return;
    r.frame = f;
    r.now = ImGui::GetTime();
    // a scope left open last frame would have broken the group stack anyway, start clean
    r.depth = r.overflow = 0;
    if (r.now < r.last_sweep) r.last_sweep = r.now;
    if (r.now - r.last_sweep >= 1.0) {
        r.table.Sweep(r.now);
        r.last_sweep = r.now;
    }
}

inline MotionEntry* MotionAcquire(MotionRuntime& r, ImGuiID id) {
    MotionEntry* e = r.table.Find(id);
    if (!e) e = r.table.Insert(id);
    e->last_time = r.now;
    return e;
}

inline MotionTiming BindingTiming(const MotionBinding& b, int stagger_index) {
    MotionTiming m = DefTiming(*b.def, b.duration_scale);
    m.delay = SaneSeconds(m.delay + SaneSeconds(b.delay) + SaneSeconds(b.stagger) * (float)stagger_index);
    if (b.iterations >= 0) m.iters = b.iterations;
    else if (b.trigger == Trigger::Loop) m.iters = 0;
    // an exit that never ends would keep the item alive forever
    if (b.trigger == Trigger::Exit && m.iters == 0) m.iters = 1;
    return m;
}

inline void RestartSlot(MotionSlot& s) {
    s.t = 0.0f;
    s.started = 1;
    s.done = 0;
}

inline void InitSlot(MotionSlot& s, const MotionBinding& b, bool shown) {
    s.def = b.def;
    s.trigger = b.trigger;
    s.t = 0.0f;
    s.done = 0;
    s.started = (b.trigger == Trigger::Loop || (b.trigger == Trigger::Enter && shown)) ? 1 : 0;
}

inline bool PoseHasLinear(const Pose& p) {
    return p.scale_x != 1.0f || p.scale_y != 1.0f || p.rotate != 0.0f || p.skew_x != 0.0f || p.skew_y != 0.0f;
}

// keeps long running loops from losing float precision
inline void WrapTime(float& t, const MotionTiming& m) {
    float cycle = TimingCycle(m);
    if (cycle > 0.0f && t - m.delay > cycle * 64.0f) t = m.delay + std::fmod(t - m.delay, cycle);
}

inline void RevealRect(RevealDir dir, float r, ImVec2& mn, ImVec2& mx) {
    float w = mx.x - mn.x, h = mx.y - mn.y;
    switch (dir) {
    case RevealDir::Left: mx.x = mn.x + w * r; break;
    case RevealDir::Right: mn.x = mx.x - w * r; break;
    case RevealDir::Up: mx.y = mn.y + h * r; break;
    case RevealDir::Down: mn.y = mx.y - h * r; break;
    case RevealDir::Center: {
        float cx = mn.x + w * 0.5f, cy = mn.y + h * 0.5f;
        mn = ImVec2(cx - w * r * 0.5f, cy - h * r * 0.5f);
        mx = ImVec2(cx + w * r * 0.5f, cy + h * r * 0.5f);
        break;
    }
    }
}

inline unsigned char MulChannel(ImU32 c, int shift, int m) {
    return (unsigned char)((((c >> shift) & 0xFF) * (ImU32)m + 127) / 255);
}

inline void TransformVerts(ImDrawList* dl, int start, const Pose& p, const ImVec2& origin, bool hide) {
    int end = dl->VtxBuffer.Size;
    if (end <= start) return;
    float alpha = Sat(p.opacity) * Sat(p.tint.w);
    if (hide) alpha = 0.0f;
    float tr = Sat(p.tint.x), tg = Sat(p.tint.y), tb = Sat(p.tint.z);
    bool xf = PoseHasLinear(p) || p.translate.x != 0.0f || p.translate.y != 0.0f;
    bool col = alpha < 1.0f || tr < 1.0f || tg < 1.0f || tb < 1.0f;
    if (!xf && !col) return;
    const float deg = kMotionPi / 180.0f;
    float sk_x = Lim(p.skew_x, 89.0f), sk_y = Lim(p.skew_y, 89.0f);
    float rot = Lim(std::fmod(p.rotate, 360.0f), 360.0f);
    float c = std::cos(rot * deg), s = std::sin(rot * deg);
    float kx = std::tan(sk_x * deg), ky = std::tan(sk_y * deg);
    float sx = Lim(p.scale_x, 1e4f), sy = Lim(p.scale_y, 1e4f);
    float m00 = c * sx - s * ky * sx, m01 = c * kx * sy - s * sy;
    float m10 = s * sx + c * ky * sx, m11 = s * kx * sy + c * sy;
    float dpi = ImGui::GetFontSize() / 13.0f;
    float tx = origin.x + Lim(p.translate.x, 1e5f) * dpi, ty = origin.y + Lim(p.translate.y, 1e5f) * dpi;
    int mr = (int)(tr * 255.0f + 0.5f), mg = (int)(tg * 255.0f + 0.5f);
    int mb = (int)(tb * 255.0f + 0.5f), ma = (int)(alpha * 255.0f + 0.5f);
    ImDrawVert* v = dl->VtxBuffer.Data + start;
    ImDrawVert* v_end = dl->VtxBuffer.Data + end;
    for (; v < v_end; v++) {
        if (xf) {
            float dx = v->pos.x - origin.x, dy = v->pos.y - origin.y;
            v->pos.x = tx + m00 * dx + m01 * dy;
            v->pos.y = ty + m10 * dx + m11 * dy;
        }
        if (col) {
            ImU32 cc = v->col;
            v->col = ((ImU32)MulChannel(cc, IM_COL32_R_SHIFT, mr) << IM_COL32_R_SHIFT) |
                     ((ImU32)MulChannel(cc, IM_COL32_G_SHIFT, mg) << IM_COL32_G_SHIFT) |
                     ((ImU32)MulChannel(cc, IM_COL32_B_SHIFT, mb) << IM_COL32_B_SHIFT) |
                     ((ImU32)MulChannel(cc, IM_COL32_A_SHIFT, ma) << IM_COL32_A_SHIFT);
        }
    }
}

inline void UpdatePhase(MotionEntry* e, const MotionBinding* bindings, int count, bool visible, bool step) {
    if (!e->seen) {
        e->seen = 1;
        e->visible = visible ? 1 : 0;
        e->phase = visible ? kPhaseShown : kPhaseHidden;
        for (int i = 0; i < count; i++) InitSlot(e->slots[i], bindings[i], visible);
        return;
    }
    for (int i = 0; i < count; i++) {
        MotionSlot& s = e->slots[i];
        if (s.def != bindings[i].def || s.trigger != bindings[i].trigger) InitSlot(s, bindings[i], e->phase == kPhaseShown);
    }
    if (!step || visible == (e->visible != 0)) return;
    e->visible = visible ? 1 : 0;
    bool any_exit = false;
    for (int i = 0; i < count; i++) {
        MotionSlot& s = e->slots[i];
        if (visible && s.trigger == Trigger::Enter) {
            RestartSlot(s);
        } else if (visible && s.trigger == Trigger::Exit) {
            s.started = 0;
        } else if (!visible && s.trigger == Trigger::Exit && s.def) {
            RestartSlot(s);
            any_exit = true;
        }
    }
    e->phase = visible ? kPhaseShown : (any_exit ? kPhaseExiting : kPhaseHidden);
}

// hover/press/focus move a playhead forward while on and back while off, holding at either end
inline Pose AdvanceStateSlot(MotionSlot& s, const AnimDef& def, MotionTiming m, bool on, float dt, bool* playing) {
    bool infinite = TimingInfinite(m);
    float end = TimingEnd(m);
    if (infinite) {
        float cycle = TimingCycle(m);
        if (!on && s.t > m.delay && cycle > 0.0f) s.t = m.delay + std::fmod(s.t - m.delay, cycle);
        else WrapTime(s.t, m);
    }
    s.t += on ? dt : -dt;
    if (s.t < 0.0f) s.t = 0.0f;
    if (!infinite && s.t > end) s.t = end;
    if (on ? (infinite || s.t < end) : s.t > 0.0f) *playing = true;
    m.fill = Fill::Both;
    return SampleTiming(def, m, s.t);
}

inline Pose AdvanceTimedSlot(MotionSlot& s, const AnimDef& def, const MotionTiming& m, float dt, bool* playing) {
    if (!s.started) return Pose();
    if (!s.done) {
        bool infinite = TimingInfinite(m);
        s.t += dt;
        if (!infinite && s.t >= TimingEnd(m)) {
            s.t = TimingEnd(m);
            s.done = 1;
        } else {
            *playing = true;
            if (infinite) WrapTime(s.t, m);
        }
    }
    return SampleTiming(def, m, s.t);
}

struct MotionEval {
    Pose pose;
    ImVec2 origin = ImVec2(0.5f, 0.5f);
    RevealDir reveal_dir = RevealDir::Left;
    bool playing = false, exit_running = false;
};

inline MotionEval Evaluate(MotionEntry* e, const MotionBinding* bindings, int count, const MotionOptions& opt, float dt) {
    MotionEval ev;
    bool on_hover = e->hovered || opt.force_hover, on_press = e->active || opt.force_press;
    bool origin_set = false, origin_linear = false;
    float reveal_min = 1.0f;
    for (int i = 0; i < count; i++) {
        const MotionBinding& b = bindings[i];
        MotionSlot& s = e->slots[i];
        if (!b.def) continue;
        MotionTiming m = BindingTiming(b, opt.stagger_index);
        Pose bp;
        if (s.trigger == Trigger::Hover) bp = AdvanceStateSlot(s, *b.def, m, on_hover, dt, &ev.playing);
        else if (s.trigger == Trigger::Press) bp = AdvanceStateSlot(s, *b.def, m, on_press, dt, &ev.playing);
        else if (s.trigger == Trigger::Focus) bp = AdvanceStateSlot(s, *b.def, m, e->focused != 0, dt, &ev.playing);
        else bp = AdvanceTimedSlot(s, *b.def, m, dt, &ev.playing);
        if (s.trigger == Trigger::Exit && s.started && !s.done) ev.exit_running = true;
        if (bp.reveal < reveal_min) {
            reveal_min = bp.reveal;
            ev.reveal_dir = b.def->reveal_dir;
        }
        // pivot on the first binding that actually scales/rotates/skews this frame
        bool linear = PoseHasLinear(bp);
        if (!origin_set || (linear && !origin_linear)) {
            ev.origin = b.def->origin;
            origin_set = true;
            origin_linear = linear;
        }
        ComposePose(ev.pose, bp);
    }
    return ev;
}

// glow/shadow go in before the item so they sit behind it and get transformed with it
inline void BeginEffects(MotionFrame& f, const MotionEntry* e, const MotionEval& ev, const MotionOptions& opt) {
    const Pose& pose = ev.pose;
    // the group min is the cursor, only the size comes from last frame
    ImVec2 mn = ImGui::GetCursorScreenPos(), mx(mn.x + e->w, mn.y + e->h);
    float dpi = ImGui::GetFontSize() / 13.0f, rounding = ImGui::GetStyle().FrameRounding;
    if (e->has_size && pose.shadow > 0.0f) {
        float k = Clamp01(pose.shadow);
        ImU32 c = opt.shadow_color ? opt.shadow_color : IM_COL32(0, 0, 0, 110);
        Shadow(f.dl, mn, mx, rounding, WithAlpha(c, k), 16.0f * k * dpi, ImVec2(0, 4.0f * k * dpi));
    }
    if (e->has_size && pose.glow > 0.0f) {
        float k = Clamp01(pose.glow);
        ImU32 c = opt.glow_color ? opt.glow_color : ImGui::GetColorU32(ImGuiCol_CheckMark);
        Glow(f.dl, mn, mx, rounding, WithAlpha(c, k), 12.0f * k * dpi);
    }
    if (pose.reveal >= 1.0f) return;
    if (!e->has_size) {
        f.hide = true;   // size unknown on the first frame, skip one frame instead of flashing
        return;
    }
    RevealRect(ev.reveal_dir, Sat(pose.reveal), mn, mx);
    ImGui::PushClipRect(mn, mx, true);
    f.clip = true;
}

}  // namespace detail

// Wraps BeginGroup. Returns false when the item is hidden (not visible and its exit motion is done):
// then skip drawing it and don't call EndMotion. Call it every frame, also while not visible.
inline bool BeginMotion(ImGuiID id, const MotionBinding* bindings, int count, const MotionOptions& opt = MotionOptions()) {
    using namespace detail;
    MotionRuntime& r = MotionState();
    MotionTick(r);
    if (!bindings || count < 0) count = 0;
    if (count > kMaxMotionBindings) count = kMaxMotionBindings;
    MotionEntry* e = MotionAcquire(r, id);
    bool step = e->last_frame != r.frame;
    e->last_frame = r.frame;
    float dt = step ? DeltaTime() : 0.0f;
    if (dt < 0.0f) dt = 0.0f;

    UpdatePhase(e, bindings, count, opt.visible, step);
    if (e->pending) {
        for (int i = 0; i < count; i++)
            if (e->pending & (1u << (unsigned)e->slots[i].trigger)) RestartSlot(e->slots[i]);
        e->pending = 0;
    }

    MotionEval ev = Evaluate(e, bindings, count, opt, dt);
    if (e->phase == kPhaseExiting && !ev.exit_running) e->phase = kPhaseHidden;
    e->playing = (ev.playing && e->phase != kPhaseHidden) ? 1 : 0;
    if (e->phase == kPhaseHidden) {
        e->hovered = e->active = e->focused = 0;
        return false;
    }
    e->text_reveal = ev.pose.text_reveal;

    ImGui::BeginGroup();
    if (r.depth >= kMaxMotionDepth) {
        r.overflow++;
        return true;
    }
    MotionFrame& f = r.stack[r.depth++];
    f.id = id;
    f.dl = ImGui::GetWindowDrawList();
    f.vtx_start = f.dl->VtxBuffer.Size;
    f.pose = ev.pose;
    f.origin = ev.origin;
    f.clip = f.hide = false;
    BeginEffects(f, e, ev, opt);
    return true;
}

// Draw commands and clip rects are not transformed (big scales can get clipped by the window), and
// child windows opened inside the scope draw to their own list so they don't move either.
inline void EndMotion() {
    using namespace detail;
    MotionRuntime& r = MotionState();
    if (r.overflow > 0) {
        r.overflow--;
        ImGui::EndGroup();
        return;
    }
    IM_ASSERT(r.depth > 0 && "EndMotion without BeginMotion");
    if (r.depth <= 0) return;
    MotionFrame f = r.stack[--r.depth];
    if (f.clip) ImGui::PopClipRect();
    ImGui::EndGroup();
    ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    if (MotionEntry* e = r.table.Find(f.id)) {
        e->w = mx.x - mn.x;
        e->h = mx.y - mn.y;
        e->has_size = 1;
        e->active = ImGui::IsItemActive() ? 1 : 0;
        e->hovered = ImGui::IsItemHovered() ? 1 : 0;
        e->focused = (ImGui::IsItemFocused() || e->active) ? 1 : 0;
    }
    // the vertex buffer is shared by all channels (tables and columns only split the index lists),
    // so everything written since BeginMotion is ours wherever it went
    ImVec2 o(mn.x + (mx.x - mn.x) * f.origin.x, mn.y + (mx.y - mn.y) * f.origin.y);
    TransformVerts(f.dl, f.vtx_start, f.pose, o, f.hide);
}

// restarts every binding of `id` with trigger t at its next BeginMotion
inline void TriggerMotion(ImGuiID id, Trigger t) {
    detail::MotionRuntime& r = detail::MotionState();
    detail::MotionTick(r);
    detail::MotionAcquire(r, id)->pending |= 1u << (unsigned)t;
}

// composed TextReveal of the innermost open scope with this id, else last frame's value
inline float MotionTextReveal(ImGuiID id) {
    detail::MotionRuntime& r = detail::MotionState();
    for (int i = r.depth - 1; i >= 0; i--)
        if (r.stack[i].id == id) return r.stack[i].pose.text_reveal;
    detail::MotionEntry* e = r.table.Find(id);
    return (e && e->seen) ? e->text_reveal : 1.0f;
}

inline bool IsMotionPlaying(ImGuiID id) {
    detail::MotionEntry* e = detail::MotionState().table.Find(id);
    return e && e->playing;
}

// drops all playback state: everything replays its enter motion next time it is seen
inline void ResetMotions() { detail::MotionState().table.Clear(); }

}  // namespace imb
