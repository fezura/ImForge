#pragma once
// easings, per-id transitions and springs, enter presets, tab crossfade. needs only imgui.h
#include "imgui.h"
#if IMGUI_VERSION_NUM >= 18900
#include "imgui_internal.h"
#endif
#include <cmath>
#include <cstdint>
#include <cstring>

namespace imb {

enum class Ease { Linear, InQuad, OutQuad, InOutQuad, InCubic, OutCubic, InOutCubic, OutBack, InBack, OutElastic, OutBounce, OutExpo };
enum class Preset { None, Fade, SlideUp, SlideDown, SlideLeft, SlideRight, FadeSlideUp, FadeSlideDown, ScalePop, Stagger };

inline float EaseApply(Ease e, float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    const float kPi = 3.14159265358979f;
    switch (e) {
    case Ease::Linear: return t;
    case Ease::InQuad: return t * t;
    case Ease::OutQuad: return 1.0f - (1.0f - t) * (1.0f - t);
    case Ease::InOutQuad: {
        if (t < 0.5f) return 2.0f * t * t;
        float u = -2.0f * t + 2.0f;
        return 1.0f - u * u * 0.5f;
    }
    case Ease::InCubic: return t * t * t;
    case Ease::OutCubic: {
        float u = 1.0f - t;
        return 1.0f - u * u * u;
    }
    case Ease::InOutCubic: {
        if (t < 0.5f) return 4.0f * t * t * t;
        float u = -2.0f * t + 2.0f;
        return 1.0f - u * u * u * 0.5f;
    }
    case Ease::OutBack: {
        const float c1 = 1.70158f, c3 = c1 + 1.0f;
        float u = t - 1.0f;
        return 1.0f + c3 * u * u * u + c1 * u * u;
    }
    case Ease::InBack: {
        const float c1 = 1.70158f, c3 = c1 + 1.0f;
        return c3 * t * t * t - c1 * t * t;
    }
    case Ease::OutElastic: {
        const float c4 = 2.0f * kPi / 3.0f;
        return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
    }
    case Ease::OutBounce: {
        const float n1 = 7.5625f, d1 = 2.75f;
        if (t < 1.0f / d1) return n1 * t * t;
        if (t < 2.0f / d1) { t -= 1.5f / d1; return n1 * t * t + 0.75f; }
        if (t < 2.5f / d1) { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
        t -= 2.625f / d1;
        return n1 * t * t + 0.984375f;
    }
    case Ease::OutExpo: return 1.0f - std::pow(2.0f, -10.0f * t);
    }
    return t;
}

inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline ImVec2 Lerp(const ImVec2& a, const ImVec2& b, float t) {
    return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
}
inline ImVec4 Lerp(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}
inline ImU32 LerpCol(ImU32 a, ImU32 b, float t) {
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    ImU32 out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        int ca = (int)((a >> shift) & 0xFF), cb = (int)((b >> shift) & 0xFF);
        int c = ca + (int)std::floor((float)(cb - ca) * t + 0.5f);
        if (c < 0) c = 0;
        if (c > 255) c = 255;
        out |= (ImU32)c << shift;
    }
    return out;
}

namespace detail {

const int kMaxEnterDepth = 32;
const int kMaxParticleSystems = 32;
const int kMaxParticles = 256;
const double kGcAge = 2.0;
const ImGuiID kSaltAnimate = 0x5bd1e995u;
const ImGuiID kSaltSpring = 0x9e3779b9u;
const ImGuiID kSaltEnter = 0x27d4eb2fu;
const ImGuiID kSaltTab = 0x165667b1u;

struct AnimEntry {
    ImGuiID key;
    int state;   // 0 empty, 1 live, 2 tombstone
    float value, velocity, start, target, t;
    int last_frame;
    double last_time;
};

struct AnimTable {
    ImVector<AnimEntry> slots;
    int live = 0;
    int used = 0;

