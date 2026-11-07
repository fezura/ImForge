#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "imgui_headless.h"
#include "imb_essentials.h"

#include <cmath>
#include <cstring>

using imforge::test::HeadlessImGui;
namespace ess = imb::ess;

namespace {

const ImGuiWindowFlags kWinFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoSavedSettings;

void SetMouse(const ImVec2& pos, bool down, int button = 0) {
    ImGuiIO& io = ImGui::GetIO();
#if IMGUI_VERSION_NUM >= 18700
    io.AddMousePosEvent(pos.x, pos.y);
    io.AddMouseButtonEvent(button, down);
#else
    io.MousePos = pos;
    io.MouseDown[button] = down;
#endif
}

template <class F> void Frame(HeadlessImGui& h, F&& body) {
    h.BeginFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(600, 500));
    ImGui::Begin("t", nullptr, kWinFlags);
    body();
    ImGui::End();
    h.EndFrame();
}

template <class F> void Click(HeadlessImGui& h, const ImVec2& pos, F&& body, int button = 0) {
    SetMouse(pos, false, button);
    Frame(h, body);
    Frame(h, body);
    SetMouse(pos, true, button);
    Frame(h, body);
    Frame(h, body);
    SetMouse(pos, false, button);
    Frame(h, body);
    Frame(h, body);
}

template <class F> void PressKey(HeadlessImGui& h, int key, F&& body) {
    ImGuiIO& io = ImGui::GetIO();
#if IMGUI_VERSION_NUM >= 18700
    io.AddKeyEvent((ImGuiKey)key, true);
    Frame(h, body);
    Frame(h, body);
    io.AddKeyEvent((ImGuiKey)key, false);
#else
    io.KeysDown[key] = true;
    Frame(h, body);
    io.KeysDown[key] = false;
#endif
    Frame(h, body);
    Frame(h, body);
}

ImVec2 Center(const ImVec2& a, const ImVec2& b) {
    return ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
}

}  // namespace

TEST_CASE("KeyName encodings") {
    HeadlessImGui h;
    CHECK(std::strcmp(ess::KeyName(0), "None") == 0);
    CHECK(std::strcmp(ess::KeyName(-1), "Mouse1") == 0);
    CHECK(std::strcmp(ess::KeyName(-5), "Mouse5") == 0);
    CHECK(std::strcmp(ess::KeyName(-9), "None") == 0);
    CHECK(std::strcmp(ess::KeyName(12345), "Key 12345") == 0);
#if IMGUI_VERSION_NUM >= 18700
    CHECK(std::strcmp(ess::KeyName(ImGuiKey_F1), "F1") == 0);
    CHECK(std::strcmp(ess::KeyName(ImGuiKey_A), "A") == 0);
#else
    CHECK(std::strcmp(ess::KeyName('A'), "A") == 0);
    ImGui::GetIO().KeyMap[ImGuiKey_Escape] = 27;
    CHECK(std::strcmp(ess::KeyName(27), "Escape") == 0);
#endif
}

TEST_CASE("Style color fallbacks derive from ImGui style") {
    HeadlessImGui h;
    ess::GetStyle() = ess::Style();
    Frame(h, [&] {
        CHECK(ess::detail::Accent() == ImGui::GetColorU32(ImGuiCol_CheckMark));
        CHECK(ess::detail::Rounding() == ImGui::GetStyle().FrameRounding);
        ess::GetStyle().accent = IM_COL32(1, 2, 3, 255);
        CHECK(ess::detail::Accent() == ImGui::GetColorU32(IM_COL32(1, 2, 3, 255)));
        CHECK(ess::detail::WithAlpha(IM_COL32(1, 2, 3, 200), 0.5f) == IM_COL32(1, 2, 3, 100));
    });
    ess::GetStyle() = ess::Style();
}

