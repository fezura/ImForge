#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "imgui_headless.h"
#include "imforge/imb_runtime.h"

#include <cmath>

namespace {

const float kDt = 1.0f / 60.0f;
const ImVec2 kMin(60, 60), kMax(260, 160);

struct Fixture {
    imforge::test::HeadlessImGui h;
    Fixture() { imb::Reset(); }
    ~Fixture() { imb::Reset(); }

    template <class F>
    void Frame(F fn) {
        h.BeginFrame();
        imb::NewFrame(kDt);
        fn();
        h.EndFrame();
    }

    template <class F>
    bool WindowFrame(F fn) {
        bool ran = false;
        Frame([&] {
            ImGui::SetNextWindowPos(ImVec2(20, 20));
            ImGui::SetNextWindowSize(ImVec2(500, 400));
            if (ImGui::Begin("rt", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
                ran = true;
                fn(ImGui::GetWindowDrawList());
            }
            ImGui::End();
        });
        return ran;
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

}  // namespace

TEST_CASE("easing endpoints, range and monotonicity") {
    using imb::Ease;
    const Ease all[] = {Ease::Linear,     Ease::InQuad,  Ease::OutQuad, Ease::InOutQuad,  Ease::InCubic,   Ease::OutCubic,
                        Ease::InOutCubic, Ease::OutBack, Ease::InBack,  Ease::OutElastic, Ease::OutBounce, Ease::OutExpo};
    for (Ease e : all) {
        CHECK(imb::EaseApply(e, 0.0f) == doctest::Approx(0.0f));
        CHECK(imb::EaseApply(e, 1.0f) == doctest::Approx(1.0f));
        CHECK(imb::EaseApply(e, -1.0f) == 0.0f);
        CHECK(imb::EaseApply(e, 2.0f) == 1.0f);
    }
    const Ease mono[] = {Ease::Linear,   Ease::InQuad,     Ease::OutQuad, Ease::InOutQuad,
                         Ease::InCubic,  Ease::OutCubic,   Ease::InOutCubic, Ease::OutExpo};
    for (Ease e : mono) {
        float prev = 0.0f;
        int violations = 0;
        for (int i = 1; i <= 200; i++) {
            float v = imb::EaseApply(e, (float)i / 200.0f);
            if (v < prev - 1e-6f) violations++;
            prev = v;
        }
        CHECK(violations == 0);
    }
    float back_max = 0.0f, elastic_max = 0.0f, bounce_min = 1.0f, bounce_max = 0.0f;
    for (int i = 0; i <= 200; i++) {
        float t = (float)i / 200.0f;
        back_max = std::fmax(back_max, imb::EaseApply(Ease::OutBack, t));
        elastic_max = std::fmax(elastic_max, imb::EaseApply(Ease::OutElastic, t));
        bounce_min = std::fmin(bounce_min, imb::EaseApply(Ease::OutBounce, t));
        bounce_max = std::fmax(bounce_max, imb::EaseApply(Ease::OutBounce, t));
    }
    CHECK(back_max > 1.05f);
    CHECK(elastic_max > 1.05f);
    CHECK(bounce_min >= 0.0f);
    CHECK(bounce_max <= 1.0f + 1e-6f);
    CHECK(imb::EaseApply(Ease::InBack, 0.2f) < 0.0f);
    CHECK(imb::EaseApply(Ease::OutExpo, 0.5f) == doctest::Approx(1.0f - 1.0f / 32.0f));
    CHECK(imb::EaseApply(Ease::OutBounce, 0.5f) == doctest::Approx(0.765625f));
}

TEST_CASE("Lerp helpers") {
    CHECK(imb::Lerp(0.0f, 10.0f, 0.25f) == doctest::Approx(2.5f));
    ImVec2 v2 = imb::Lerp(ImVec2(0, 0), ImVec2(10, 20), 0.5f);
    CHECK(v2.x == doctest::Approx(5.0f));
    CHECK(v2.y == doctest::Approx(10.0f));
    ImVec4 v4 = imb::Lerp(ImVec4(0, 0, 0, 0), ImVec4(1, 2, 3, 4), 0.5f);
    CHECK(v4.z == doctest::Approx(1.5f));
    CHECK(v4.w == doctest::Approx(2.0f));
    ImU32 a = IM_COL32(0, 0, 0, 0), b = IM_COL32(255, 255, 255, 255);
    CHECK(imb::LerpCol(a, b, 0.0f) == a);
    CHECK(imb::LerpCol(a, b, 1.0f) == b);
    CHECK(imb::LerpCol(a, b, 0.5f) == IM_COL32(128, 128, 128, 128));
    CHECK(imb::LerpCol(IM_COL32(200, 0, 0, 255), IM_COL32(0, 0, 200, 255), 2.0f) == IM_COL32(0, 0, 200, 255));
}

TEST_CASE("DeltaTime override lasts one frame") {
    Fixture f;
    f.h.BeginFrame();
    CHECK(imb::DeltaTime() == ImGui::GetIO().DeltaTime);
    imb::NewFrame(0.25f);
    CHECK(imb::DeltaTime() == 0.25f);
    f.h.EndFrame();
    f.h.BeginFrame();
    CHECK(imb::DeltaTime() == ImGui::GetIO().DeltaTime);
    f.h.EndFrame();
}

TEST_CASE("Transition reaches its target after duration") {
    Fixture f;
    const ImGuiID id = 1234;
    float v = -1.0f;
    f.Frame([&] { v = imb::Transition(id, false, 0.5f, imb::Ease::Linear); });
    CHECK(v == 0.0f);
    for (int i = 0; i < 15; i++) f.Frame([&] { v = imb::Transition(id, true, 0.5f, imb::Ease::Linear); });
    CHECK(v == doctest::Approx(0.5f).epsilon(0.02));
    for (int i = 0; i < 16; i++) f.Frame([&] { v = imb::Transition(id, true, 0.5f, imb::Ease::Linear); });
    CHECK(v == doctest::Approx(1.0f));
    for (int i = 0; i < 40; i++) f.Frame([&] { v = imb::Transition(id, true, 0.5f, imb::Ease::Linear); });
    CHECK(v == 1.0f);
    for (int i = 0; i < 31; i++) f.Frame([&] { v = imb::Transition(id, false, 0.5f, imb::Ease::OutCubic); });
    CHECK(v == doctest::Approx(0.0f));

    f.Frame([&] { v = imb::Transition(id, true, 0.0f); });
    CHECK(v == 1.0f);
    f.Frame([&] { v = imb::Transition(id, false, 0.0f); });
    CHECK(v == 0.0f);

    f.Frame([&] { v = imb::Transition(999, true, 1.0f); });
    CHECK(v == 1.0f);

    f.Frame([&] { imb::Transition(555, false, 1.0f); });
    f.Frame([&] {
        float a = imb::Transition(555, true, 1.0f);
        float b = imb::Transition(555, true, 1.0f);
        CHECK(a > 0.0f);
        CHECK(a == b);
    });
}

TEST_CASE("AnimateTo retargets without jumps") {
    Fixture f;
    const ImGuiID id = 42;
    float v = -1.0f;
    f.Frame([&] { v = imb::AnimateTo(id, 0.0f, 1.0f); });
    CHECK(v == 0.0f);
    for (int i = 0; i < 20; i++) f.Frame([&] { v = imb::AnimateTo(id, 100.0f, 1.0f); });
    CHECK(v > 10.0f);
    CHECK(v < 100.0f);
    float prev = v;
    f.Frame([&] { v = imb::AnimateTo(id, -50.0f, 1.0f); });
    CHECK(v < prev);
    CHECK(std::fabs(v - prev) < 10.0f);
    for (int i = 0; i < 120; i++) f.Frame([&] { v = imb::AnimateTo(id, -50.0f, 1.0f); });
    CHECK(v == doctest::Approx(-50.0f));
    f.Frame([&] { v = imb::AnimateTo(id, 7.0f, 0.0f); });
    CHECK(v == 7.0f);
}

TEST_CASE("Spring converges and tolerates large dt") {
    Fixture f;
    const ImGuiID id = 7;
    float v = -1.0f, max_v = 0.0f;
    f.Frame([&] { v = imb::Spring(id, 0.0f); });
    CHECK(v == 0.0f);
    for (int i = 0; i < 180; i++) {
        f.Frame([&] { v = imb::Spring(id, 100.0f); });
        max_v = std::fmax(max_v, v);
    }
    CHECK(v == doctest::Approx(100.0f).epsilon(0.001));
    CHECK(max_v < 110.0f);
    f.h.BeginFrame();
    imb::NewFrame(1.0f);
    v = imb::Spring(id, 0.0f, 2000.0f, 10.0f);
    f.h.EndFrame();
    CHECK(std::fabs(v) < 200.0f);
    for (int i = 0; i < 600; i++) f.Frame([&] { v = imb::Spring(id, 0.0f, 2000.0f, 10.0f); });
    CHECK(std::fabs(v) < 1.0f);
}

TEST_CASE("BeginEnter/EndEnter stay balanced and restore layout") {
    Fixture f;
    const ImGuiID id = 99;
    bool ran = f.WindowFrame([&](ImDrawList*) {
        float a0 = ImGui::GetStyle().Alpha;
        CHECK_FALSE(imb::BeginEnter(id, false, 0.3f, imb::Preset::FadeSlideUp));
        CHECK(ImGui::GetStyle().Alpha == doctest::Approx(a0));
    });
    CHECK(ran);

    ran = f.WindowFrame([&](ImDrawList*) {
        float a0 = ImGui::GetStyle().Alpha, y0 = ImGui::GetCursorPosY();
        REQUIRE(imb::BeginEnter(id, true, 0.3f, imb::Preset::FadeSlideUp));
        CHECK(ImGui::GetStyle().Alpha < a0);
        CHECK(ImGui::GetStyle().Alpha > 0.0f);
        CHECK(ImGui::GetCursorPosY() > y0);
        ImGui::Dummy(ImVec2(10, 10));
        imb::EndEnter();
        CHECK(ImGui::GetStyle().Alpha == doctest::Approx(a0));
        CHECK(ImGui::GetCursorPosY() == doctest::Approx(y0 + 10.0f + ImGui::GetStyle().ItemSpacing.y));
    });
    CHECK(ran);

    for (int i = 0; i < 30; i++)
        f.WindowFrame([&](ImDrawList*) {
            if (imb::BeginEnter(id, true, 0.3f, imb::Preset::FadeSlideUp)) {
                ImGui::Dummy(ImVec2(10, 10));
                imb::EndEnter();
            }
        });
    f.WindowFrame([&](ImDrawList*) {
        float a0 = ImGui::GetStyle().Alpha, y0 = ImGui::GetCursorPosY();
        REQUIRE(imb::BeginEnter(id, true, 0.3f, imb::Preset::FadeSlideUp));
        CHECK(ImGui::GetStyle().Alpha == doctest::Approx(a0));
        CHECK(ImGui::GetCursorPosY() == doctest::Approx(y0));
        imb::EndEnter();
    });

    const imb::Preset presets[] = {imb::Preset::None,       imb::Preset::Fade,          imb::Preset::SlideUp,
                                   imb::Preset::SlideDown,  imb::Preset::SlideLeft,     imb::Preset::SlideRight,
                                   imb::Preset::FadeSlideUp, imb::Preset::FadeSlideDown, imb::Preset::ScalePop,
                                   imb::Preset::Stagger};
    for (int k = 0; k < 10; k++) {
        for (int i = 0; i < 3; i++) {
            f.WindowFrame([&](ImDrawList*) {
                float a0 = ImGui::GetStyle().Alpha, x0 = ImGui::GetCursorPosX();
                ImGui::Dummy(ImVec2(10, 10));
                float y0 = ImGui::GetCursorPosY();
                bool open = imb::BeginEnter(1000 + (ImGuiID)k, true, 0.5f, presets[k]);
                CHECK(open);
                if (open) {
                    ImGui::Dummy(ImVec2(10, 10));
                    ImGui::Text("x");
                    imb::EndEnter();
                }
                CHECK(ImGui::GetStyle().Alpha == doctest::Approx(a0));
                CHECK(ImGui::GetCursorPosX() == doctest::Approx(x0));
                ImGui::Dummy(ImVec2(10, 10));
                CHECK(ImGui::GetCursorPosX() == doctest::Approx(x0));
                CHECK(ImGui::GetCursorPosY() == doctest::Approx(y0 + 3.0f * (ImGui::GetStyle().ItemSpacing.y) + 20.0f +
                                                                ImGui::GetTextLineHeight()));
            });
        }
    }

    for (int i = 0; i < 3; i++)
        f.WindowFrame([&](ImDrawList*) {
            float a0 = ImGui::GetStyle().Alpha;
            REQUIRE(imb::BeginEnter(2000, true, 0.3f, imb::Preset::Stagger, 0.2f));
            CHECK(ImGui::GetStyle().Alpha == 0.0f);
            imb::EndEnter();
            CHECK(ImGui::GetStyle().Alpha == doctest::Approx(a0));
        });
    for (int i = 0; i < 12; i++)
        f.WindowFrame([&](ImDrawList*) {
            if (imb::BeginEnter(2000, true, 0.3f, imb::Preset::Stagger, 0.2f)) imb::EndEnter();
        });
    f.WindowFrame([&](ImDrawList*) {
        REQUIRE(imb::BeginEnter(2000, true, 0.3f, imb::Preset::Stagger, 0.2f));
        CHECK(ImGui::GetStyle().Alpha > 0.0f);
        imb::EndEnter();
    });

    int frames_visible = 0;
    for (int i = 0; i < 60; i++)
        f.WindowFrame([&](ImDrawList*) {
            if (imb::BeginEnter(id, false, 0.3f, imb::Preset::FadeSlideUp)) {
                frames_visible++;
                imb::EndEnter();
            }
        });
    CHECK(frames_visible > 5);
    CHECK(frames_visible < 30);
}

TEST_CASE("Tab crossfade fades on tab change") {
    Fixture f;
    const ImGuiID id = 77;
    bool ran = f.WindowFrame([&](ImDrawList*) {
        float a0 = ImGui::GetStyle().Alpha;
        imb::BeginTabCrossfade(id, 0, 0.3f);
        CHECK(ImGui::GetStyle().Alpha == doctest::Approx(a0));
        imb::EndTabCrossfade();
        CHECK(ImGui::GetStyle().Alpha == doctest::Approx(a0));
    });
    CHECK(ran);
    f.WindowFrame([&](ImDrawList*) {
        float a0 = ImGui::GetStyle().Alpha;
        imb::BeginTabCrossfade(id, 1, 0.3f);
        CHECK(ImGui::GetStyle().Alpha < a0);
        CHECK(ImGui::GetStyle().Alpha > 0.0f);
        imb::EndTabCrossfade();
        CHECK(ImGui::GetStyle().Alpha == doctest::Approx(a0));
    });
    for (int i = 0; i < 30; i++)
        f.WindowFrame([&](ImDrawList*) {
            imb::BeginTabCrossfade(id, 1, 0.3f);
            imb::EndTabCrossfade();
        });
    f.WindowFrame([&](ImDrawList*) {
        float a0 = ImGui::GetStyle().Alpha;
        imb::BeginTabCrossfade(id, 1, 0.3f);
        CHECK(ImGui::GetStyle().Alpha == doctest::Approx(a0));
        imb::EndTabCrossfade();
    });
}

TEST_CASE("DrawList effects add geometry") {
    Fixture f;
    f.WindowFrame([](ImDrawList*) {});
    const ImU32 red = IM_COL32(255, 0, 0, 255), blue = IM_COL32(0, 0, 255, 255), white = IM_COL32(255, 255, 255, 160);

    struct Effect {
        const char* name;
        void (*draw)(ImDrawList*);
    };
    const Effect effects[] = {
        {"GradientRect", [](ImDrawList* dl) { imb::GradientRect(dl, kMin, kMax, IM_COL32(255, 0, 0, 255), IM_COL32(0, 0, 255, 255), true); }},
        {"GradientRect horizontal rounded", [](ImDrawList* dl) { imb::GradientRect(dl, kMin, kMax, IM_COL32(255, 0, 0, 255), IM_COL32(0, 0, 255, 255), false, 12.0f); }},
        {"RadialGradient", [](ImDrawList* dl) { imb::RadialGradient(dl, ImVec2(160, 110), 40.0f, IM_COL32(255, 255, 255, 255), IM_COL32(255, 255, 255, 0)); }},
        {"Shadow", [](ImDrawList* dl) { imb::Shadow(dl, kMin, kMax, 6.0f, IM_COL32(0, 0, 0, 128), 12.0f); }},
        {"Shadow square", [](ImDrawList* dl) { imb::Shadow(dl, kMin, kMax, 0.0f, IM_COL32(0, 0, 0, 128), 3.0f, ImVec2(4, 4)); }},
        {"Glow", [](ImDrawList* dl) { imb::Glow(dl, kMin, kMax, 6.0f, IM_COL32(0, 200, 255, 160), 10.0f); }},
        {"Border Solid", [](ImDrawList* dl) { imb::AnimatedBorder(dl, kMin, kMax, 6.0f, 2.0f, imb::BorderStyle::Solid, IM_COL32(255, 255, 255, 255)); }},
        {"Border Rainbow", [](ImDrawList* dl) { imb::AnimatedBorder(dl, kMin, kMax, 6.0f, 2.0f, imb::BorderStyle::Rainbow, IM_COL32(255, 255, 255, 255)); }},
        {"Border Rainbow square", [](ImDrawList* dl) { imb::AnimatedBorder(dl, kMin, kMax, 0.0f, 1.0f, imb::BorderStyle::Rainbow, IM_COL32(255, 255, 255, 255), 2.0f); }},
        {"Border Sweep", [](ImDrawList* dl) { imb::AnimatedBorder(dl, kMin, kMax, 6.0f, 2.0f, imb::BorderStyle::Sweep, IM_COL32(255, 255, 255, 255)); }},
        {"Border Pulse", [](ImDrawList* dl) { imb::AnimatedBorder(dl, kMin, kMax, 6.0f, 2.0f, imb::BorderStyle::Pulse, IM_COL32(255, 255, 255, 255)); }},
        {"Particles", [](ImDrawList* dl) { imb::ParticleStyle st; st.connect = true; st.connect_dist = 200.0f; imb::Particles(dl, 5, kMin, kMax, st); }},
        {"NoiseBackground", [](ImDrawList* dl) { imb::NoiseBackground(dl, kMin, kMax, IM_COL32(255, 255, 255, 255), 0.5f, 17); }},
    };
    for (const Effect& e : effects) {
        INFO(e.name);
        bool ran = f.WindowFrame([&](ImDrawList* dl) {
            int v0 = dl->VtxBuffer.Size, i0 = dl->IdxBuffer.Size;
            e.draw(dl);
            CHECK(dl->VtxBuffer.Size > v0);
            CHECK(dl->IdxBuffer.Size > i0);
        });
        CHECK(ran);
    }

    f.WindowFrame([&](ImDrawList* dl) {
        int v0 = dl->VtxBuffer.Size;
        imb::GradientRect(dl, kMin, kMax, red, blue, true, 8.0f);
        CHECK(dl->VtxBuffer.Size > v0 + 4);
        bool has_red = false, has_blue = false;
        for (int i = v0; i < dl->VtxBuffer.Size; i++) {
            ImU32 c = dl->VtxBuffer[i].col;
            if (((c >> IM_COL32_A_SHIFT) & 0xFF) == 0) continue;
            if (((c >> IM_COL32_R_SHIFT) & 0xFF) > 200) has_red = true;
            if (((c >> IM_COL32_B_SHIFT) & 0xFF) > 200) has_blue = true;
        }
        CHECK(has_red);
        CHECK(has_blue);
    });

    f.WindowFrame([&](ImDrawList* dl) {
        int v0 = dl->VtxBuffer.Size;
        imb::NoiseBackground(dl, kMin, kMax, white, 0.0f, 1);
        imb::GradientRect(dl, kMax, kMin, red, blue, true);
        imb::RadialGradient(dl, kMin, 0.0f, red, blue);
        imb::Shadow(dl, kMin, kMax, 0.0f, IM_COL32(0, 0, 0, 0), 10.0f);
        CHECK(dl->VtxBuffer.Size == v0);
    });

    SetMouse(1000, 600);
    f.WindowFrame([&](ImDrawList* dl) {
        int v0 = dl->VtxBuffer.Size;
        imb::CursorHighlight(dl, kMin, kMax, 6.0f, white, 60.0f);
        CHECK(dl->VtxBuffer.Size == v0);
    });
    SetMouse(100, 100);
    f.WindowFrame([&](ImDrawList* dl) {
        int v0 = dl->VtxBuffer.Size;
        imb::CursorHighlight(dl, kMin, kMax, 6.0f, white, 60.0f);
        CHECK(dl->VtxBuffer.Size > v0);
    });
}

TEST_CASE("Particles are deterministic per id") {
    Fixture f;
    f.WindowFrame([](ImDrawList*) {});
    imb::ParticleStyle st;
    st.count = 30;
    st.connect = true;
    st.connect_dist = 120.0f;
    ImVector<ImVec2> a, b, c, d;
    auto capture = [](ImDrawList* dl, int from, ImVector<ImVec2>& out) {
        for (int i = from; i < dl->VtxBuffer.Size; i++) out.push_back(dl->VtxBuffer[i].pos);
    };
    bool ran = f.WindowFrame([&](ImDrawList* dl) {
        int v0 = dl->VtxBuffer.Size;
        imb::Particles(dl, 1, kMin, kMax, st);
        capture(dl, v0, a);
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        int f0 = fg->VtxBuffer.Size;
        imb::Particles(fg, 1, kMin, kMax, st);
        capture(fg, f0, b);
        int v1 = dl->VtxBuffer.Size;
        imb::Particles(dl, 2, kMin, kMax, st);
        capture(dl, v1, c);
    });
    CHECK(ran);
    CHECK(a.Size > 0);
    REQUIRE(a.Size == b.Size);
    int mismatches = 0;
    for (int i = 0; i < a.Size; i++)
        if (a[i].x != b[i].x || a[i].y != b[i].y) mismatches++;
    CHECK(mismatches == 0);
    bool differs = a.Size != c.Size;
    for (int i = 0; !differs && i < a.Size; i++) differs = a[i].x != c[i].x || a[i].y != c[i].y;
    CHECK(differs);

    imb::Reset();
    f.WindowFrame([&](ImDrawList* dl) {
        int v0 = dl->VtxBuffer.Size;
        imb::Particles(dl, 1, kMin, kMax, st);
        capture(dl, v0, d);
    });
    REQUIRE(a.Size == d.Size);
    mismatches = 0;
    for (int i = 0; i < a.Size; i++)
        if (a[i].x != d[i].x || a[i].y != d[i].y) mismatches++;
    CHECK(mismatches == 0);

    ImVector<ImVec2> e;
    f.WindowFrame([&](ImDrawList* dl) {
        int v0 = dl->VtxBuffer.Size;
        imb::Particles(dl, 1, kMin, kMax, st);
        capture(dl, v0, e);
    });
    REQUIRE(e.Size == d.Size);
    bool moved = false;
    for (int i = 0; !moved && i < e.Size; i++) moved = e[i].x != d[i].x || e[i].y != d[i].y;
    CHECK(moved);
}

TEST_CASE("Stale animation entries are recycled after 2 s") {
    Fixture f;
    const ImGuiID id = 31337;
    float v = -1.0f;
    f.Frame([&] { imb::Transition(id, true, 1.0f); });
    f.Frame([&] { v = imb::Transition(id, false, 1.0f); });
    CHECK(v < 1.0f);
    CHECK(v > 0.5f);
    double t0 = ImGui::GetTime();
    int n = 0;
    while (ImGui::GetTime() < t0 + 3.5 && n < 200000) {
        f.h.BeginFrame();
        f.h.EndFrame();
        n++;
    }
    if (ImGui::GetTime() >= t0 + 3.5) {
        f.Frame([&] { v = imb::Transition(id, false, 1.0f); });
        CHECK(v == 0.0f);
    } else {
        MESSAGE("ImGui time did not advance; GC check skipped");
    }
}

TEST_CASE("Animation table grows past its initial capacity") {
    Fixture f;
    const int n = 5000;
    f.Frame([&] {
        for (int i = 0; i < n; i++) imb::Transition((ImGuiID)(i * 7919 + 1), false, 1.0f, imb::Ease::Linear);
    });
    for (int k = 0; k < 6; k++)
        f.Frame([&] {
            for (int i = 0; i < n; i++) imb::Transition((ImGuiID)(i * 7919 + 1), true, 1.0f, imb::Ease::Linear);
        });
    int bad = 0;
    f.Frame([&] {
        for (int i = 0; i < n; i++) {
            float v = imb::Transition((ImGuiID)(i * 7919 + 1), true, 1.0f, imb::Ease::Linear);
            if (std::fabs(v - 7.0f * kDt) > 1e-4f) bad++;
        }
    });
    CHECK(bad == 0);
}