    static unsigned Hash(ImGuiID k) {
        unsigned h = (unsigned)k;
        h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
        return h;
    }
    AnimEntry* Find(ImGuiID key) {
        if (slots.Size == 0) return nullptr;
        unsigned mask = (unsigned)slots.Size - 1, i = Hash(key) & mask;
        for (;;) {
            AnimEntry& s = slots[(int)i];
            if (s.state == 0) return nullptr;
            if (s.state == 1 && s.key == key) return &s;
            i = (i + 1) & mask;
        }
    }
    AnimEntry* Place(ImGuiID key) {
        unsigned mask = (unsigned)slots.Size - 1, i = Hash(key) & mask;
        for (;;) {
            AnimEntry& s = slots[(int)i];
            if (s.state != 1) {
                if (s.state == 0) used++;
                live++;
                memset(&s, 0, sizeof(s));
                s.key = key;
                s.state = 1;
                return &s;
            }
            i = (i + 1) & mask;
        }
    }
    void Rehash(int cap) {
        ImVector<AnimEntry> old;
        old.swap(slots);
        slots.resize(cap);
        memset(slots.Data, 0, sizeof(AnimEntry) * (size_t)cap);
        live = used = 0;
        for (int i = 0; i < old.Size; i++)
            if (old[i].state == 1) *Place(old[i].key) = old[i];
    }
    AnimEntry* Insert(ImGuiID key) {
        if (slots.Size == 0 || (used + 1) * 4 > slots.Size * 3) {
            int cap = slots.Size < 64 ? 64 : slots.Size;
            while ((live + 1) * 2 > cap) cap *= 2;
            Rehash(cap);
        }
        return Place(key);
    }
    void Sweep(double now) {
        for (int i = 0; i < slots.Size; i++) {
            AnimEntry& s = slots[i];
            if (s.state == 1 && now - s.last_time > kGcAge) { s.state = 2; live--; }
        }
    }
    void Clear() {
        slots.clear();
        live = used = 0;
    }
};

struct Particle { float x, y, vx, vy, scale; };
struct ParticleSystem { ImGuiID key; int count; int last_frame; double last_time; };
struct EnterFrame { float indent; float dy; };

struct Runtime {
    AnimTable anims;
    int frame = -1;
    double now = 0.0;
    double last_sweep = 0.0;
    int dt_frame = -1;
    float dt_override = -1.0f;
    int enter_depth = 0;
    EnterFrame enter[kMaxEnterDepth];
    ParticleSystem psys[kMaxParticleSystems];
    ImVector<Particle> ppool;
};

inline Runtime& State() {
    static Runtime s;
    return s;
}

inline void BeginFrame() {
    Runtime& s = State();
    int f = ImGui::GetFrameCount();
    if (s.frame == f) return;
    s.frame = f;
    s.now = ImGui::GetTime();
    if (s.now - s.last_sweep >= 1.0) {
        s.anims.Sweep(s.now);
        for (int i = 0; i < kMaxParticleSystems; i++)
            if (s.psys[i].key != 0 && s.now - s.psys[i].last_time > kGcAge) s.psys[i].key = 0;
        s.last_sweep = s.now;
    }
}

struct Slot {
    AnimEntry* e;
    bool created;
    bool stepped;   // already advanced this frame (same id used twice)
};

inline Slot Acquire(ImGuiID key) {
    BeginFrame();
    Runtime& s = State();
    Slot r;
    r.e = s.anims.Find(key);
    r.created = (r.e == nullptr);
    if (r.created) r.e = s.anims.Insert(key);
    r.stepped = !r.created && r.e->last_frame == s.frame;
    r.e->last_frame = s.frame;
    r.e->last_time = s.now;
    return r;
}

inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

}  // namespace detail

inline float DeltaTime() {
    detail::Runtime& s = detail::State();
    if (s.dt_override >= 0.0f && s.dt_frame == ImGui::GetFrameCount()) return s.dt_override;
    return ImGui::GetIO().DeltaTime;
}

// optional: call after ImGui::NewFrame() to override delta time (dt < 0 uses io.DeltaTime)
inline void NewFrame(float dt = -1.0f) {
    detail::Runtime& s = detail::State();
    s.dt_frame = ImGui::GetFrameCount();
    s.dt_override = dt;
    detail::BeginFrame();
}

// call this after destroying the ImGui context
inline void Reset() {
    detail::Runtime& s = detail::State();
    s.anims.Clear();
    s.frame = -1;
    s.now = s.last_sweep = 0.0;
    s.dt_frame = -1;
    s.dt_override = -1.0f;
    s.enter_depth = 0;
    memset(s.psys, 0, sizeof(s.psys));
    s.ppool.clear();
}

namespace detail {

// `start` doubles as the stagger delay accumulator
inline float TransitionEx(ImGuiID key, bool on, float duration, Ease ease, float init_t, float delay) {
    Slot s = Acquire(key);
    AnimEntry& e = *s.e;
    float target = on ? 1.0f : 0.0f;
    if (s.created) e.t = init_t;
    if (!s.stepped) {
        if (duration <= 0.0f) {
            e.t = target;
        } else {
            float dt = DeltaTime();
            if (on && delay > 0.0f && e.start < delay) {
                e.start += dt;
            } else {
                float step = dt / duration;
                if (e.t < target) e.t = (e.t + step > target) ? target : e.t + step;
                else if (e.t > target) e.t = (e.t - step < target) ? target : e.t - step;
            }
        }
        if (!on) e.start = 0.0f;
        e.value = EaseApply(ease, e.t);
    }
    return e.value;
}

}  // namespace detail

// eased 0..1. a new id starts at its target, so nothing animates on first sight
inline float Transition(ImGuiID id, bool on, float duration, Ease ease = Ease::OutCubic) {
    return detail::TransitionEx(id, on, duration, ease, on ? 1.0f : 0.0f, 0.0f);
}

inline float AnimateTo(ImGuiID id, float target, float duration, Ease ease = Ease::OutCubic) {
    detail::Slot s = detail::Acquire(id ^ detail::kSaltAnimate);
    detail::AnimEntry& e = *s.e;
    if (s.created) {
        e.value = e.start = e.target = target;
        e.t = 1.0f;
        return target;
    }
    if (s.stepped) return e.value;
    if (e.target != target) {
        e.start = e.value;
        e.target = target;
        e.t = 0.0f;
    }
    if (e.t < 1.0f) {
        if (duration <= 0.0f) e.t = 1.0f;
        else e.t = detail::Clamp01(e.t + DeltaTime() / duration);
        e.value = e.start + (e.target - e.start) * EaseApply(ease, e.t);
    }
    return e.value;
}

inline float Spring(ImGuiID id, float target, float stiffness = 170.f, float damping = 26.f) {
    detail::Slot s = detail::Acquire(id ^ detail::kSaltSpring);
    detail::AnimEntry& e = *s.e;
    if (s.created) {
        e.value = target;
        e.velocity = 0.0f;
        return target;
    }
    if (s.stepped) return e.value;
    float dt = DeltaTime();
    if (dt > 1.0f / 30.0f) dt = 1.0f / 30.0f;
    if (dt < 0.0f) dt = 0.0f;
    int steps = (int)(dt * 120.0f) + 1;
    float h = dt / (float)steps;
    for (int i = 0; i < steps; i++) {
        float a = stiffness * (target - e.value) - damping * e.velocity;
        e.velocity += a * h;
        e.value += e.velocity * h;
    }
    if (std::fabs(target - e.value) < 1e-4f && std::fabs(e.velocity) < 1e-4f) {
        e.value = target;
        e.velocity = 0.0f;
    }
    return e.value;
}

// Returns false when fully hidden (nothing pushed, don't call EndEnter).
// ScalePop is faked with alpha + a small overshoot, imgui can't scale a widget subtree.
inline bool BeginEnter(ImGuiID id, bool visible, float duration, Preset preset, float stagger_delay = 0.f) {
    Ease ease = preset == Preset::ScalePop ? Ease::OutBack : Ease::OutCubic;
    float p = detail::TransitionEx(id ^ detail::kSaltEnter, visible, duration, ease, 0.0f, stagger_delay);
    if (!visible && p <= 0.0f) return false;
    detail::Runtime& s = detail::State();
    IM_ASSERT(s.enter_depth < detail::kMaxEnterDepth);
    detail::EnterFrame& f = s.enter[s.enter_depth++];
    f.indent = f.dy = 0.0f;
    float alpha = 1.0f;
    float dist = (1.0f - p) * 20.0f * ImGui::GetFontSize() / 13.0f;
    switch (preset) {
    case Preset::None: break;
    case Preset::Fade: alpha = p; break;
    case Preset::SlideUp: f.dy = dist; break;
    case Preset::SlideDown: f.dy = -dist; break;
    case Preset::SlideLeft: f.indent = dist; break;
    case Preset::SlideRight: f.indent = -dist; break;
    case Preset::FadeSlideUp:
    case Preset::Stagger: alpha = p; f.dy = dist; break;
    case Preset::FadeSlideDown: alpha = p; f.dy = -dist; break;
    case Preset::ScalePop: alpha = p; f.dy = dist * 0.3f; break;
    }
    f.dy = std::floor(f.dy + 0.5f);   // whole pixels: ImGui truncates item positions, so EndEnter can undo exactly
    f.indent = std::floor(f.indent + 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * detail::Clamp01(alpha));
    if (f.indent != 0.0f) ImGui::Indent(f.indent);
    if (f.dy != 0.0f) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + f.dy);
    return true;
}