TEST_CASE("Toggle flips on click and reports change") {
    HeadlessImGui h;
    bool v = false;
    int presses = 0;
    ImVec2 mn, mx;
    auto body = [&] {
        if (ess::Toggle("Enable##t", &v)) ++presses;
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
        CHECK(ImGui::GetItemRectSize().y == ImGui::GetFrameHeight());
    };
    Frame(h, body);
    Frame(h, body);
    CHECK(mx.x - mn.x > ImGui::GetFrameHeight() * 2.0f);
    Click(h, Center(mn, mx), body);
    CHECK(v == true);
    CHECK(presses == 1);
    Click(h, Center(mn, mx), body);
    CHECK(v == false);
    CHECK(presses == 2);
    bool d = true;
    ess::ToggleStyle ts;
    ts.disabled = true;
    auto disabled = [&] {
        ess::Toggle("Off##d", &d, ts);
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
    };
    Frame(h, disabled);
    Frame(h, disabled);
    Click(h, Center(mn, mx), disabled);
    CHECK(d == true);
}

TEST_CASE("KeybindPicker captures keys, mouse buttons and Escape") {
    HeadlessImGui h;
    int key = 0;
    int changes = 0;
    ImVec2 mn, mx;
    auto body = [&] {
        if (ess::KeybindPicker("Bind##k", &key)) ++changes;
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
    };
    Frame(h, body);
    Frame(h, body);
    CHECK_FALSE(ess::IsCapturingKeybind());
    Click(h, Center(mn, mx), body);
    CHECK(ess::IsCapturingKeybind());
#if IMGUI_VERSION_NUM >= 18700
    const int kKey = ImGuiKey_F1;
    const char* kName = "F1";
    const int kEsc = ImGuiKey_Escape;
#else
    const int kKey = 'A';
    const char* kName = "A";
    const int kEsc = 27;
    ImGui::GetIO().KeyMap[ImGuiKey_Escape] = kEsc;
#endif
    PressKey(h, kKey, body);
    CHECK(key == kKey);
    CHECK(changes == 1);
    CHECK_FALSE(ess::IsCapturingKeybind());
    CHECK(std::strcmp(ess::KeyName(key), kName) == 0);

    Click(h, Center(mn, mx), body);
    CHECK(ess::IsCapturingKeybind());
    Click(h, ImVec2(400, 400), body, 1);
    CHECK(key == -2);
    CHECK(changes == 2);
    CHECK(std::strcmp(ess::KeyName(key), "Mouse2") == 0);

    Click(h, Center(mn, mx), body);
    CHECK(ess::IsCapturingKeybind());
    PressKey(h, kEsc, body);
    CHECK(key == 0);
    CHECK(changes == 3);
    CHECK_FALSE(ess::IsCapturingKeybind());
}

TEST_CASE("ColorSwatchRow selects on click and opens a picker on right click") {
    HeadlessImGui h;
    ImVec4 cols[3] = {ImVec4(1, 0, 0, 1), ImVec4(0, 1, 0, 1), ImVec4(0, 0, 1, 0.5f)};
    int sel = 0;
    int changes = 0;
    bool popup = false;
    ImVec2 mn, mx;
    float sz = 0, gap = 0;
    auto body = [&] {
        if (ess::ColorSwatchRow("Colors##c", cols, 3, &sel)) ++changes;
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
        sz = ImGui::GetFrameHeight();
        gap = ImGui::GetStyle().ItemInnerSpacing.x;
        ImGui::PushID("Colors##c");
        popup = ImGui::IsPopupOpen("##picker");
        ImGui::PopID();
    };
    Frame(h, body);
    Frame(h, body);
    CHECK(mx.x - mn.x > sz * 3.0f);
    Click(h, ImVec2(mn.x + sz + gap + sz * 0.5f, mn.y + sz * 0.5f), body);
    CHECK(sel == 1);
    CHECK(changes == 1);
    CHECK_FALSE(popup);
    Click(h, ImVec2(mn.x + (sz + gap) * 2.0f + sz * 0.5f, mn.y + sz * 0.5f), body, 1);
    CHECK(popup);
    CHECK(sel == 1);
}

