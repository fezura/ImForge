#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "imgui.h"
#include "imforge/imforge_interp.h"
#include "imgui_headless.h"
#include "../packs/controls/imb_controls.h"
#include "../packs/controls/imforge_adapters_controls.h"

#ifndef IMFORGE_SOURCE_DIR
#define IMFORGE_SOURCE_DIR "."
#endif

using imforge::test::HeadlessImGui;
namespace ctl = imb::ctl;

namespace {

const ImGuiWindowFlags kWinFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;

int K(ImGuiKey k) {
#if IMGUI_VERSION_NUM >= 18700
    return (int)k;
#else
    return 256 + (int)k;
#endif
}

int Letter(char c) {
#if IMGUI_VERSION_NUM >= 18700
    return (int)(ImGuiKey_A + (c - 'A'));
#else
    return (int)c;
#endif
}

ImVec2 Mid(const ImVec2& a, const ImVec2& b) { return ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f); }

struct H {
    HeadlessImGui gui{1600.0f, 1600.0f};

    H() {
        imb::Reset();
        ctl::detail::CapturingChord() = 0;
        ctl::detail::EditState().id = 0;
        ctl::detail::Palette().id = 0;
        ImGuiIO& io = ImGui::GetIO();
#if IMGUI_VERSION_NUM >= 18700
        io.ConfigInputTrickleEventQueue = false;
#else
        for (int k = 0; k < ImGuiKey_COUNT; ++k) io.KeyMap[k] = 256 + k;
#endif
    }

    template <class F> void Frame(F&& body) {
        gui.BeginFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(900, 1500));
        ImGui::Begin("t", nullptr, kWinFlags);
        body();
        ImGui::End();
        gui.EndFrame();
    }
    template <class F> void Frames(int n, F&& body) {
        for (int i = 0; i < n; ++i) Frame(body);
    }
    void Mouse(const ImVec2& p, bool down, int button = 0) {
        ImGuiIO& io = ImGui::GetIO();
#if IMGUI_VERSION_NUM >= 18700
        io.AddMousePosEvent(p.x, p.y);
        io.AddMouseButtonEvent(button, down);
#else
        io.MousePos = p;
        io.MouseDown[button] = down;
#endif
    }
    template <class F> void Click(const ImVec2& p, F&& body, int button = 0) {
        Mouse(p, false, button);
        Frames(2, body);
        Mouse(p, true, button);
        Frames(2, body);
        Mouse(p, false, button);
        Frames(2, body);
    }
    template <class F> void Drag(const ImVec2& a, const ImVec2& b, F&& body, int steps = 10) {
        Mouse(a, false);
        Frames(2, body);
        Mouse(a, true);
        Frames(2, body);
        for (int i = 1; i <= steps; ++i) {
            float t = (float)i / (float)steps;
            Mouse(ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t), true);
            Frame(body);
        }
        Mouse(b, false);
        Frames(2, body);
    }
    void KeyState(int key, bool down) {
#if IMGUI_VERSION_NUM >= 18700
        ImGui::GetIO().AddKeyEvent((ImGuiKey)key, down);
#else
        ImGui::GetIO().KeysDown[key] = down;
#endif
    }
    template <class F> void Key(int key, F&& body) {
        KeyState(key, true);
        Frame(body);
        KeyState(key, false);
        Frames(2, body);
    }
    void Ctrl(bool down) {
#if IMGUI_VERSION_NUM >= 18900
        ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, down);
#elif IMGUI_VERSION_NUM >= 18700
        ImGui::GetIO().AddKeyEvent(ImGuiKey_ModCtrl, down);
#else
        ImGui::GetIO().KeyCtrl = down;
#endif
    }
    template <class F> void Type(const char* s, F&& body) {
        for (const char* c = s; *c; ++c) {
            ImGui::GetIO().AddInputCharacter((unsigned)*c);
            Frame(body);
        }
        Frame(body);
    }
};

}  // namespace

