#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "imgui_headless.h"
#include "imforge/imb_motion.h"
#include "imgui_internal.h"
#include "imforge/imforge_anim_names.h"

#include <cfloat>
#include <cmath>
#include <set>
#include <string>

using imb::AnimDef;
using imb::Ease;
using imb::EaseKind;
using imb::EaseSpec;
using imb::Keyframe;
using imb::Pose;
using imb::Prop;
using imb::Track;
using imb::Trigger;

namespace {

const float kDt = 1.0f / 60.0f;

EaseSpec Named(Ease e) {
    EaseSpec s;
    s.named = e;
    return s;
}

EaseSpec Bezier(float a, float b, float c, float d) {
    EaseSpec s;
    s.kind = EaseKind::CubicBezier;
    s.p[0] = a; s.p[1] = b; s.p[2] = c; s.p[3] = d;
    return s;
}

Keyframe Key(float t, float v, EaseSpec e = EaseSpec()) {
    Keyframe k;
    k.t = t;
    k.v[0] = v; k.v[1] = k.v[2] = k.v[3] = 0.0f;
    k.ease = e;
    return k;
}

Keyframe Key4(float t, float r, float g, float b, float a) {
    Keyframe k;
    k.t = t;
    k.v[0] = r; k.v[1] = g; k.v[2] = b; k.v[3] = a;
    k.ease = EaseSpec();
    return k;
}

// one def with a single track, owns its keys
struct OneTrack {
    Keyframe keys[4];
    Track track;
    AnimDef def;
    OneTrack(const char* name, Prop prop, Keyframe a, Keyframe b, float duration = 1.0f) {
        keys[0] = a;
        keys[1] = b;
        track.prop = prop;
        track.keys = keys;
        track.count = 2;
        def.name = name;
        def.category = "test";
        def.duration = duration;
        def.tracks = &track;
        def.track_count = 1;
    }
};

void SetMouse(float x, float y) {
    ImGuiIO& io = ImGui::GetIO();
#if IMGUI_VERSION_NUM >= 18700
    io.AddMousePosEvent(x, y);
#else
    io.MousePos = ImVec2(x, y);
#endif
}

struct Bounds {
    ImVec2 mn = ImVec2(FLT_MAX, FLT_MAX), mx = ImVec2(-FLT_MAX, -FLT_MAX);
    float W() const { return mx.x - mn.x; }
    float H() const { return mx.y - mn.y; }
    float Cx() const { return (mn.x + mx.x) * 0.5f; }
    float Cy() const { return (mn.y + mx.y) * 0.5f; }
};

Bounds VertexBounds(ImDrawList* dl, int start, int end) {
    Bounds b;
    for (int i = start; i < end; i++) {
        const ImVec2& p = dl->VtxBuffer[i].pos;
        b.mn.x = std::fmin(b.mn.x, p.x); b.mn.y = std::fmin(b.mn.y, p.y);
        b.mx.x = std::fmax(b.mx.x, p.x); b.mx.y = std::fmax(b.mx.y, p.y);
    }
    return b;
}

int Channel(ImU32 c, int shift) { return (int)((c >> shift) & 0xFF); }

struct Fixture {
    imforge::test::HeadlessImGui h;
    Fixture() {
        imb::Reset();
        imb::ResetMotions();
        SetMouse(-FLT_MAX, -FLT_MAX);
    }
    ~Fixture() {
        imb::ResetMotions();
        imb::Reset();
    }