TEST_CASE("AnimatedTabBar and AnimatedSideTabs change index on click") {
    HeadlessImGui h;
    const char* labels[] = {"Aim", "Visuals", "Misc"};
    const char* icons[] = {"A", "V", nullptr};
    int idx = 0, idx2 = 0, side = 0;
    int changes = 0, changes2 = 0, side_changes = 0;
    ImVec2 bar_mn, bar_mx, bar2_mn, bar2_mx, side_mn, side_mx;
    auto body = [&] {
        if (ess::AnimatedTabBar("tabs", labels, 3, &idx)) ++changes;
        bar_mn = ImGui::GetItemRectMin();
        bar_mx = ImGui::GetItemRectMax();
        ess::TabStyle pill;
        pill.pill = true;
        if (ess::AnimatedTabBar("tabs2", "One\0Two\0", &idx2, pill)) ++changes2;
        bar2_mn = ImGui::GetItemRectMin();
        bar2_mx = ImGui::GetItemRectMax();
        if (ess::AnimatedSideTabs("side", labels, icons, 3, &side)) ++side_changes;
        side_mn = ImGui::GetItemRectMin();
        side_mx = ImGui::GetItemRectMax();
    };
    Frame(h, body);
    Frame(h, body);
    float w = bar_mx.x - bar_mn.x;
    CHECK(w > 100.0f);
    Click(h, ImVec2(bar_mn.x + w * 5.0f / 6.0f, Center(bar_mn, bar_mx).y), body);
    CHECK(idx == 2);
    CHECK(changes == 1);
    Click(h, ImVec2(bar_mn.x + w * 5.0f / 6.0f, Center(bar_mn, bar_mx).y), body);
    CHECK(changes == 1);
    Click(h, ImVec2(bar2_mn.x + (bar2_mx.x - bar2_mn.x) * 0.75f, Center(bar2_mn, bar2_mx).y), body);
    CHECK(idx2 == 1);
    CHECK(changes2 == 1);
    float row_h = (side_mx.y - side_mn.y) / 3.0f;
    Click(h, ImVec2(Center(side_mn, side_mx).x, side_mn.y + row_h * 1.5f), body);
    CHECK(side == 1);
    CHECK(side_changes == 1);
    int none = 0;
    Frame(h, [&] { CHECK_FALSE(ess::AnimatedTabBar("empty", "", &none)); });
}

TEST_CASE("GlowButton reports clicks") {
    HeadlessImGui h;
    int clicks = 0;
    ImVec2 mn, mx;
    auto body = [&] {
        if (ess::GlowButton("Fire##g", ImVec2(120, 0), IM_COL32(200, 40, 40, 255))) ++clicks;
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
    };
    Frame(h, body);
    Frame(h, body);
    CHECK(mx.x - mn.x == doctest::Approx(120.0f));
    Click(h, Center(mn, mx), body);
    CHECK(clicks == 1);
    Click(h, ImVec2(mx.x + 50, mx.y + 50), body);
    CHECK(clicks == 1);
}

TEST_CASE("SectionCard header toggles open state") {
    HeadlessImGui h;
    bool open = true;
    ImVec2 mn, mx;
    float header_h = 0;
    auto body = [&] {
        ess::SectionCard("Card##s", ImVec2(0, 150), &open);
        ImGui::Text("inner");
        ess::EndSectionCard();
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
        header_h = floorf(ImGui::GetFrameHeight() * 1.2f + 0.5f);
    };
    Frame(h, body);
    Frame(h, body);
    CHECK(mx.y - mn.y == doctest::Approx(150.0f));
    Click(h, ImVec2(mn.x + 40, mn.y + header_h * 0.5f), body);
    CHECK(open == false);
    CHECK(mx.y - mn.y == doctest::Approx(header_h));
    Click(h, ImVec2(mn.x + 40, mn.y + header_h * 0.5f), body);
    CHECK(open == true);
    CHECK(mx.y - mn.y == doctest::Approx(150.0f));
    Frame(h, [&] {
        ess::SectionCard("Plain##p", ImVec2(200, 80));
        ImGui::Text("x");
        ess::EndSectionCard();
        CHECK(ImGui::GetItemRectSize().x == doctest::Approx(200.0f));
    });
}