TEST_CASE("fuzzy score matches subsequences and ranks word starts") {
    CHECK(ctl::FuzzyScore("sf", "Save File") > 0);
    CHECK(ctl::FuzzyScore("sav", "Save File") > ctl::FuzzyScore("sav", "Undo last save"));
    CHECK(ctl::FuzzyScore("xyz", "Save File") == -1);
    CHECK(ctl::FuzzyScore("", "anything") == 0);
    CHECK(ctl::FuzzyScore("ts", "ToggleSidebar") > ctl::FuzzyScore("ts", "Tools"));
    int pos[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    CHECK(ctl::FuzzyScore("o f", "Open File", pos, 8) >= 0);
    CHECK(pos[0] == 0);
    CHECK(pos[1] == 5);
}

TEST_CASE("hex colors, chords and gradient sampling") {
    H h;
    ImVec4 c;
    REQUIRE(ctl::ParseHexColor("#ff8000", &c));
    CHECK(c.x == doctest::Approx(1.0f));
    CHECK(c.y == doctest::Approx(128.0f / 255.0f));
    CHECK(c.w == doctest::Approx(1.0f));
    CHECK(ctl::ParseHexColor("0f08", &c));
    CHECK(c.w == doctest::Approx(136.0f / 255.0f));
    CHECK_FALSE(ctl::ParseHexColor("#12", &c));
    h.Frame([&] {
        CHECK(std::strcmp(ctl::ChordName(0), "None") == 0);
        CHECK(std::strcmp(ctl::ChordName(Letter('S') | ctl::kModCtrl | ctl::kModShift), "Ctrl+Shift+S") == 0);
        CHECK(std::strcmp(ctl::ChordName(ctl::kMouseKey + 3), "Mouse4") == 0);
    });
    ctl::GradientStop g[2] = {{0.0f, ImVec4(0, 0, 0, 1)}, {1.0f, ImVec4(1, 1, 1, 1)}};
    CHECK(ctl::GradientSample(g, 2, 0.5f).x == doctest::Approx(0.5f));
    CHECK(ctl::GradientSample(g, 2, -1.0f).x == doctest::Approx(0.0f));
    CHECK(ctl::GradientSample(g, 2, 2.0f).x == doctest::Approx(1.0f));
    ctl::GradientStop s3[3] = {{0.0f, ImVec4()}, {0.8f, ImVec4()}, {0.6f, ImVec4()}};
    int track = 1;
    ctl::detail::SortStops(s3, 3, &track);
    CHECK(track == 2);
    CHECK(s3[2].pos == doctest::Approx(0.8f));
    char f[32];
    CHECK(std::strcmp(ctl::detail::IntFmt("%3d px", f, 32), "%3.0f px") == 0);
    CHECK(std::strcmp(ctl::detail::IntFmt("%i", f, 32), "%.0f") == 0);
    CHECK(std::strcmp(ctl::detail::IntFmt("100%% %02d", f, 32), "100%% %02.0f") == 0);
    CHECK(std::strcmp(ctl::detail::IntFmt("%.1f", f, 32), "%.1f") == 0);
}

TEST_CASE("every widget draws for several frames") {
    H h;
    int seg = 1, tab = 0, mask = 5, chips = 2, step = 3, tri = 2, chord = Letter('K') | ctl::kModCtrl;
    int irange[2] = {10, 60}, pal = 2, split = 0, dd = 1, cmd = -1, unit = 1;
    float lo = 0.2f, hi = 0.7f, frange[2] = {20, 80}, gain = 0.4f, q = 3, rating = 3.5f, dist = 1.25f;
    float v2[2] = {1, 2}, v3[3] = {1, 2, 3}, hue = 0.3f, sat = 0.6f, val = 0.8f;
    bool on = true, sq = false, ic = true, bold = true, palette_open = true;
    ImVec4 pick(0.2f, 0.5f, 0.9f, 0.8f), pcol(0, 0, 0, 0);
    char search[64] = "abc", tags[128] = "red,green,blue";
    std::string stags = "one,two";
    std::vector<ctl::GradientStop> grad = {{0.0f, ImVec4(1, 0, 0, 1)}, {0.6f, ImVec4(0, 1, 0, 0.5f)}, {1.0f, ImVec4(0, 0, 1, 1)}};
    float packed[20] = {0, 1, 1, 0, 1, 1, 0, 0, 1, 1, -1, 0, 0, 0, 0, -1, 0, 0, 0, 0};
    const char* items[] = {"Low", "Medium", "High"};
    const char* icons[] = {"L", "", "H"};
    const char* cmds[] = {"New File", "Open File", "Save File"};
    const char* hints[] = {"Ctrl+N", "Ctrl+O"};
    const char* hexes[] = {"#e74c3c", "#2ecc71", "#3498db", "#f1c40f80"};
    ctl::DragUnit units[] = {{"m", 1.0f, 0.01f, "%.2f"}, {"cm", 0.01f, 1.0f, "%.0f"}};
    ImDrawData* dd_out = nullptr;
    for (int i = 0; i < 4; ++i) {
        h.gui.BeginFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(900, 1500));
        ImGui::Begin("all", nullptr, kWinFlags);
        ctl::SegmentedControl("Mode", &seg, items, 3);
        ctl::SegmentedControl("Fill##f", &seg, "A\0B\0C\0D\0", ctl::SegmentedStyle());
        ctl::SegmentedControl("Wide##w", &seg, items, 3, -1.0f);
        ctl::PillTabs("##pills", &tab, "Overview\0Stats\0Settings\0");
        ctl::ToggleButtonGroup("Style", &mask, "B\0I\0U\0");
        ctl::Chips("##chips", &chips, items, 3);
        ctl::RangeSlider("Range", &lo, &hi, 0.0f, 1.0f);
        ctl::RangeSlider("Float2", frange, 0.0f, 100.0f, "%.0f");
        ctl::RangeSlider("Ints", irange, 0, 100);
        ctl::Knob("Gain", &gain, 0.0f, 1.0f);
        ctl::KnobStyle bip;
        bip.bipolar = true;
        ctl::Knob("Pan", &gain, 0.0f, 1.0f, "%.1f", bip);
        ctl::Knob("Steps", &step, 0, 10);
        ctl::TickSlider("Quality", &q, 0.0f, 10.0f, 11);
        ctl::TickSlider("Level", &step, 0, 5);
        ctl::Stepper("Count", &step, 0, 10);
        ctl::Stepper("Float", &gain, 0.0f, 1.0f, 0.1f, "%.1f");
        ctl::SwitchToggle("Ios", &on);
        ctl::SwitchToggle("Square", &sq, ctl::SwitchKind::Square);
        ctl::SwitchToggle("Icons", &ic, ctl::SwitchKind::Icons);
        ctl::IconToggle("B##bold", &bold);
        ctl::IconToggle("Eye", &bold, "O", "-");
        ctl::TriCheckbox("All", &tri);
        ctl::RatingStars("Rating", &rating, 5, 0.5f);
        ctl::RatingStars("IntRating", &step, 5);
        ctl::SearchField("##search", search, sizeof(search));
        ctl::TagInput("Tags", tags, sizeof(tags));
        ctl::TagInput("StrTags", &stags);
        ctl::KeyChordPicker("Shortcut", &chord);
        ctl::DragNumber("Speed", &gain, 0.01f, 0.0f, 1.0f, "%.2f", "m/s");
        ctl::DragNumber("Dist", &dist, units, 2, &unit);
        ctl::Vec2Edit("Pos", v2);
        ctl::Vec3Edit("Rot", v3, 1.0f, -180.0f, 180.0f, "%.0f");
        ctl::ColorPalette("Team", &pal, &pcol, hexes, 4, 2);
        ctl::HueStrip("##hue", &hue);
        ctl::HueStripStyle vhs;
        vhs.vertical = true;
        ctl::HueStrip("##vhue", &hue, vhs);
        ctl::SatValBox("##sv", hue, &sat, &val, ImVec2(80, 80));
        ctl::ColorPickerMini("Pick", &pick);
        ctl::GradientEditor("Gradient", &grad);
        ctl::GradientEditorPacked("Packed", packed, 20);
        ctl::SplitButton("##split", &split, "Save\0Save As\0Save All\0");
        ctl::IconDropdown("Mode##dd", &dd, items, 3, icons, 3);
        ctl::CommandPalette("##cmd", &palette_open, cmds, 3, &cmd, hints, 2);
        CHECK(ImGui::GetItemRectSize().x >= 0.0f);
        ImGui::End();
        dd_out = h.gui.EndFrame();
    }
    REQUIRE(dd_out != nullptr);
    CHECK(dd_out->TotalVtxCount > 1000);
    CHECK(palette_open);
}