    template <class F>
    void Frame(F fn) {
        h.BeginFrame();
        imb::NewFrame(kDt);
        ImGui::SetNextWindowPos(ImVec2(100, 100));
        ImGui::SetNextWindowSize(ImVec2(600, 500));
        ImGui::Begin("motion", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
        fn(ImGui::GetWindowDrawList());
        ImGui::End();
        h.EndFrame();
    }
};

// draws a 120x40 button inside one motion scope, returns its vertex bounds and the layout rect
struct ButtonShot {
    Bounds verts, layout;
    bool shown = false;
    ImU32 first_col = 0;
};

ButtonShot MotionButton(ImDrawList* dl, ImGuiID id, const imb::MotionBinding* b, int n,
                        const imb::MotionOptions& opt = imb::MotionOptions()) {
    ButtonShot s;
    int v0 = dl->VtxBuffer.Size;
    if (!imb::BeginMotion(id, b, n, opt)) return s;
    s.shown = true;
    ImGui::Button("Target", ImVec2(120, 40));
    imb::EndMotion();
    s.layout.mn = ImGui::GetItemRectMin();
    s.layout.mx = ImGui::GetItemRectMax();
    s.verts = VertexBounds(dl, v0, dl->VtxBuffer.Size);
    if (dl->VtxBuffer.Size > v0) s.first_col = dl->VtxBuffer[v0].col;
    return s;
}

imb::MotionBinding Bind(const AnimDef* def, Trigger t) {
    imb::MotionBinding b;
    b.def = def;
    b.trigger = t;
    return b;
}

int g_preset_calls = 0;
AnimDef g_preset_def;

void TestPresets() {
    g_preset_calls++;
    g_preset_def.name = "PresetFromHook";
    g_preset_def.category = "entrance";
    imb::RegisterAnim(&g_preset_def);
}

// same pattern the generated presets header uses
const bool kHooked = (imb::detail::MotionPresetsHook() = &TestPresets, true);

}  // namespace

TEST_CASE("new easings hit their endpoints") {
    for (int i = 0; i < imforge::kEaseCountAll; i++) {
        Ease e = (Ease)i;
        CAPTURE(imforge::EaseName(e));
        CHECK(imb::EaseApply(e, 0.0f) == doctest::Approx(0.0f));
        CHECK(imb::EaseApply(e, 1.0f) == doctest::Approx(1.0f));
        CHECK(imb::EaseApply(e, 0.999f) == doctest::Approx(1.0f).epsilon(0.05));
        CHECK(std::isfinite(imb::EaseApply(e, 0.5f)));
    }
}

TEST_CASE("monotonic easings never go backwards") {
    const Ease mono[] = {Ease::InSine, Ease::OutSine, Ease::InOutSine, Ease::InQuart, Ease::OutQuart,
                         Ease::InOutQuart, Ease::InQuint, Ease::OutQuint, Ease::InOutQuint, Ease::InExpo,
                         Ease::InOutExpo, Ease::InCirc, Ease::OutCirc, Ease::InOutCirc, Ease::InBounce,
                         Ease::InOutBounce};
    for (Ease e : mono) {
        CAPTURE(imforge::EaseName(e));
        float prev = 0.0f;
        int bad = 0;
        for (int i = 1; i <= 400; i++) {
            float v = imb::EaseApply(e, (float)i / 400.0f);
            if (v < prev - 1e-5f && e != Ease::InBounce && e != Ease::InOutBounce) bad++;
            if (v < -1e-5f || v > 1.0f + 1e-5f) bad++;
            prev = v;
        }
        CHECK(bad == 0);
    }
    CHECK(imb::EaseApply(Ease::InOutSine, 0.5f) == doctest::Approx(0.5f));
    CHECK(imb::EaseApply(Ease::InOutQuart, 0.5f) == doctest::Approx(0.5f));
    CHECK(imb::EaseApply(Ease::InOutCirc, 0.5f) == doctest::Approx(0.5f));
    CHECK(imb::EaseApply(Ease::OutSine, 0.5f) == doctest::Approx(0.70710678f));
    CHECK(imb::EaseApply(Ease::InExpo, 0.5f) == doctest::Approx(1.0f / 32.0f));
    float back_min = 1.0f, back_max = 0.0f, el_min = 0.0f;
    for (int i = 0; i <= 400; i++) {
        float t = (float)i / 400.0f;
        back_min = std::fmin(back_min, imb::EaseApply(Ease::InOutBack, t));
        back_max = std::fmax(back_max, imb::EaseApply(Ease::InOutBack, t));
        el_min = std::fmin(el_min, imb::EaseApply(Ease::InElastic, t));
    }
    CHECK(back_min < -0.05f);
    CHECK(back_max > 1.05f);
    CHECK(el_min < -0.05f);
    CHECK(imb::EaseApply(Ease::InBounce, 0.5f) == doctest::Approx(1.0f - 0.765625f));
}

TEST_CASE("every ease name round-trips, old names keep their place") {
    std::set<std::string> seen;
    for (int i = 0; i < imforge::kEaseCountAll; i++) {
        Ease e = (Ease)i;
        std::string name = imforge::EaseName(e);
        CHECK(seen.insert(name).second);
        Ease back = Ease::Linear;
        CHECK(imforge::TryEaseFromName(name.c_str(), &back));
        CHECK(back == e);
    }
    CHECK((int)Ease::OutExpo == 11);
    CHECK((int)Ease::InOutBounce == imforge::kEaseCountAll - 1);
    CHECK(imforge::EaseFromName("InOutElastic") == Ease::InOutElastic);
}

TEST_CASE("cubic-bezier matches CSS") {
    EaseSpec ease = Bezier(0.25f, 0.1f, 0.25f, 1.0f);
    CHECK(imb::SampleEase(ease, 0.5f) == doctest::Approx(0.8024f).epsilon(0.002));
    CHECK(imb::SampleEase(ease, 0.0f) == 0.0f);
    CHECK(imb::SampleEase(ease, 1.0f) == 1.0f);
    CHECK(imb::SampleEase(Bezier(0.42f, 0.0f, 1.0f, 1.0f), 0.5f) == doctest::Approx(0.3153f).epsilon(0.003));
    CHECK(imb::SampleEase(Bezier(0.0f, 0.0f, 0.58f, 1.0f), 0.5f) == doctest::Approx(0.6847f).epsilon(0.003));
    CHECK(imb::SampleEase(Bezier(0.42f, 0.0f, 0.58f, 1.0f), 0.5f) == doctest::Approx(0.5f).epsilon(0.001));
    for (int i = 1; i < 10; i++) {
        float t = (float)i / 10.0f;
        CHECK(imb::SampleEase(Bezier(0.0f, 0.0f, 1.0f, 1.0f), t) == doctest::Approx(t).epsilon(0.001));
    }
    float y = imb::SampleEase(Bezier(0.34f, 1.56f, 0.64f, 1.0f), 0.5f);
    CHECK(y > 1.0f);
    // steep curve where newton alone tends to wander
    float prev = 0.0f;
    int bad = 0;
    for (int i = 1; i <= 200; i++) {
        float v = imb::SampleEase(Bezier(0.9f, 0.0f, 0.1f, 1.0f), (float)i / 200.0f);
        if (v < prev - 1e-4f) bad++;
        prev = v;
    }
    CHECK(bad == 0);
}

TEST_CASE("steps jump at the end of each interval") {
    EaseSpec s;
    s.kind = EaseKind::Steps;
    s.p[0] = 4;
    CHECK(imb::SampleEase(s, 0.1f) == 0.0f);
    CHECK(imb::SampleEase(s, 0.24f) == 0.0f);
    CHECK(imb::SampleEase(s, 0.25f) == doctest::Approx(0.25f));
    CHECK(imb::SampleEase(s, 0.6f) == doctest::Approx(0.5f));
    CHECK(imb::SampleEase(s, 0.99f) == doctest::Approx(0.75f));
    CHECK(imb::SampleEase(s, 1.0f) == 1.0f);
    s.p[0] = 0;
    CHECK(imb::SampleEase(s, 0.5f) == 0.0f);
}

TEST_CASE("spring settles at 1") {
    struct Case { float k, d; bool overshoot; };
    const Case cases[] = {{170, 26, false}, {170, 8, true}, {300, 10, true}, {100, 20, false}, {100, 60, false}, {0, 0, false}};
    for (const Case& c : cases) {
        CAPTURE(c.k);
        CAPTURE(c.d);
        EaseSpec s;
        s.kind = EaseKind::Spring;
        s.p[0] = c.k;
        s.p[1] = c.d;
        CHECK(imb::SampleEase(s, 0.0f) == 0.0f);
        CHECK(imb::SampleEase(s, 1.0f) == 1.0f);
        CHECK(imb::SampleEase(s, 0.999f) == doctest::Approx(1.0f).epsilon(0.01));
        float mx = 0.0f;
        for (int i = 0; i <= 200; i++) mx = std::fmax(mx, imb::SampleEase(s, (float)i / 200.0f));
        if (c.overshoot) CHECK(mx > 1.05f);
        else CHECK(mx < 1.02f);
        for (int i = 180; i <= 200; i++) CHECK(std::fabs(imb::SampleEase(s, (float)i / 200.0f) - 1.0f) < 0.02f);
    }
}

TEST_CASE("SampleAnim follows CSS timing") {
    OneTrack a("Slide", Prop::TranslateX, Key(0, 10), Key(1, 100));
    AnimDef& d = a.def;
    d.delay = 0.5f;

    SUBCASE("fill none") {
        d.fill = imb::Fill::None;
        CHECK(imb::SampleAnim(d, 0.25f).translate.x == 0.0f);
        CHECK(imb::SampleAnim(d, 1.0f).translate.x == doctest::Approx(55.0f));
        CHECK(imb::SampleAnim(d, 2.0f).translate.x == 0.0f);
    }
    SUBCASE("fill forwards") {
        d.fill = imb::Fill::Forwards;
        CHECK(imb::SampleAnim(d, 0.25f).translate.x == 0.0f);
        CHECK(imb::SampleAnim(d, 2.0f).translate.x == doctest::Approx(100.0f));
    }
    SUBCASE("fill backwards") {
        d.fill = imb::Fill::Backwards;
        CHECK(imb::SampleAnim(d, 0.25f).translate.x == doctest::Approx(10.0f));
        CHECK(imb::SampleAnim(d, 2.0f).translate.x == 0.0f);
    }
    SUBCASE("fill both") {
        d.fill = imb::Fill::Both;
        CHECK(imb::SampleAnim(d, 0.0f).translate.x == doctest::Approx(10.0f));
        CHECK(imb::SampleAnim(d, 9.0f).translate.x == doctest::Approx(100.0f));
    }
    SUBCASE("three iterations") {
        d.iterations = 3;
        CHECK(imb::SampleAnim(d, 0.5f + 1.5f).translate.x == doctest::Approx(55.0f));
        CHECK(imb::SampleAnim(d, 0.5f + 2.25f).translate.x == doctest::Approx(32.5f));
        CHECK_FALSE(imb::AnimFinished(d, 3.4f));
        CHECK(imb::AnimFinished(d, 3.5f));
        CHECK(imb::SampleAnim(d, 4.0f).translate.x == doctest::Approx(100.0f));
    }
    SUBCASE("alternate") {
        d.iterations = 3;
        d.direction = imb::Direction::Alternate;
        CHECK(imb::SampleAnim(d, 0.5f + 0.25f).translate.x == doctest::Approx(32.5f));
        CHECK(imb::SampleAnim(d, 0.5f + 1.25f).translate.x == doctest::Approx(77.5f));
        CHECK(imb::SampleAnim(d, 0.5f + 2.25f).translate.x == doctest::Approx(32.5f));
        CHECK(imb::SampleAnim(d, 9.0f).translate.x == doctest::Approx(100.0f));
        d.iterations = 2;
        CHECK(imb::SampleAnim(d, 9.0f).translate.x == doctest::Approx(10.0f));
    }
    SUBCASE("reverse and alternate-reverse") {
        d.direction = imb::Direction::Reverse;
        CHECK(imb::SampleAnim(d, 0.5f + 0.25f).translate.x == doctest::Approx(77.5f));
        CHECK(imb::SampleAnim(d, 0.0f).translate.x == doctest::Approx(100.0f));
        CHECK(imb::SampleAnim(d, 9.0f).translate.x == doctest::Approx(10.0f));
        d.direction = imb::Direction::AlternateReverse;
        d.iterations = 2;
        CHECK(imb::SampleAnim(d, 0.5f + 1.25f).translate.x == doctest::Approx(32.5f));
        CHECK(imb::SampleAnim(d, 9.0f).translate.x == doctest::Approx(100.0f));
    }
    SUBCASE("infinite wraps") {
        d.iterations = 0;
        CHECK(imb::SampleAnim(d, 0.5f + 5.25f).translate.x == doctest::Approx(32.5f));
        CHECK(imb::SampleAnim(d, 0.5f + 1000.5f).translate.x == doctest::Approx(55.0f).epsilon(0.01));
        CHECK_FALSE(imb::AnimFinished(d, 1e6f));
    }
    SUBCASE("duration scale stretches the active part only") {
        CHECK(imb::SampleAnim(d, 0.5f + 1.0f, 2.0f).translate.x == doctest::Approx(55.0f));
        CHECK_FALSE(imb::AnimFinished(d, 2.4f, 2.0f));
        CHECK(imb::AnimFinished(d, 2.5f, 2.0f));
    }
    SUBCASE("zero duration jumps to the end") {
        d.duration = 0.0f;
        CHECK(imb::SampleAnim(d, 0.5f).translate.x == doctest::Approx(100.0f));
        CHECK(imb::AnimFinished(d, 0.5f));
    }
}

TEST_CASE("tracks ease per segment and leave other props alone") {
    Keyframe keys[3] = {Key(0, 0, Named(Ease::InQuad)), Key(0.5f, 1), Key(1, 3)};
    Keyframe tint[2] = {Key4(0, 1, 0, 0, 0), Key4(1, 1, 1, 1, 1)};
    Track tracks[2] = {{Prop::Scale, keys, 3}, {Prop::Tint, tint, 2}};
    AnimDef d;
    d.name = "Seg";
    d.tracks = tracks;
    d.track_count = 2;
    Pose p = imb::SampleAnim(d, 0.25f);
    CHECK(p.scale_x == doctest::Approx(0.25f));
    CHECK(p.scale_y == doctest::Approx(0.25f));
    CHECK(imb::SampleAnim(d, 0.75f).scale_x == doctest::Approx(2.0f));
    CHECK(p.tint.x == doctest::Approx(1.0f));
    CHECK(p.tint.y == doctest::Approx(0.25f));
    CHECK(p.tint.w == doctest::Approx(0.25f));
    CHECK(p.opacity == 1.0f);
    CHECK(p.rotate == 0.0f);
    CHECK(p.translate.x == 0.0f);
    CHECK(p.reveal == 1.0f);
    CHECK(p.glow == 0.0f);
    Keyframe one[1] = {Key(0.3f, 45)};
    Track t1 = {Prop::Rotate, one, 1};
    d.tracks = &t1;
    d.track_count = 1;
    CHECK(imb::SampleAnim(d, 0.9f).rotate == doctest::Approx(45.0f));
}

namespace gen {
using namespace imb;
static const Keyframe kPopScale[] = {
    {0.0f, {0.3f, 0, 0, 0}, {EaseKind::Named, Ease::OutBack, {0, 0, 0, 0}}},
    {1.0f, {1.0f, 0, 0, 0}, {EaseKind::Named, Ease::Linear, {0, 0, 0, 0}}},
};
static const Keyframe kPopTint[] = {
    {0.0f, {1, 1, 1, 0}, {EaseKind::CubicBezier, Ease::Linear, {0.25f, 0.1f, 0.25f, 1.0f}}},
    {1.0f, {1, 1, 1, 1}, {EaseKind::Named, Ease::Linear, {0, 0, 0, 0}}},
};
static const Track kPopTracks[] = {{Prop::Scale, kPopScale, 2}, {Prop::Tint, kPopTint, 2}};
static const AnimDef kPop = {"Pop", "entrance", 0.5f, 0.0f, 1, Direction::Normal, Fill::Both, ImVec2(0.5f, 0.5f),
                             RevealDir::Left, Trigger::Enter, kPopTracks, 2};
}  // namespace gen

TEST_CASE("static tables in the generated style work") {
    Pose p = imb::SampleAnim(gen::kPop, 0.25f);
    CHECK(p.scale_x == doctest::Approx(0.3f + 0.7f * imb::EaseApply(Ease::OutBack, 0.5f)));
    CHECK(p.tint.w == doctest::Approx(0.8024f).epsilon(0.002));
    CHECK(imb::AnimFinished(gen::kPop, 0.5f));
    CHECK(imb::SampleAnim(gen::kPop, 5.0f).scale_x == doctest::Approx(1.0f));
}

TEST_CASE("ComposePose combines each channel its own way") {
    Pose a, b;
    a.opacity = 0.5f; b.opacity = 0.5f;
    a.translate = ImVec2(3, 4); b.translate = ImVec2(1, -1);
    a.scale_x = 2; b.scale_x = 1.5f; b.scale_y = 0.5f;
    a.rotate = 10; b.rotate = 5; a.skew_x = 3; b.skew_y = 2;
    a.tint = ImVec4(1, 0.5f, 1, 1); b.tint = ImVec4(0.5f, 1, 1, 0.5f);
    a.reveal = 0.7f; b.reveal = 0.4f; a.text_reveal = 0.2f; b.text_reveal = 0.9f;
    a.glow = 0.3f; b.glow = 0.6f; a.shadow = 0.8f; b.shadow = 0.1f;
    imb::ComposePose(a, b);
    CHECK(a.opacity == doctest::Approx(0.25f));
    CHECK(a.translate.x == doctest::Approx(4.0f));
    CHECK(a.translate.y == doctest::Approx(3.0f));
    CHECK(a.scale_x == doctest::Approx(3.0f));
    CHECK(a.scale_y == doctest::Approx(0.5f));
    CHECK(a.rotate == doctest::Approx(15.0f));
    CHECK(a.skew_x == doctest::Approx(3.0f));
    CHECK(a.skew_y == doctest::Approx(2.0f));
    CHECK(a.tint.x == doctest::Approx(0.5f));
    CHECK(a.tint.y == doctest::Approx(0.5f));
    CHECK(a.tint.w == doctest::Approx(0.5f));
    CHECK(a.reveal == doctest::Approx(0.4f));
    CHECK(a.text_reveal == doctest::Approx(0.2f));
    CHECK(a.glow == doctest::Approx(0.6f));
    CHECK(a.shadow == doctest::Approx(0.8f));
}

TEST_CASE("registry replaces by name and keeps order") {
    CHECK(kHooked);
    REQUIRE(imb::FindAnim("PresetFromHook") == &g_preset_def);
    CHECK(g_preset_calls == 1);
    int base = imb::AnimCount();
    AnimDef a, b, a2;
    a.name = "RegA";
    b.name = "RegB";
    a2.name = "RegA";
    imb::RegisterAnim(&a);
    imb::RegisterAnim(&b);
    CHECK(imb::AnimCount() == base + 2);
    CHECK(imb::FindAnim("RegA") == &a);
    CHECK(imb::AnimAt(base) == &a);
    CHECK(imb::AnimAt(base + 1) == &b);
    imb::RegisterAnim(&a2);
    CHECK(imb::AnimCount() == base + 2);
    CHECK(imb::FindAnim("RegA") == &a2);
    CHECK(imb::AnimAt(base) == &a2);
    CHECK(imb::FindAnim("rega") == nullptr);
    CHECK(imb::FindAnim("Nope") == nullptr);
    CHECK(imb::FindAnim(nullptr) == nullptr);
    CHECK(imb::AnimAt(-1) == nullptr);
    CHECK(imb::AnimAt(imb::AnimCount()) == nullptr);
    imb::RegisterAnim(nullptr);
    CHECK(imb::UnregisterAnim("RegA"));
    CHECK_FALSE(imb::UnregisterAnim("RegA"));
    CHECK(imb::FindAnim("RegA") == nullptr);
    CHECK(imb::FindAnim("RegB") == &b);
    CHECK(imb::AnimAt(base) == &b);
    CHECK(imb::UnregisterAnim("RegB"));
    CHECK(imb::AnimCount() == base);
    CHECK(g_preset_calls == 1);
}

TEST_CASE("BeginMotion scales, rotates and recolors the item's vertices") {
    Fixture fx;
    OneTrack scale("Scale2", Prop::Scale, Key(0, 2), Key(1, 2));
    OneTrack rot("Rot90", Prop::Rotate, Key(0, 90), Key(1, 90));
    OneTrack fade("Half", Prop::Opacity, Key(0, 0.5f), Key(1, 0.5f));
    OneTrack tint("Tint", Prop::Tint, Key4(0, 1, 0.5f, 0, 1), Key4(1, 1, 0.5f, 0, 1));
    OneTrack move("Move", Prop::TranslateX, Key(0, 30), Key(1, 30));
    imb::MotionBinding none = Bind(nullptr, Trigger::Enter);
    imb::MotionBinding bs = Bind(&scale.def, Trigger::Loop), br = Bind(&rot.def, Trigger::Loop);
    imb::MotionBinding bf = Bind(&fade.def, Trigger::Loop), bt = Bind(&tint.def, Trigger::Loop);
    imb::MotionBinding bm = Bind(&move.def, Trigger::Loop);

    ButtonShot plain, s, r, f, t, m;
    fx.Frame([&](ImDrawList* dl) {
        plain = MotionButton(dl, 1, &none, 1);
        s = MotionButton(dl, 2, &bs, 1);
        r = MotionButton(dl, 3, &br, 1);
        f = MotionButton(dl, 4, &bf, 1);
        t = MotionButton(dl, 5, &bt, 1);
        m = MotionButton(dl, 6, &bm, 1);
    });
    REQUIRE(plain.shown);
    CHECK(plain.verts.W() == doctest::Approx(120.0f));
    CHECK(plain.verts.H() == doctest::Approx(40.0f));
    CHECK(plain.layout.W() == doctest::Approx(120.0f));

    CHECK(s.verts.W() == doctest::Approx(240.0f));
    CHECK(s.verts.H() == doctest::Approx(80.0f));
    CHECK(s.verts.Cx() == doctest::Approx(s.layout.Cx()));
    CHECK(s.verts.Cy() == doctest::Approx(s.layout.Cy()));
    CHECK(s.layout.W() == doctest::Approx(120.0f));

    CHECK(r.verts.W() == doctest::Approx(40.0f).epsilon(0.01));
    CHECK(r.verts.H() == doctest::Approx(120.0f).epsilon(0.01));
    CHECK(r.verts.Cx() == doctest::Approx(r.layout.Cx()));

    CHECK(m.verts.mn.x == doctest::Approx(m.layout.mn.x + 30.0f));
    CHECK(m.verts.W() == doctest::Approx(120.0f));

    int a0 = Channel(plain.first_col, IM_COL32_A_SHIFT);
    CHECK(std::abs(Channel(f.first_col, IM_COL32_A_SHIFT) - a0 / 2) <= 1);
    CHECK(Channel(f.first_col, IM_COL32_R_SHIFT) == Channel(plain.first_col, IM_COL32_R_SHIFT));
    CHECK(Channel(t.first_col, IM_COL32_R_SHIFT) == Channel(plain.first_col, IM_COL32_R_SHIFT));
    CHECK(std::abs(Channel(t.first_col, IM_COL32_G_SHIFT) - Channel(plain.first_col, IM_COL32_G_SHIFT) / 2) <= 1);
    CHECK(Channel(t.first_col, IM_COL32_B_SHIFT) == 0);
    CHECK(Channel(t.first_col, IM_COL32_A_SHIFT) == a0);
}

TEST_CASE("translate follows the font scale") {
    Fixture fx;
    OneTrack move("Move", Prop::TranslateY, Key(0, 10), Key(1, 10));
    imb::MotionBinding b = Bind(&move.def, Trigger::Loop);
    ButtonShot s;
    float font = 0.0f;
    fx.Frame([&](ImDrawList* dl) {
        ImGui::SetWindowFontScale(2.0f);
        font = ImGui::GetFontSize();
        s = MotionButton(dl, 1, &b, 1);
    });
    CHECK(font > 20.0f);
    CHECK(s.verts.mn.y - s.layout.mn.y == doctest::Approx(10.0f * font / 13.0f).epsilon(0.02));
}

TEST_CASE("enter plays once from first sight") {
    Fixture fx;
    OneTrack fade("FadeIn", Prop::Opacity, Key(0, 0), Key(1, 1), 0.5f);
    imb::MotionBinding b = Bind(&fade.def, Trigger::Enter);
    ButtonShot first, mid, done;
    fx.Frame([&](ImDrawList* dl) { first = MotionButton(dl, 7, &b, 1); });
    CHECK(imb::IsMotionPlaying(7));
    for (int i = 0; i < 13; i++) fx.Frame([&](ImDrawList* dl) { mid = MotionButton(dl, 7, &b, 1); });
    for (int i = 0; i < 30; i++) fx.Frame([&](ImDrawList* dl) { done = MotionButton(dl, 7, &b, 1); });
    CHECK_FALSE(imb::IsMotionPlaying(7));
    int a_first = Channel(first.first_col, IM_COL32_A_SHIFT), a_mid = Channel(mid.first_col, IM_COL32_A_SHIFT);
    int a_done = Channel(done.first_col, IM_COL32_A_SHIFT);
    CHECK(a_first < a_mid);
    CHECK(a_mid < a_done);
    CHECK(a_done == Channel(ImGui::GetColorU32(ImGuiCol_Button), IM_COL32_A_SHIFT));

    imb::ResetMotions();
    ButtonShot again;
    fx.Frame([&](ImDrawList* dl) { again = MotionButton(dl, 7, &b, 1); });
    CHECK(Channel(again.first_col, IM_COL32_A_SHIFT) < a_mid);
}

TEST_CASE("stagger delays later items") {
    Fixture fx;
    OneTrack fade("FadeIn", Prop::Opacity, Key(0, 0), Key(1, 1), 0.2f);
    imb::MotionBinding b = Bind(&fade.def, Trigger::Enter);
    b.stagger = 0.1f;
    ButtonShot s0, s3;
    for (int i = 0; i < 12; i++) {
        fx.Frame([&](ImDrawList* dl) {
            imb::MotionOptions o;
            s0 = MotionButton(dl, 10, &b, 1, o);
            o.stagger_index = 3;
            s3 = MotionButton(dl, 13, &b, 1, o);
        });
    }
    CHECK(Channel(s0.first_col, IM_COL32_A_SHIFT) > 0);
    CHECK(Channel(s3.first_col, IM_COL32_A_SHIFT) == 0);
}

TEST_CASE("exit keeps the item alive until it finishes") {
    Fixture fx;
    OneTrack out("FadeOut", Prop::Opacity, Key(0, 1), Key(1, 0), 0.5f);
    imb::MotionBinding b = Bind(&out.def, Trigger::Exit);
    imb::MotionOptions vis, hid;
    hid.visible = false;
    bool shown = false;
    for (int i = 0; i < 3; i++) fx.Frame([&](ImDrawList* dl) { shown = MotionButton(dl, 20, &b, 1, vis).shown; });
    CHECK(shown);
    int frames_true = 0;
    for (int i = 0; i < 60; i++) {
        fx.Frame([&](ImDrawList* dl) {
            if (MotionButton(dl, 20, &b, 1, hid).shown) frames_true++;
        });
        if (i == 5) CHECK(imb::IsMotionPlaying(20));
    }
    CHECK(frames_true >= 28);
    CHECK(frames_true <= 31);
    CHECK_FALSE(imb::IsMotionPlaying(20));
    fx.Frame([&](ImDrawList* dl) { shown = MotionButton(dl, 20, &b, 1, hid).shown; });
    CHECK_FALSE(shown);
    fx.Frame([&](ImDrawList* dl) { shown = MotionButton(dl, 20, &b, 1, vis).shown; });
    CHECK(shown);

    imb::MotionBinding enter_only = Bind(&out.def, Trigger::Enter);
    fx.Frame([&](ImDrawList* dl) { shown = MotionButton(dl, 21, &enter_only, 1, vis).shown; });
    CHECK(shown);
    fx.Frame([&](ImDrawList* dl) { shown = MotionButton(dl, 21, &enter_only, 1, hid).shown; });
    CHECK_FALSE(shown);
    fx.Frame([&](ImDrawList* dl) { shown = MotionButton(dl, 22, &b, 1, hid).shown; });
    CHECK_FALSE(shown);
}

TEST_CASE("hover plays forward while hovered and back after") {
    Fixture fx;
    OneTrack lift("Lift", Prop::TranslateY, Key(0, 0), Key(1, -10), 0.2f);
    lift.def.fill = imb::Fill::None;
    imb::MotionBinding b = Bind(&lift.def, Trigger::Hover);
    ButtonShot s;
    auto frame = [&] { fx.Frame([&](ImDrawList* dl) { s = MotionButton(dl, 30, &b, 1); }); };
    frame();
    frame();
    REQUIRE(s.shown);
    CHECK(s.verts.mn.y == doctest::Approx(s.layout.mn.y));
    ImVec2 c(s.layout.Cx(), s.layout.Cy());
    SetMouse(c.x, c.y);
    float prev = 0.0f;
    int forward = 0;
    for (int i = 0; i < 6; i++) {
        frame();
        float off = s.verts.mn.y - s.layout.mn.y;
        if (off < prev - 0.01f) forward++;
        prev = off;
    }
    CHECK(forward >= 4);
    for (int i = 0; i < 20; i++) frame();
    CHECK(s.verts.mn.y - s.layout.mn.y == doctest::Approx(-10.0f));
    CHECK_FALSE(imb::IsMotionPlaying(30));
    SetMouse(-FLT_MAX, -FLT_MAX);
    frame();
    frame();
    float back = s.verts.mn.y - s.layout.mn.y;
    CHECK(back > -10.0f);
    CHECK(back < -0.5f);
    CHECK(imb::IsMotionPlaying(30));
    for (int i = 0; i < 20; i++) frame();
    CHECK(s.verts.mn.y - s.layout.mn.y == doctest::Approx(0.0f));

    imb::MotionOptions forced;
    forced.force_hover = true;
    for (int i = 0; i < 20; i++) fx.Frame([&](ImDrawList* dl) { s = MotionButton(dl, 30, &b, 1, forced); });
    CHECK(s.verts.mn.y - s.layout.mn.y == doctest::Approx(-10.0f));
}

TEST_CASE("press follows the active state") {
    Fixture fx;
    OneTrack squish("Squish", Prop::Scale, Key(0, 1), Key(1, 0.5f), 0.1f);
    imb::MotionBinding b = Bind(&squish.def, Trigger::Press);
    ButtonShot s;
    auto frame = [&] { fx.Frame([&](ImDrawList* dl) { s = MotionButton(dl, 31, &b, 1); }); };
    frame();
    SetMouse(s.layout.Cx(), s.layout.Cy());
    frame();
#if IMGUI_VERSION_NUM >= 18700
    ImGui::GetIO().AddMouseButtonEvent(0, true);
#else
    ImGui::GetIO().MouseDown[0] = true;
#endif
    for (int i = 0; i < 12; i++) frame();
    CHECK(s.verts.W() == doctest::Approx(60.0f));
#if IMGUI_VERSION_NUM >= 18700
    ImGui::GetIO().AddMouseButtonEvent(0, false);
#else
    ImGui::GetIO().MouseDown[0] = false;
#endif
    for (int i = 0; i < 12; i++) frame();
    CHECK(s.verts.W() == doctest::Approx(120.0f));
    imb::MotionOptions forced;
    forced.force_press = true;
    for (int i = 0; i < 12; i++) fx.Frame([&](ImDrawList* dl) { s = MotionButton(dl, 31, &b, 1, forced); });
    CHECK(s.verts.W() == doctest::Approx(60.0f));
}

TEST_CASE("attention waits for TriggerMotion") {
    Fixture fx;
    OneTrack shake("Shake", Prop::TranslateX, Key(0, 20), Key(1, 20), 0.25f);
    shake.def.fill = imb::Fill::None;
    imb::MotionBinding b = Bind(&shake.def, Trigger::Attention);
    ButtonShot s;
    auto frame = [&] { fx.Frame([&](ImDrawList* dl) { s = MotionButton(dl, 40, &b, 1); }); };
    for (int i = 0; i < 5; i++) frame();
    CHECK(s.verts.mn.x == doctest::Approx(s.layout.mn.x));
    CHECK_FALSE(imb::IsMotionPlaying(40));
    imb::TriggerMotion(40, Trigger::Attention);
    frame();
    CHECK(s.verts.mn.x == doctest::Approx(s.layout.mn.x + 20.0f));
    CHECK(imb::IsMotionPlaying(40));
    for (int i = 0; i < 20; i++) frame();
    CHECK(s.verts.mn.x == doctest::Approx(s.layout.mn.x));
    CHECK_FALSE(imb::IsMotionPlaying(40));
    imb::TriggerMotion(40, Trigger::Change);
    frame();
    CHECK(s.verts.mn.x == doctest::Approx(s.layout.mn.x));
}

TEST_CASE("loops keep playing") {
    Fixture fx;
    OneTrack spin("Spin", Prop::Rotate, Key(0, 0), Key(1, 360), 0.5f);
    spin.def.iterations = 1;
    imb::MotionBinding b = Bind(&spin.def, Trigger::Loop);
    for (int i = 0; i < 200; i++) fx.Frame([&](ImDrawList* dl) { MotionButton(dl, 50, &b, 1); });
    CHECK(imb::IsMotionPlaying(50));
}

TEST_CASE("text reveal is readable inside the scope") {
    Fixture fx;
    OneTrack type("Type", Prop::TextReveal, Key(0, 0), Key(1, 1), 1.0f);
    imb::MotionBinding b = Bind(&type.def, Trigger::Enter);
    float inside = -1.0f;
    for (int i = 0; i < 30; i++) {
        fx.Frame([&](ImDrawList*) {
            if (imb::BeginMotion(60, &b, 1)) {
                inside = imb::MotionTextReveal(60);
                ImGui::TextUnformatted("typing");
                imb::EndMotion();
            }
        });
    }
    CHECK(inside == doctest::Approx(0.5f).epsilon(0.05));
    CHECK(imb::MotionTextReveal(60) == doctest::Approx(inside));
    CHECK(imb::MotionTextReveal(12345) == 1.0f);
}

TEST_CASE("reveal clips from last frame's bounds") {
    Fixture fx;
    OneTrack wipe("Wipe", Prop::Reveal, Key(0, 0.5f), Key(1, 0.5f));
    imb::MotionBinding b = Bind(&wipe.def, Trigger::Loop);
    struct Dir { imb::RevealDir d; int edge; };   // edge: 0 left, 1 right, 2 top, 3 bottom, 4 center
    const Dir dirs[] = {{imb::RevealDir::Left, 0}, {imb::RevealDir::Right, 1}, {imb::RevealDir::Up, 2},
                        {imb::RevealDir::Down, 3}, {imb::RevealDir::Center, 4}};
    for (const Dir& d : dirs) {
        CAPTURE((int)d.d);
        wipe.def.reveal_dir = d.d;
        imb::ResetMotions();
        ButtonShot s;
        ImVec4 clip(0, 0, 0, 0);
        int alpha0 = -1;
        for (int i = 0; i < 2; i++) {
            fx.Frame([&](ImDrawList* dl) {
                int v0 = dl->VtxBuffer.Size;
                REQUIRE(imb::BeginMotion(70, &b, 1));
                ImGui::Button("Target", ImVec2(120, 40));
                ImVec2 c0 = dl->GetClipRectMin(), c1 = dl->GetClipRectMax();
                clip = ImVec4(c0.x, c0.y, c1.x, c1.y);
                imb::EndMotion();
                s.layout.mn = ImGui::GetItemRectMin();
                s.layout.mx = ImGui::GetItemRectMax();
                if (i == 0) alpha0 = Channel(dl->VtxBuffer[v0].col, IM_COL32_A_SHIFT);
            });
        }
        CHECK(alpha0 == 0);
        float cw = clip.z - clip.x, ch = clip.w - clip.y;
        const Bounds& L = s.layout;
        if (d.edge <= 1) {
            CHECK(cw == doctest::Approx(60.0f));
            CHECK((d.edge == 0 ? clip.x : clip.z) == doctest::Approx(d.edge == 0 ? L.mn.x : L.mx.x));
        } else if (d.edge <= 3) {
            CHECK(ch == doctest::Approx(20.0f));
            CHECK((d.edge == 2 ? clip.y : clip.w) == doctest::Approx(d.edge == 2 ? L.mn.y : L.mx.y));
        } else {
            CHECK(cw == doctest::Approx(60.0f));
            CHECK(ch == doctest::Approx(20.0f));
            CHECK(clip.x == doctest::Approx(L.mn.x + 30.0f));
        }
    }
}

TEST_CASE("glow and shadow draw behind once the size is known") {
    Fixture fx;
    OneTrack glow("Glow", Prop::Glow, Key(0, 1), Key(1, 1));
    OneTrack shadow("Shadow", Prop::Shadow, Key(0, 1), Key(1, 1));
    imb::MotionBinding none = Bind(nullptr, Trigger::Loop);
    imb::MotionBinding bg = Bind(&glow.def, Trigger::Loop), bs = Bind(&shadow.def, Trigger::Loop);
    int n_plain = 0, n_glow = 0, n_shadow = 0;
    for (int i = 0; i < 2; i++) {
        fx.Frame([&](ImDrawList* dl) {
            int v = dl->VtxBuffer.Size;
            MotionButton(dl, 80, &none, 1);
            n_plain = dl->VtxBuffer.Size - v;
            v = dl->VtxBuffer.Size;
            MotionButton(dl, 81, &bg, 1);
            n_glow = dl->VtxBuffer.Size - v;
            v = dl->VtxBuffer.Size;
            MotionButton(dl, 82, &bs, 1);
            n_shadow = dl->VtxBuffer.Size - v;
        });
    }
    CHECK(n_glow > n_plain);
    CHECK(n_shadow > n_plain);
}

TEST_CASE("nested scopes compose and stay balanced past the depth limit") {
    Fixture fx;
    OneTrack scale("Scale2", Prop::Scale, Key(0, 2), Key(1, 2));
    imb::MotionBinding b = Bind(&scale.def, Trigger::Loop);
    imb::MotionBinding none = Bind(nullptr, Trigger::Loop);
    Bounds inner;
    fx.Frame([&](ImDrawList* dl) {
        int v0 = dl->VtxBuffer.Size;
        REQUIRE(imb::BeginMotion(90, &b, 1));
        REQUIRE(imb::BeginMotion(91, &b, 1));
        ImGui::Button("Nested", ImVec2(50, 20));
        imb::EndMotion();
        imb::EndMotion();
        inner = VertexBounds(dl, v0, dl->VtxBuffer.Size);
    });
    CHECK(inner.W() == doctest::Approx(200.0f));
    CHECK(inner.H() == doctest::Approx(80.0f));

    for (int f = 0; f < 3; f++) {
        fx.Frame([&](ImDrawList*) {
            for (int i = 0; i < 40; i++) CHECK(imb::BeginMotion(100 + (ImGuiID)i, &none, 1));
            ImGui::Button("Deep");
            for (int i = 0; i < 40; i++) imb::EndMotion();
        });
    }
    CHECK(imb::detail::MotionState().depth == 0);
    CHECK(imb::detail::MotionState().overflow == 0);
}

TEST_CASE("same id twice in a frame advances time once") {
    Fixture fx;
    OneTrack fade("FadeIn", Prop::Opacity, Key(0, 0), Key(1, 1), 1.0f);
    imb::MotionBinding b = Bind(&fade.def, Trigger::Enter);
    for (int i = 0; i < 30; i++) {
        fx.Frame([&](ImDrawList* dl) {
            MotionButton(dl, 110, &b, 1);
            MotionButton(dl, 110, &b, 1);
        });
    }
    CHECK(imb::detail::MotionState().table.Find(110)->slots[0].t == doctest::Approx(30.0f / 60.0f).epsilon(0.02));
}

TEST_CASE("idle entries are recycled") {
    Fixture fx;
    imb::MotionBinding none = Bind(nullptr, Trigger::Loop);
    fx.Frame([&](ImDrawList* dl) { MotionButton(dl, 120, &none, 1); });
    CHECK(imb::detail::MotionState().table.Find(120) != nullptr);
    for (int i = 0; i < 60 * 4; i++) fx.Frame([&](ImDrawList* dl) { MotionButton(dl, 121, &none, 1); });
    CHECK(imb::detail::MotionState().table.Find(121) != nullptr);
    CHECK(imb::detail::MotionState().table.Find(120) == nullptr);
}

namespace {

bool AllFinite(ImDrawList* dl, int start, int end) {
    for (int i = start; i < end; i++) {
        const ImVec2& p = dl->VtxBuffer[i].pos;
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) return false;
    }
    return true;
}

struct BlockShot {
    Bounds verts, layout;
};

// kind 0 table, 1 old columns, 2 user channel split; three frames so column widths settle
BlockShot RunBlock(Fixture& fx, const imb::MotionBinding* b, int kind) {
    BlockShot out;
    for (int f = 0; f < 3; f++) {
        fx.Frame([&](ImDrawList* dl) {
            int v0 = dl->VtxBuffer.Size;
            REQUIRE(imb::BeginMotion(300, b, 1));
            if (kind == 0) {
                if (ImGui::BeginTable("tbl", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("a", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                    ImGui::TableSetupColumn("b", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                    for (int r = 0; r < 2; r++) {
                        ImGui::TableNextRow();
                        for (int c = 0; c < 2; c++) {
                            ImGui::TableSetColumnIndex(c);
                            ImGui::PushID(r * 2 + c);
                            ImGui::Button("cell", ImVec2(60, 24));
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndTable();
                }
            } else if (kind == 1) {
                ImGui::Columns(2, "cols", true);
                for (int i = 0; i < 4; i++) {
                    ImGui::PushID(i);
                    ImGui::Button("col", ImVec2(60, 24));
                    ImGui::PopID();
                    ImGui::NextColumn();
                }
                ImGui::Columns(1);
            } else {
                ImVec2 p = ImGui::GetCursorScreenPos();
                dl->ChannelsSplit(2);
                dl->ChannelsSetCurrent(1);
                ImGui::Button("front", ImVec2(100, 30));
                dl->ChannelsSetCurrent(0);
                dl->AddRectFilled(ImVec2(p.x - 4, p.y - 4), ImVec2(p.x + 104, p.y + 34), IM_COL32(255, 0, 0, 255));
                dl->ChannelsMerge();
            }
            imb::EndMotion();
            out.layout.mn = ImGui::GetItemRectMin();
            out.layout.mx = ImGui::GetItemRectMax();
            out.verts = VertexBounds(dl, v0, dl->VtxBuffer.Size);
        });
    }
    return out;
}

}  // namespace

TEST_CASE("scopes around tables, columns and channel splits transform every vertex once") {
    OneTrack scale("Scale2", Prop::Scale, Key(0, 2), Key(1, 2));
    imb::MotionBinding none = Bind(nullptr, Trigger::Loop), b = Bind(&scale.def, Trigger::Loop);
    for (int kind = 0; kind < 3; kind++) {
        Fixture fx;
        BlockShot plain = RunBlock(fx, &none, kind);
        imb::ResetMotions();
        BlockShot big = RunBlock(fx, &b, kind);
        CAPTURE(kind);
        REQUIRE(plain.verts.W() > 50.0f);
        CHECK(big.verts.W() == doctest::Approx(plain.verts.W() * 2.0f).epsilon(0.01));
        CHECK(big.verts.H() == doctest::Approx(plain.verts.H() * 2.0f).epsilon(0.01));
        float cx = (big.layout.mn.x + big.layout.mx.x) * 0.5f, cy = (big.layout.mn.y + big.layout.mx.y) * 0.5f;
        CHECK(big.verts.mn.x == doctest::Approx(cx + 2.0f * (plain.verts.mn.x - cx)).epsilon(0.01));
        CHECK(big.verts.mn.y == doctest::Approx(cy + 2.0f * (plain.verts.mn.y - cy)).epsilon(0.01));
    }
}

TEST_CASE("a scope in one table cell leaves the other cells alone") {
    Fixture fx;
    OneTrack move("Move", Prop::TranslateX, Key(0, 40), Key(1, 40));
    imb::MotionBinding b = Bind(&move.def, Trigger::Loop);
    float x_motion = 0, x_plain = 1;
    for (int f = 0; f < 3; f++) {
        fx.Frame([&](ImDrawList* dl) {
            if (!ImGui::BeginTable("tbl2", 2, 0)) return;
            ImGui::TableSetupColumn("a", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("b", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(1);
            ButtonShot s = MotionButton(dl, 310, &b, 1);
            x_motion = s.verts.mn.x - s.layout.mn.x;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(7);
            int v1 = dl->VtxBuffer.Size;
            ImGui::Button("plain", ImVec2(50, 20));
            x_plain = VertexBounds(dl, v1, dl->VtxBuffer.Size).mn.x - ImGui::GetItemRectMin().x;
            ImGui::PopID();
            ImGui::EndTable();
        });
    }
    CHECK(x_motion == doctest::Approx(40.0f));
    CHECK(std::fabs(x_plain) < 0.01f);
}

TEST_CASE("vertices written after the buffer grew are still transformed") {
    Fixture fx;
    OneTrack scale("Scale2", Prop::Scale, Key(0, 2), Key(1, 2));
    imb::MotionBinding b = Bind(&scale.def, Trigger::Loop);
    Bounds got;
    int cap_before = 0, cap_after = 0;
    fx.Frame([&](ImDrawList* dl) {
        int v0 = dl->VtxBuffer.Size;
        REQUIRE(imb::BeginMotion(320, &b, 1));
        cap_before = dl->VtxBuffer.Capacity;
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(100, 100));
        for (int i = 0; i < 6000; i++) dl->AddRectFilled(p, ImVec2(p.x + 100, p.y + 100), IM_COL32(255, 255, 255, 255));
        imb::EndMotion();
        cap_after = dl->VtxBuffer.Capacity;
        got = VertexBounds(dl, v0, dl->VtxBuffer.Size);
    });
    CHECK(cap_after > cap_before);
    CHECK(got.W() == doctest::Approx(200.0f));
    CHECK(got.H() == doctest::Approx(200.0f));
}

TEST_CASE("inner transform applies first, the outer one after") {
    Fixture fx;
    OneTrack scale("Scale2", Prop::Scale, Key(0, 2), Key(1, 2));
    OneTrack move("Move30", Prop::TranslateX, Key(0, 30), Key(1, 30));
    imb::MotionBinding bs = Bind(&scale.def, Trigger::Loop), bm = Bind(&move.def, Trigger::Loop);
    Bounds v, layout;
    fx.Frame([&](ImDrawList* dl) {
        int v0 = dl->VtxBuffer.Size;
        REQUIRE(imb::BeginMotion(330, &bs, 1));
        REQUIRE(imb::BeginMotion(331, &bm, 1));
        ImGui::Button("Nest", ImVec2(120, 40));
        imb::EndMotion();
        imb::EndMotion();
        layout.mn = ImGui::GetItemRectMin();
        layout.mx = ImGui::GetItemRectMax();
        v = VertexBounds(dl, v0, dl->VtxBuffer.Size);
    });
    CHECK(v.W() == doctest::Approx(240.0f));
    CHECK(v.mn.x == doctest::Approx(layout.mn.x));
}

TEST_CASE("clip rects and groups stay balanced on every path") {
    Fixture fx;
    OneTrack reveal("Reveal", Prop::Reveal, Key(0, 0.5f), Key(1, 0.5f));
    OneTrack gone("Gone", Prop::Opacity, Key(0, 0), Key(1, 0));
    imb::MotionBinding br = Bind(&reveal.def, Trigger::Loop);
    imb::MotionBinding exit_b = Bind(&gone.def, Trigger::Exit);
    exit_b.duration_scale = 0.01f;
    for (int f = 0; f < 6; f++) {
        fx.Frame([&](ImDrawList* dl) {
            ImGuiContext& g = *ImGui::GetCurrentContext();
            int clips = dl->_ClipRectStack.Size, groups = g.GroupStack.Size;
            imb::MotionOptions leaving;
            leaving.visible = f < 2;
            if (imb::BeginMotion(340, &br, 1)) {
                ImGui::Button("Clipped", ImVec2(80, 20));
                imb::EndMotion();
            }
            CHECK(dl->_ClipRectStack.Size == clips);
            CHECK(g.GroupStack.Size == groups);
            if (imb::BeginMotion(341, &exit_b, 1, leaving)) {
                ImGui::Button("Leaving", ImVec2(80, 20));
                imb::EndMotion();
            }
            CHECK(dl->_ClipRectStack.Size == clips);
            CHECK(g.GroupStack.Size == groups);
        });
    }
}

TEST_CASE("visible flipping every frame keeps the scope balanced") {
    Fixture fx;
    OneTrack in("In", Prop::Opacity, Key(0, 0), Key(1, 1), 0.2f);
    OneTrack out("Out", Prop::Opacity, Key(0, 1), Key(1, 0), 0.2f);
    imb::MotionBinding b[2] = {Bind(&in.def, Trigger::Enter), Bind(&out.def, Trigger::Exit)};
    int shown = 0;
    for (int f = 0; f < 60; f++) {
        fx.Frame([&](ImDrawList*) {
            imb::MotionOptions o;
            o.visible = (f & 1) == 0;
            int groups = ImGui::GetCurrentContext()->GroupStack.Size;
            if (imb::BeginMotion(350, b, 2, o)) {
                shown++;
                ImGui::Button("Flip", ImVec2(60, 20));
                imb::EndMotion();
            }
            CHECK(ImGui::GetCurrentContext()->GroupStack.Size == groups);
        });
    }
    CHECK(shown == 60);
    for (int f = 0; f < 30; f++) {
        fx.Frame([&](ImDrawList*) {
            imb::MotionOptions o;
            o.visible = false;
            if (imb::BeginMotion(350, b, 2, o)) imb::EndMotion();
        });
    }
    CHECK_FALSE(imb::IsMotionPlaying(350));
    bool again = false;
    fx.Frame([&](ImDrawList*) {
        again = imb::BeginMotion(350, b, 2);
        if (again) imb::EndMotion();
    });
    CHECK(again);
}

TEST_CASE("an id reused with other bindings starts over") {
    Fixture fx;
    OneTrack a("A", Prop::Opacity, Key(0, 0), Key(1, 1), 0.5f);
    OneTrack c("C", Prop::TranslateX, Key(0, 50), Key(1, 0), 0.5f);
    imb::MotionBinding ba = Bind(&a.def, Trigger::Enter), bc = Bind(&c.def, Trigger::Enter);
    for (int f = 0; f < 40; f++) fx.Frame([&](ImDrawList* dl) { MotionButton(dl, 360, &ba, 1); });
    ButtonShot s;
    fx.Frame([&](ImDrawList* dl) { s = MotionButton(dl, 360, &bc, 1); });
    CHECK(s.shown);
    CHECK(s.verts.mn.x - s.layout.mn.x > 40.0f);
}

TEST_CASE("a scope left open by a missing EndMotion is cleaned up next frame") {
    Fixture fx;
    imb::MotionBinding none = Bind(nullptr, Trigger::Loop);
    fx.Frame([&](ImDrawList*) {
        CHECK(imb::BeginMotion(370, &none, 1));
        ImGui::Button("Open");
        ImGui::EndGroup();
    });
    fx.Frame([&](ImDrawList* dl) { CHECK(MotionButton(dl, 370, &none, 1).shown); });
    CHECK(imb::detail::MotionState().depth == 0);
}

TEST_CASE("scale 0 collapses the item without NaN") {
    Fixture fx;
    OneTrack t("Flat", Prop::Scale, Key(0, 0), Key(1, 0));
    imb::MotionBinding b = Bind(&t.def, Trigger::Loop);
    ButtonShot s;
    fx.Frame([&](ImDrawList* dl) { s = MotionButton(dl, 390, &b, 1); });
    CHECK(s.verts.W() == doctest::Approx(0.0f));
    CHECK(s.verts.H() == doctest::Approx(0.0f));
    CHECK(std::isfinite(s.verts.mn.x));
}

TEST_CASE("an exit that repeats forever still ends") {
    Fixture fx;
    OneTrack out("Out", Prop::Opacity, Key(0, 1), Key(1, 0), 0.1f);
    out.def.iterations = 0;
    imb::MotionBinding b = Bind(&out.def, Trigger::Exit);
    bool open = true;
    for (int f = 0; f < 60; f++) {
        fx.Frame([&](ImDrawList*) {
            imb::MotionOptions o;
            o.visible = f < 3;
            open = imb::BeginMotion(400, &b, 1, o);
            if (open) imb::EndMotion();
        });
    }
    CHECK_FALSE(open);
}

TEST_CASE("broken keyframe numbers never reach the vertices") {
    const float nan = std::nanf(""), inf = INFINITY;
    struct Case { Prop prop; float a, b; };
    const Case cases[] = {
        {Prop::Opacity, nan, nan}, {Prop::Scale, nan, 1}, {Prop::Scale, 0, 0}, {Prop::Scale, 1e30f, 1e30f},
        {Prop::ScaleX, inf, inf}, {Prop::Rotate, 1e30f, 1e30f}, {Prop::Rotate, inf, inf}, {Prop::Rotate, nan, nan},
        {Prop::TranslateX, inf, inf}, {Prop::TranslateY, -1e30f, -1e30f}, {Prop::SkewX, 1e30f, 1e30f},
        {Prop::SkewY, nan, nan}, {Prop::Tint, nan, inf}, {Prop::Reveal, nan, nan}, {Prop::Glow, nan, nan},
        {Prop::Shadow, inf, inf}};
    for (const Case& cs : cases) {
        Fixture fx;
        OneTrack t("Broken", cs.prop, Key(0, cs.a), Key(1, cs.b));
        imb::MotionBinding b = Bind(&t.def, Trigger::Loop);
        for (int f = 0; f < 3; f++) {
            fx.Frame([&](ImDrawList* dl) {
                int v0 = dl->VtxBuffer.Size;
                MotionButton(dl, 380, &b, 1);
                CHECK_MESSAGE(AllFinite(dl, v0, dl->VtxBuffer.Size), "prop " << (int)cs.prop << " " << cs.a);
            });
        }
    }
}

TEST_CASE("timing survives zero, negative and absurd durations") {
    OneTrack t("Fade", Prop::Opacity, Key(0, 0), Key(1, 1));
    const float durs[] = {0.0f, -1.0f, 1e-30f, 1e30f, std::nanf(""), INFINITY};
    const float scales[] = {0.0f, -2.0f, 1e-30f, 1e30f, std::nanf("")};
    const float times[] = {0.0f, 0.016f, 1.0f, 1e6f};
    for (float d : durs) {
        for (float sc : scales) {
            t.def.duration = d;
            for (int iters = 0; iters < 3; iters++) {
                t.def.iterations = iters;
                for (float time : times) {
                    Pose p = imb::SampleAnim(t.def, time, sc);
                    CAPTURE(d);
                    CAPTURE(sc);
                    CHECK(std::isfinite(p.opacity));
                }
                imb::AnimFinished(t.def, 1.0f, sc);
            }
        }
    }
}

TEST_CASE("eases stay finite for extreme parameters") {
    const float nan = std::nanf(""), inf = INFINITY;
    const float vals[] = {0.0f, -1.0f, 1.0f, 1e-30f, 1e30f, nan, inf, -inf, 170.0f, 26.0f};
    for (float a : vals) {
        for (float b : vals) {
            EaseSpec sp;
            sp.kind = EaseKind::Spring;
            sp.p[0] = a;
            sp.p[1] = b;
            EaseSpec st;
            st.kind = EaseKind::Steps;
            st.p[0] = a;
            EaseSpec bz = Bezier(a, b, b, a);
            for (int i = 0; i <= 100; i++) {
                float t = (float)i / 100.0f;
                CAPTURE(a);
                CAPTURE(b);
                float v = imb::SampleEase(sp, t);
                CHECK(std::isfinite(v));
                CHECK(std::fabs(v) < 1e4f);
                CHECK(std::isfinite(imb::SampleEase(st, t)));
                CHECK(std::isfinite(imb::SampleEase(bz, t)));
            }
            CHECK(imb::SampleEase(sp, 1.0f) == 1.0f);
        }
    }
}

TEST_CASE("steps(0) acts like one step and a hard bezier stays monotone") {
    EaseSpec st;
    st.kind = EaseKind::Steps;
    st.p[0] = 0.0f;
    CHECK(imb::SampleEase(st, 0.5f) == 0.0f);
    CHECK(imb::SampleEase(st, 1.0f) == 1.0f);
    EaseSpec bz = Bezier(1.0f, 0.0f, 0.0f, 1.0f);
    float prev = -1.0f;
    for (int i = 0; i <= 200; i++) {
        float v = imb::SampleEase(bz, (float)i / 200.0f);
        CHECK(v >= prev - 1e-4f);
        prev = v;
    }
}