TEST_CASE("Toasts stack, draw to the foreground and expire") {
    HeadlessImGui h;
    ess::ClearToasts();
    ess::PushToast("Saved", ess::ToastType::Success, 1.0f);
    ess::PushToast("Something went wrong", ess::ToastType::Error, 1.0f);
    CHECK(ess::ToastCount() == 2);
    for (int i = 0; i < 10; ++i) ess::PushToast("spam", ess::ToastType::Info, 1.0f);
    CHECK(ess::ToastCount() == 8);
    int fg_vtx = 0;
    Frame(h, [&] {
        ess::DrawToasts(ess::ToastCorner::BottomRight);
        fg_vtx = ImGui::GetForegroundDrawList()->VtxBuffer.Size;
    });
    CHECK(fg_vtx > 0);
    CHECK(ess::ToastCount() == 8);
    Frame(h, [&] {
        ImGui::GetIO().DeltaTime = 5.0f;
        ess::DrawToasts(ess::ToastCorner::TopLeft);
    });
    CHECK(ess::ToastCount() == 0);
    int empty_vtx = 1;
    Frame(h, [&] {
        ess::DrawToasts();
        empty_vtx = ImGui::GetForegroundDrawList()->VtxBuffer.Size;
    });
    CHECK(empty_vtx == 0);
}

TEST_CASE("Every widget draws headless for several frames") {
    HeadlessImGui h;
    bool t = true;
    int key = -4, tab = 1, side = 2, sel = 0;
    ImVec4 cols[4] = {ImVec4(1, 0, 0, 1), ImVec4(0, 1, 0, 1), ImVec4(0, 0, 1, 1),
                      ImVec4(1, 1, 1, 0.3f)};
    const char* tabs[] = {"Aimbot", "Visuals", "Misc", "Config"};
    ess::PushToast("hello", ess::ToastType::Warning, 2.0f);
    ImDrawData* dd = nullptr;
    for (int i = 0; i < 6; ++i) {
        h.BeginFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(640, 700));
        ImGui::Begin("all", nullptr, kWinFlags);
        ess::GradientHeader("Essentials", IM_COL32(120, 60, 200, 255), IM_COL32(40, 160, 220, 255));
        CHECK(ImGui::GetItemRectSize().x > 100.0f);
        ess::GradientHeader("Centered", ImVec4(1, 1, 1, 1), ImVec4(1, 1, 1, 0), 30.0f, true);
        CHECK(ImGui::GetItemRectSize().y == doctest::Approx(30.0f));
        ess::AnimatedTabBar("tabs", tabs, 4, &tab);
        ess::AnimatedSideTabs("side", "Aimbot\0Visuals\0Misc\0", "A\0V\0M\0", &side);
        ess::SectionCard("Combat", ImVec2(0, 0));
        ess::Toggle("Enabled", &t);
        ess::Toggle("Colored##c", &t, IM_COL32(255, 0, 0, 255), IM_COL32(40, 40, 40, 255));
        ess::Toggle("Vec4##v", &t, ImVec4(0, 1, 0, 1));
        ess::KeybindPicker("Key", &key);
        ess::ColorSwatchRow("Team", cols, 4, &sel);
        ess::HotkeyLabel("Toggle menu", key);
        CHECK(ImGui::GetItemRectSize().x > 0.0f);
        ess::Spinner("spin", 10.0f, 3.0f, 0);
        CHECK(ImGui::GetItemRectSize().x == doctest::Approx(20.0f));
        ess::GlowButton("Apply", ImVec2(0, 0), 0);
        ess::GlowButton("Vec4", ImVec2(100, 30), ImVec4(1, 1, 0, 1));
        ess::EndSectionCard();
        ess::DrawToasts(ess::ToastCorner::TopRight);
        ImGui::End();
        dd = h.EndFrame();
    }
    REQUIRE(dd != nullptr);
    CHECK(dd->TotalVtxCount > 0);
    ess::ClearToasts();
}