TEST_CASE("segmented control and pill tabs pick the clicked item and follow arrows") {
    H h;
    int idx = 0, tab = 0, changes = 0, tab_changes = 0;
    const char* items[] = {"One", "Two", "Three"};
    ImVec2 mn, mx, tmn, tmx;
    auto body = [&] {
        if (ctl::SegmentedControl("##seg", &idx, items, 3)) ++changes;
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
        if (ctl::PillTabs("##pills", &tab, items, 3)) ++tab_changes;
        tmn = ImGui::GetItemRectMin();
        tmx = ImGui::GetItemRectMax();
    };
    h.Frames(2, body);
    float w = mx.x - mn.x;
    h.Click(ImVec2(mn.x + w * 5.0f / 6.0f, Mid(mn, mx).y), body);
    CHECK(idx == 2);
    CHECK(changes == 1);
    h.Key(K(ImGuiKey_LeftArrow), body);
    CHECK(idx == 1);
    CHECK(changes == 2);
    h.Click(ImVec2(mn.x + w / 6.0f, Mid(mn, mx).y), body);
    CHECK(idx == 0);
    h.Click(ImVec2(tmx.x - 10.0f, Mid(tmn, tmx).y), body);
    CHECK(tab == 2);
    CHECK(tab_changes == 1);
}