inline void EndEnter() {
    detail::Runtime& s = detail::State();
    IM_ASSERT(s.enter_depth > 0);
    detail::EnterFrame& f = s.enter[--s.enter_depth];
    if (f.dy != 0.0f) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - f.dy);
#if IMGUI_VERSION_NUM >= 18900
        // cursor is back where normal flow put it, don't let imgui treat this as a boundary extension
        ImGui::GetCurrentWindow()->DC.IsSetPos = false;
#endif
    }
    if (f.indent != 0.0f) ImGui::Unindent(f.indent);
    ImGui::PopStyleVar();
}

inline void BeginTabCrossfade(ImGuiID id, int current_tab, float duration) {
    detail::Slot s = detail::Acquire(id ^ detail::kSaltTab);
    detail::AnimEntry& e = *s.e;
    if (s.created) {
        e.target = (float)current_tab;
        e.t = 1.0f;
    } else if (!s.stepped) {
        if ((int)e.target != current_tab) {
            e.target = (float)current_tab;
            e.t = 0.0f;
        }
        if (duration <= 0.0f) e.t = 1.0f;
        else e.t = detail::Clamp01(e.t + DeltaTime() / duration);
    }
    e.value = EaseApply(Ease::OutCubic, e.t);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * e.value);
}

inline void EndTabCrossfade() { ImGui::PopStyleVar(); }

}  // namespace imb