TEST_CASE("toggle button group, chips and switches flip on click") {
    H h;
    int mask = 0, chips = 0;
    bool sw = false, ic = false;
    const char* items[] = {"Bold", "Italic", "Under"};
    ImVec2 gmn, cmn, cmx, smn, smx, imn, imx;
    auto body = [&] {
        ctl::ToggleButtonGroup("##tbg", &mask, items, 3);
        gmn = ImGui::GetItemRectMin();
        ctl::Chips("##chips", &chips, items, 3);
        cmn = ImGui::GetItemRectMin();
        cmx = ImGui::GetItemRectMax();
        ctl::SwitchToggle("Switch", &sw);
        smn = ImGui::GetItemRectMin();
        smx = ImGui::GetItemRectMax();
        ctl::IconToggle("I##it", &ic);
        imn = ImGui::GetItemRectMin();
        imx = ImGui::GetItemRectMax();
    };
    h.Frames(2, body);
    float w0 = ImGui::CalcTextSize("Bold").x + ImGui::GetStyle().FramePadding.x * 2.6f;
    float cy = gmn.y + ImGui::GetFrameHeight() * 0.5f;
    h.Click(ImVec2(gmn.x + 5, cy), body);
    CHECK(mask == 1);
    h.Click(ImVec2(gmn.x + w0 + 5, cy), body);
    CHECK(mask == 3);
    h.Click(ImVec2(gmn.x + 5, cy), body);
    CHECK(mask == 2);
    h.Click(ImVec2(cmn.x + 8, Mid(cmn, cmx).y), body);
    CHECK(chips == 1);
    h.Click(ImVec2(cmn.x + 8, Mid(cmn, cmx).y), body);
    CHECK(chips == 0);
    h.Click(Mid(smn, smx), body);
    CHECK(sw);
    h.Click(Mid(imn, imx), body);
    CHECK(ic);
}

TEST_CASE("knob drags up, resets on double click and steps with arrows") {
    H h;
    float v = 0.25f;
    int changes = 0;
    ctl::KnobStyle ks;
    ks.default_value = 0.1f;
    ks.show_label = false;
    ImVec2 mn, mx;
    auto body = [&] {
        if (ctl::Knob("##knob", &v, 0.0f, 1.0f, "%.2f", ks)) ++changes;
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
    };
    h.Frames(2, body);
    float rad = ImGui::GetFrameHeight() * 1.15f;
    ImVec2 c(Mid(mn, mx).x, mn.y + rad);
    h.Drag(c, ImVec2(c.x, c.y - 100.0f), body);
    CHECK(v == doctest::Approx(0.75f).epsilon(0.02));
    CHECK(changes > 0);
    h.Mouse(c, false);
    h.Frame(body);
    h.Mouse(c, true);
    h.Frame(body);
    h.Mouse(c, false);
    h.Frame(body);
    h.Mouse(c, true);
    h.Frame(body);
    h.Mouse(c, false);
    h.Frames(2, body);
    CHECK(v == doctest::Approx(0.1f));
    h.Key(K(ImGuiKey_UpArrow), body);
    CHECK(v == doctest::Approx(0.12f));
}

TEST_CASE("range slider moves the nearest thumb and keeps lo <= hi") {
    H h;
    float lo = 20, hi = 80;
    int ir[2] = {2, 8};
    ctl::RangeSliderStyle rs;
    rs.show_values = false;
    rs.show_label = false;
    ImVec2 mn, mx, imn, imx;
    auto body = [&] {
        ctl::RangeSlider("##r", &lo, &hi, 0.0f, 100.0f, "%.0f", rs);
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
        ctl::RangeSlider("##ri", ir, 0, 10, "%d", rs);
        imn = ImGui::GetItemRectMin();
        imx = ImGui::GetItemRectMax();
    };
    h.Frames(2, body);
    float r = std::fmax(6.0f, ImGui::GetFrameHeight() * 0.34f);
    auto X = [&](float t) { return mn.x + r + (mx.x - mn.x - 2.0f * r) * t; };
    float cy = Mid(mn, mx).y;
    h.Drag(ImVec2(X(0.2f), cy), ImVec2(X(0.5f), cy), body);
    CHECK(lo == doctest::Approx(50.0f).epsilon(0.02));
    CHECK(hi == doctest::Approx(80.0f));
    h.Click(ImVec2(X(0.9f), cy), body);
    CHECK(hi == doctest::Approx(90.0f).epsilon(0.02));
    h.Drag(ImVec2(X(0.9f), cy), ImVec2(X(0.3f), cy), body);
    CHECK(hi == doctest::Approx(lo));
    CHECK(lo <= hi);
    auto IX = [&](float t) { return imn.x + r + (imx.x - imn.x - 2.0f * r) * t; };
    h.Drag(ImVec2(IX(0.8f), Mid(imn, imx).y), ImVec2(IX(1.0f), Mid(imn, imx).y), body);
    CHECK(ir[0] == 2);
    CHECK(ir[1] == 10);
}

TEST_CASE("stepper repeats while held and clamps") {
    H h;
    int n = 0;
    ctl::StepperStyle ss;
    ss.show_label = false;
    ImVec2 mn, mx;
    auto body = [&] {
        ctl::Stepper("##st", &n, 0, 100, 1, ss);
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
    };
    h.Frames(2, body);
    float fh = ImGui::GetFrameHeight();
    ImVec2 plus(mx.x - fh * 0.5f, Mid(mn, mx).y), minus(mn.x + fh * 0.5f, Mid(mn, mx).y);
    h.Mouse(plus, false);
    h.Frames(2, body);
    h.Mouse(plus, true);
    h.Frames(60, body);
    h.Mouse(plus, false);
    h.Frames(2, body);
    CHECK(n > 5);
    CHECK(n < 40);
    int held = n;
    h.Click(minus, body);
    CHECK(n == held - 1);
    n = 100;
    h.Click(plus, body);
    CHECK(n == 100);
}

TEST_CASE("tick slider snaps to ticks and drag number scrubs and accepts typed values") {
    H h;
    float q = 0, d = 10;
    ctl::TickSliderStyle ts;
    ts.show_label = false;
    ImVec2 tmn, tmx, dmn, dmx;
    auto body = [&] {
        ctl::TickSlider("##ticks", &q, 0.0f, 10.0f, 11, "%.0f", ts);
        tmn = ImGui::GetItemRectMin();
        tmx = ImGui::GetItemRectMax();
        ctl::DragNumber("##num", &d, 0.5f, 0.0f, 0.0f, "%.2f", "px");
        dmn = ImGui::GetItemRectMin();
        dmx = ImGui::GetItemRectMax();
    };
    h.Frames(2, body);
    float r = std::fmax(6.0f, ImGui::GetFrameHeight() * 0.34f);
    float x0 = tmn.x + r, x1 = tmx.x - r, cy = Mid(tmn, tmx).y;
    h.Drag(ImVec2(x0, cy), ImVec2(x0 + (x1 - x0) * 0.43f, cy), body);
    CHECK(q == doctest::Approx(4.0f));
    h.Key(K(ImGuiKey_RightArrow), body);
    CHECK(q == doctest::Approx(5.0f));
    ImVec2 c = Mid(dmn, dmx);
    h.Drag(c, ImVec2(c.x + 40.0f, c.y), body);
    CHECK(d > 20.0f);
    CHECK(d < 31.0f);
    h.Click(c, body);
    h.Frames(3, body);
    h.Key(K(ImGuiKey_End), body);
    for (int i = 0; i < 8; ++i) h.Key(K(ImGuiKey_Backspace), body);
    h.Type("7", body);
    h.Key(K(ImGuiKey_Enter), body);
    CHECK(d == doctest::Approx(7.0f));
}

TEST_CASE("drag number units and vec3 axes") {
    H h;
    float dist = 1.0f, v[3] = {1, 2, 3};
    int unit = 1;
    ctl::DragUnit units[] = {{"m", 1.0f, 0.01f, "%.2f"}, {"cm", 0.01f, 1.0f, "%.0f"}};
    ImVec2 dmn, dmx, vmn, vmx;
    auto body = [&] {
        ctl::DragNumber("##dist", &dist, units, 2, &unit);
        dmn = ImGui::GetItemRectMin();
        dmx = ImGui::GetItemRectMax();
        ctl::Vec3Edit("##rot", v);
        vmn = ImGui::GetItemRectMin();
        vmx = ImGui::GetItemRectMax();
    };
    h.Frames(2, body);
    ImVec2 c(dmn.x + 30.0f, Mid(dmn, dmx).y);
    h.Drag(c, ImVec2(c.x + 30.0f, c.y), body);
    CHECK(dist > 1.1f);
    CHECK(dist < 1.35f);
    float fw = (vmx.x - vmn.x - ImGui::GetStyle().ItemInnerSpacing.x * 2.0f) / 3.0f;
    ImVec2 y(vmn.x + fw * 1.5f + ImGui::GetStyle().ItemInnerSpacing.x, Mid(vmn, vmx).y);
    h.Drag(y, ImVec2(y.x + 50.0f, y.y), body);
    CHECK(v[0] == doctest::Approx(1.0f));
    CHECK(v[1] > 4.0f);
    CHECK(v[2] == doctest::Approx(3.0f));
}

TEST_CASE("rating stars, tri checkbox and color palette") {
    H h;
    float rating = 0;
    int tri = 2, cyc = 0, sel = 0;
    ImVec4 col(0, 0, 0, 0);
    ImVec4 cols[3] = {ImVec4(1, 0, 0, 1), ImVec4(0, 1, 0, 1), ImVec4(0, 0, 1, 1)};
    ctl::TriCheckboxStyle cs;
    cs.cycle_mixed = true;
    ImVec2 rmn, rmx, tmn, tmx, cmn, cmx, pmn;
    auto body = [&] {
        ctl::RatingStars("##stars", &rating, 5, 0.5f);
        rmn = ImGui::GetItemRectMin();
        rmx = ImGui::GetItemRectMax();
        ctl::TriCheckbox("Tri", &tri);
        tmn = ImGui::GetItemRectMin();
        tmx = ImGui::GetItemRectMax();
        ctl::TriCheckbox("Cycle", &cyc, cs);
        cmn = ImGui::GetItemRectMin();
        cmx = ImGui::GetItemRectMax();
        ctl::ColorPalette("##pal", &sel, &col, cols, 3);
        pmn = ImGui::GetItemRectMin();
    };
    h.Frames(2, body);
    float s = ImGui::GetFrameHeight(), gap = 2.0f;
    float sy = Mid(rmn, rmx).y;
    h.Click(ImVec2(rmn.x + 3.0f * (s + gap) + s * 0.75f, sy), body);
    CHECK(rating == doctest::Approx(4.0f));
    h.Click(ImVec2(rmn.x + 3.0f * (s + gap) + s * 0.75f, sy), body);
    CHECK(rating == doctest::Approx(0.0f));
    h.Click(ImVec2(rmn.x + 2.0f * (s + gap) + s * 0.25f, sy), body);
    CHECK(rating == doctest::Approx(2.5f));
    h.Click(Mid(tmn, tmx), body);
    CHECK(tri == 1);
    h.Click(Mid(tmn, tmx), body);
    CHECK(tri == 0);
    for (int expect : {1, 2, 0}) {
        h.Click(Mid(cmn, cmx), body);
        CHECK(cyc == expect);
    }
    float sz = ImGui::GetFrameHeight(), sg = ImGui::GetStyle().ItemInnerSpacing.x;
    h.Click(ImVec2(pmn.x + sz + sg + sz * 0.5f, pmn.y + sz * 0.5f), body);
    CHECK(sel == 1);
    CHECK(col.y == doctest::Approx(1.0f));
}

TEST_CASE("hue strip and sat/val box follow the mouse") {
    H h;
    float hue = 0, s = 0, v = 0;
    ImVec2 hmn, hmx, smn;
    auto body = [&] {
        ctl::HueStrip("##hue", &hue);
        hmn = ImGui::GetItemRectMin();
        hmx = ImGui::GetItemRectMax();
        ctl::SatValBox("##sv", hue, &s, &v, ImVec2(100, 100));
        smn = ImGui::GetItemRectMin();
    };
    h.Frames(2, body);
    h.Click(ImVec2(hmn.x + (hmx.x - hmn.x) * 0.5f, Mid(hmn, hmx).y), body);
    CHECK(hue == doctest::Approx(0.5f).epsilon(0.02));
    h.Click(ImVec2(smn.x + 25.0f, smn.y + 25.0f), body);
    CHECK(s == doctest::Approx(0.25f).epsilon(0.02));
    CHECK(v == doctest::Approx(0.75f).epsilon(0.02));
}

TEST_CASE("gradient editor adds, moves and removes stops") {
    H h;
    ctl::GradientStop stops[8] = {{0.0f, ImVec4(0, 0, 0, 1)}, {1.0f, ImVec4(1, 1, 1, 1)}};
    int count = 2, changes = 0;
    ctl::GradientStyle gs;
    gs.show_controls = false;
    ImVec2 mn, mx;
    auto body = [&] {
        if (ctl::GradientEditor("##grad", stops, &count, 8, gs)) ++changes;
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
    };
    h.Frames(2, body);
    float fh = ImGui::GetFrameHeight(), hs = std::fmax(10.0f, fh * 0.6f);
    float x0 = mn.x + hs * 0.5f, x1 = mx.x - hs * 0.5f;
    float hy = mn.y + fh + hs * 0.8f;
    auto X = [&](float t) { return x0 + (x1 - x0) * t; };
    h.Click(ImVec2(X(0.5f), hy), body);
    REQUIRE(count == 3);
    CHECK(stops[1].pos == doctest::Approx(0.5f).epsilon(0.02));
    CHECK(stops[1].color.x == doctest::Approx(0.5f).epsilon(0.05));
    h.Frames(25, body);
    h.Drag(ImVec2(X(0.5f), hy), ImVec2(X(0.25f), hy), body);
    CHECK(count == 3);
    CHECK(stops[1].pos == doctest::Approx(0.25f).epsilon(0.02));
    h.Click(ImVec2(X(0.25f), hy), body, 1);
    CHECK(count == 2);
    h.Frames(25, body);
    h.Click(ImVec2(X(0.7f), hy), body);
    REQUIRE(count == 3);
    h.Frames(25, body);
    h.Drag(ImVec2(X(0.7f), hy), ImVec2(X(0.7f), hy + 120.0f), body);
    CHECK(count == 2);
    CHECK(stops[0].pos == doctest::Approx(0.0f));
    CHECK(stops[1].pos == doctest::Approx(1.0f));
    CHECK(changes > 0);
    float packed[15] = {0, 1, 0, 0, 1, 1, 0, 0, 1, 1, -1, 0, 0, 0, 0};
    auto pbody = [&] {
        ctl::GradientEditorPacked("##packed", packed, 15, gs);
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
    };
    h.Frames(2, pbody);
    h.Click(ImVec2(X(0.5f), hy), pbody);
    CHECK(packed[5] == doctest::Approx(0.5f).epsilon(0.02));
    CHECK(packed[10] == doctest::Approx(1.0f));
}

TEST_CASE("search field clears, types and escapes") {
    H h;
    char buf[32] = "abc";
    int changes = 0;
    ImVec2 mn, mx;
    auto body = [&] {
        if (ctl::SearchField("##s", buf, sizeof(buf))) ++changes;
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
    };
    h.Frames(2, body);
    float fh = ImGui::GetFrameHeight();
    h.Click(ImVec2(mx.x - fh * 0.5f, Mid(mn, mx).y), body);
    CHECK(buf[0] == 0);
    CHECK(changes == 1);
    h.Click(ImVec2(mn.x + fh * 2.0f, Mid(mn, mx).y), body);
    h.Type("xy", body);
    CHECK(std::strcmp(buf, "xy") == 0);
    CHECK(changes >= 3);
    h.Key(K(ImGuiKey_Escape), body);
    CHECK(buf[0] == 0);
}

TEST_CASE("tag input adds on enter and separator, removes with x and backspace") {
    H h;
    char tags[128] = "red";
    ImVec2 mn, mx;
    auto body = [&] {
        ctl::TagInput("##tags", tags, sizeof(tags));
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
    };
    h.Frames(2, body);
    float fh = ImGui::GetFrameHeight();
    h.Click(ImVec2(mn.x + (mx.x - mn.x) * 0.8f, mn.y + fh * 0.5f), body);
    h.Type("blue", body);
    h.Key(K(ImGuiKey_Enter), body);
    CHECK(std::strcmp(tags, "red,blue") == 0);
    h.Frames(2, body);
    h.Type("green,", body);
    CHECK(std::strcmp(tags, "red,blue,green") == 0);
    h.Type("red,", body);
    CHECK(std::strcmp(tags, "red,blue,green") == 0);
    h.Key(K(ImGuiKey_Backspace), body);
    CHECK(std::strcmp(tags, "red,blue") == 0);
    float tag_h = fh - 4.0f, xw = tag_h * 0.8f, pad = 2.0f;
    float tw = ImGui::CalcTextSize("red").x + tag_h * 0.5f + xw;
    h.Click(ImVec2(mn.x + pad + tw - xw * 0.5f, mn.y + pad + tag_h * 0.5f), body);
    CHECK(std::strcmp(tags, "blue") == 0);
    CHECK(ctl::TagCount(tags) == 1);
}

TEST_CASE("key chord picker captures modifiers, cancels and clears") {
    H h;
    int chord = 0, changes = 0, hits = 0;
    ImVec2 mn, mx;
    auto body = [&] {
        if (ctl::KeyChordPicker("##chord", &chord)) ++changes;
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
        if (!ctl::IsCapturingChord() && ctl::IsChordPressed(chord)) ++hits;
    };
    h.Frames(2, body);
    h.Click(Mid(mn, mx), body);
    REQUIRE(ctl::IsCapturingChord());
    h.Ctrl(true);
    h.Frames(2, body);
    h.Key(Letter('F'), body);
    h.Ctrl(false);
    h.Frames(2, body);
    CHECK_FALSE(ctl::IsCapturingChord());
    CHECK(chord == (Letter('F') | ctl::kModCtrl));
    CHECK(changes == 1);
    CHECK(std::strcmp(ctl::ChordName(chord), "Ctrl+F") == 0);
    int hits_before = hits;
    h.Key(Letter('F'), body);
    CHECK(hits == hits_before);
    h.Ctrl(true);
    h.Frame(body);
    h.Key(Letter('F'), body);
    h.Ctrl(false);
    h.Frame(body);
    CHECK(hits == hits_before + 1);
    h.Click(Mid(mn, mx), body);
    REQUIRE(ctl::IsCapturingChord());
    h.Key(K(ImGuiKey_Escape), body);
    CHECK_FALSE(ctl::IsCapturingChord());
    CHECK(chord == (Letter('F') | ctl::kModCtrl));
    h.Click(Mid(mn, mx), body);
    REQUIRE(ctl::IsCapturingChord());
    h.Click(ImVec2(mx.x + 300.0f, mx.y + 300.0f), body);
    CHECK_FALSE(ctl::IsCapturingChord());
    CHECK(chord == (Letter('F') | ctl::kModCtrl));
    h.Click(Mid(mn, mx), body);
    h.Click(Mid(mn, mx), body, 1);
    CHECK(chord == ctl::kMouseKey + 1);
    h.Click(Mid(mn, mx), body);
    h.Key(K(ImGuiKey_Backspace), body);
    CHECK(chord == 0);
    CHECK(changes == 3);
}

TEST_CASE("command palette filters, moves with arrows and runs on enter") {
    H h;
    bool open = true;
    int sel = -1, picks = 0;
    const char* items[] = {"New File", "Open File", "Save File", "Toggle Sidebar", "Settings"};
    auto body = [&] {
        if (ctl::CommandPalette("##cmd", &open, items, 5, &sel)) ++picks;
    };
    h.Frames(4, body);
    h.Type("sav", body);
    h.Key(K(ImGuiKey_Enter), body);
    CHECK(picks == 1);
    CHECK(sel == 2);
    CHECK_FALSE(open);
    h.Frames(2, body);
    open = true;
    h.Frames(4, body);
    h.Type("file", body);
    h.Key(K(ImGuiKey_DownArrow), body);
    h.Key(K(ImGuiKey_Enter), body);
    CHECK(picks == 2);
    CHECK(sel == 1);
    open = true;
    h.Frames(4, body);
    h.Type("zzz", body);
    h.Key(K(ImGuiKey_Enter), body);
    CHECK(picks == 2);
    CHECK(open);
    h.Key(K(ImGuiKey_Escape), body);
    CHECK_FALSE(open);
}

TEST_CASE("split button and icon dropdown pick from their lists") {
    H h;
    int split = 0, clicks = 0, cur = 0, changes = 0;
    const char* items[] = {"Rifle", "Pistol", "Knife"};
    const char* icons[] = {"R", "P", "K"};
    ImVec2 smn, smx, dmn, dmx;
    auto body = [&] {
        if (ctl::SplitButton("##split", &split, items, 3)) ++clicks;
        smn = ImGui::GetItemRectMin();
        smx = ImGui::GetItemRectMax();
        ImGui::Dummy(ImVec2(10, 120));
        if (ctl::IconDropdown("##dd", &cur, items, 3, icons)) ++changes;
        dmn = ImGui::GetItemRectMin();
        dmx = ImGui::GetItemRectMax();
    };
    h.Frames(2, body);
    float fh = ImGui::GetFrameHeight();
    h.Click(ImVec2(smn.x + 6, Mid(smn, smx).y), body);
    CHECK(clicks == 1);
    CHECK(split == 0);
    h.Click(ImVec2(smx.x - fh * 0.5f, Mid(smn, smx).y), body);
    h.Frames(12, body);
    h.Click(ImVec2(smn.x + 20, smx.y + 6.0f + (fh + 1.0f) + fh * 0.5f), body);
    CHECK(split == 1);
    CHECK(clicks == 2);
    h.Click(Mid(dmn, dmx), body);
    h.Frames(12, body);
    h.Click(ImVec2(dmn.x + 20, dmx.y + 6.0f + 2.0f * (fh + 1.0f) + fh * 0.5f), body);
    CHECK(cur == 2);
    CHECK(changes == 1);
}

#include "controls_tests_interp.h"
