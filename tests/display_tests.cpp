#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "imgui_headless.h"
#include "../packs/display/imforge_adapters_display.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

struct DispState {
    int step = 2;
    float time = 3.0f;
    float vals[8] = {1, 3, 2, 5, 4, 6, 2, 7};
    std::vector<float> series = {1, 2, 3, 2.5f, 4, 3.5f};
    std::vector<float> marks = {5.0f, 10.0f};
    int crumb = -1;
    float load = 0.3f;
    ImTextureID tex = ImTextureID();
};
IMFORGE_REFLECT(DispState, step, time, vals, series, marks, crumb, load, tex);

using imforge::test::HeadlessImGui;
namespace disp = imb::disp;

namespace {

const ImGuiWindowFlags kWinFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoSavedSettings;

struct Gui {
    HeadlessImGui h;
    Gui() {
        imb::Reset();
        disp::ClearToasts();
#if IMGUI_VERSION_NUM >= 18700
        ImGui::GetIO().ConfigInputTrickleEventQueue = false;
#endif
    }
    ~Gui() {
        disp::ClearToasts();
        imb::Reset();
    }
    template <class F> void Frame(F&& body, ImVec2 size = ImVec2(900, 2400)) {
        h.BeginFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(size);
        ImGui::Begin("t", nullptr, kWinFlags);
        body();
        ImGui::End();
        h.EndFrame();
    }
    template <class F> void Frames(int n, F&& body) {
        for (int i = 0; i < n; ++i) Frame(body);
    }
};

void Mouse(const ImVec2& pos, bool down, int button = 0) {
    ImGuiIO& io = ImGui::GetIO();
#if IMGUI_VERSION_NUM >= 18700
    io.AddMousePosEvent(pos.x, pos.y);
    io.AddMouseButtonEvent(button, down);
#else
    io.MousePos = pos;
    io.MouseDown[button] = down;
#endif
}

template <class F> void Click(Gui& g, const ImVec2& pos, F&& body) {
    Mouse(pos, false);
    g.Frames(2, body);
    Mouse(pos, true);
    g.Frames(2, body);
    Mouse(pos, false);
    g.Frames(2, body);
}

ImVec2 Mid(const ImVec2& a, const ImVec2& b) { return ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f); }

ImTextureID FakeTexture() {
    return imforge::interp_widgets::TexFromInt<ImTextureID>(7, std::is_pointer<ImTextureID>());
}

std::string Root() {
#ifdef IMFORGE_SOURCE_DIR
    return IMFORGE_SOURCE_DIR;
#else
    std::string f = __FILE__;
    size_t p = f.find_last_of("/\\");
    f = p == std::string::npos ? std::string(".") : f.substr(0, p);
    return f + "/..";
#endif
}

bool ReadFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool LoadJson(const std::string& rel, imforge::Json& out) {
    std::string text;
    if (!ReadFile(Root() + "/" + rel, text)) return false;
    return imforge::ParseJson(text, out);
}

std::vector<std::string> ManifestTypes() {
    std::vector<std::string> out;
    imforge::Json m;
    if (!LoadJson("packs/display/manifest.json", m)) return out;
    const imforge::Json& w = m["widgets"];
    for (size_t i = 0; i < w.Size(); ++i) {
        std::string path = w[i].AsString();
        size_t a = path.find_last_of('/'), b = path.find(".imw.json");
        if (a != std::string::npos && b != std::string::npos) out.push_back(path.substr(a + 1, b - a - 1));
    }
    return out;
}

bool LoadCatalog(imforge::WidgetCatalog& cat) {
    for (const std::string& t : ManifestTypes()) {
        imforge::Json j;
        if (!LoadJson("packs/display/widgets/" + t + ".imw.json", j)) return false;
        imforge::Error err;
        if (!cat.AddManifest(j, &err)) return false;
    }
    return true;
}

}  // namespace

TEST_CASE("every widget draws for a few frames") {
    Gui g;
    static const float series[] = {3, 4, 3.5f, 5, 6.5f, 6, 7.2f, 6.8f, 8, 7.4f, 9};
    const int n = IM_ARRAYSIZE(series);
    std::vector<float> vec(series, series + n);
    const char* steps[] = {"Account", "Profile", "Done"};
    const char* crumbs[] = {"Home", "Configs", "Legit"};
    const float markers[] = {2.0f, 6.0f};
    disp::GaugeZone zones[] = {{0, 50, IM_COL32(0, 200, 0, 255)}, {50, 100, IM_COL32(200, 0, 0, 255)}};
    disp::FrameSamples fs;
    for (int i = 0; i < 300; ++i) fs.Push(10.0f + (float)(i % 7) * 2.0f);
    float t = 4.0f;
    int step = 1;
    disp::PushToast("Saved", "Config written to disk", disp::ToastKind::Success);
    disp::PushToast("Careful", "Long running task", disp::ToastKind::Warning);
    int up = disp::PushToast("Uploading", nullptr, disp::ToastKind::Info);
    disp::SetToastProgress(up, 0.3f);
    disp::PushToast(nullptr, "Body only", disp::ToastKind::Error);
    g.Frames(4, [&] {
        disp::RingProgress("Load", 0.4f);
        disp::RingStyle rs;
        rs.color2 = IM_COL32(255, 0, 128, 255);
        disp::RingProgress("##spin", -1.0f, rs);
        disp::RingProgress("##grad", 1.0f, rs);
        disp::RadialGauge("Speed", 62.0f, 0.0f, 100.0f);
        disp::RadialGauge("Temp", 80.0f, 0.0f, 100.0f, 60.0f, 85.0f);
        disp::GaugeStyle gs;
        gs.zones = zones;
        gs.zone_count = 2;
        gs.sweep = 180.0f;
        disp::RadialGauge("##half", 30.0f, 0.0f, 100.0f, gs);
        disp::Sparkline("##s", series, n);
        disp::Sparkline("##v", vec);
        disp::Sparkline("##e", (const float*)nullptr, 0);
        disp::Sparkline("##one", series, 1);
        disp::MiniBars("##b", series, n, steps, 3);
        disp::MiniBars("##be", (const float*)nullptr, 0);
        disp::FrameGraph("Frame", fs);
        disp::FrameGraph("Live");
        disp::FrameGraphStyle line;
        line.bars = false;
        line.budget2_ms = 1000.0f / 144.0f;
        disp::FrameGraph("Arr", series, n, 3, line);
        disp::LoadingBar("##l", 0.5f);
        disp::LoadingBar("##i", -1.0f);
        disp::LoadingBar("##seg", 0.6f, 5);
        disp::LoadingBar("##segi", -1.0f, 5);
        disp::SkeletonBlock(disp::SkeletonShape::Rect);
        disp::SkeletonBlock(disp::SkeletonShape::Line, ImVec2(0, 0), 3);
        disp::SkeletonBlock(disp::SkeletonShape::Circle);
        disp::Badge("NEW");
        disp::BadgeStyle subtle;
        subtle.subtle = true;
        disp::Badge("beta", subtle);
        disp::BadgeCount("##c", 120);
        disp::BadgeDot("##d");
        ImGui::Button("Inbox");
        disp::BadgeStyle att;
        att.attach = true;
        disp::BadgeCount("##attached", 4, att);
        disp::Avatar("Ada Lovelace");
        disp::AvatarStyle as;
        as.status = disp::Status::Busy;
        as.ring = true;
        disp::Avatar("Grace Hopper", as);
        as.texture = FakeTexture();
        as.ring = false;
        as.square = true;
        disp::Avatar("Tex", as);
        disp::StatusDot(disp::Status::Online);
        disp::StatusDot(disp::Status::Away, "Back soon");
        disp::StatusDot(disp::Status::Busy);
        disp::StatusDot(disp::Status::Offline, "##quiet");
        disp::StatusDot(disp::Status::None);
        disp::StatCardStyle cs;
        cs.show_delta = true;
        cs.delta = 12.5f;
        cs.spark = series;
        cs.spark_count = n;
        cs.icon = "$";
        disp::StatCard("Revenue", 12840.0f, cs);
        disp::StatCard("Errors", 3.0f, "%.0f", true, -4.0f, nullptr, nullptr);
        disp::ProgressSteps("##steps", steps, 3, &step);
        disp::ProgressSteps("##z", "One\0Two\0", &step);
        disp::TimelineScrubber("##tl", &t, 10.0f, markers, 2);
        disp::TimelineScrubber("##tl2", &t, 4000.0f, nullptr, nullptr);
        disp::Breadcrumbs("##bc", crumbs, 3);
        disp::KeyOverlay("##keys");
        disp::Divider();
        disp::Divider("Section");
        disp::Divider("Left", disp::DividerLook::Fade, disp::DividerAlign::Left);
        disp::Divider("Right", disp::DividerLook::Dashed, disp::DividerAlign::Right);
        disp::Divider(nullptr, disp::DividerLook::Dotted);
        disp::Divider("", disp::DividerLook::Fade);
        disp::Kbd("Ctrl+Shift+P");
        disp::Kbd("Ctrl++");
        disp::KbdStyle pressed;
        pressed.pressed = true;
        disp::Kbd("F5", pressed);
        disp::EmptyState("Nothing here", "Some hint text that wraps across a couple of lines in the empty state",
                         "Create");
        disp::EmptyState("Search", nullptr, nullptr, disp::EmptyIcon::Search);
        disp::EmptyState("##folder", "Folder", nullptr, disp::EmptyIcon::Folder);
        disp::EmptyState("Alert", "", "", disp::EmptyIcon::Alert);
        disp::EmptyStateStyle es;
        es.icon = disp::EmptyIcon::Check;
        disp::EmptyState("Done", "All caught up", nullptr, es);
        disp::TooltipCardStyle ts;
        ts.header = IM_COL32(120, 80, 255, 255);
        ts.footer = "Ctrl+A";
        ts.image = FakeTexture();
        disp::TooltipCardInline("Title", "Body text for the card, long enough to wrap onto a second line.", ts);
        disp::TooltipCardInline("Plain", nullptr);
        ImGui::Button("hover me");
        disp::TooltipCard("Tip", "text");
        if (disp::BeginTooltipCard("Custom", ts)) {
            ImGui::TextUnformatted("free-form");
            disp::EndTooltipCard();
        }
        disp::ToastStack();
    });
    CHECK(g.h.frames == 4);
    CHECK(disp::ToastCount() == 4);
}

TEST_CASE("toast stack corners all draw") {
    Gui g;
    disp::PushToast("One", "first");
    disp::PushToast("Two", "second", disp::ToastKind::Error);
    const disp::ToastCorner corners[] = {disp::ToastCorner::TopLeft,    disp::ToastCorner::TopRight,
                                         disp::ToastCorner::BottomLeft, disp::ToastCorner::BottomRight,
                                         disp::ToastCorner::TopCenter,  disp::ToastCorner::BottomCenter};
    for (disp::ToastCorner c : corners) g.Frame([&] { disp::ToastStack(c); });
    CHECK(disp::ToastCount() == 2);
}

TEST_CASE("widgets are items, so hover and tooltips work") {
    Gui g;
    static const float series[] = {1, 2, 3, 2, 4};
    ImVec2 mn[8], mx[8];
    bool hovered[8] = {};
    auto body = [&] {
        int i = 0;
        auto rec = [&] {
            hovered[i] = ImGui::IsItemHovered();
            mn[i] = ImGui::GetItemRectMin();
            mx[i] = ImGui::GetItemRectMax();
            ++i;
        };
        disp::RingProgress("r", 0.5f);
        rec();
        disp::Sparkline("s", series, 5);
        rec();
        disp::Badge("NEW");
        rec();
        disp::Avatar("Ada");
        rec();
        disp::StatusDot(disp::Status::Online);
        rec();
        disp::Kbd("Ctrl+K");
        rec();
        disp::LoadingBar("l", 0.5f);
        rec();
        disp::StatCard("c", 3.0f);
        rec();
    };
    Mouse(ImVec2(-100, -100), false);
    g.Frames(2, body);
    for (int k = 0; k < 8; ++k) {
        CAPTURE(k);
        CHECK(mx[k].x > mn[k].x);
        CHECK(mx[k].y > mn[k].y);
        Mouse(Mid(mn[k], mx[k]), false);
        g.Frames(2, body);
        CHECK(hovered[k]);
        for (int o = 0; o < 8; ++o)
            if (o != k) CHECK_FALSE(hovered[o]);
    }
}

TEST_CASE("values animate in and converge") {
    Gui g;
    float ring = -1, gauge = -1, bar = -1, card = -1;
    float target = 0.75f;
    auto body = [&] {
        ring = disp::RingProgress("r", target);
        gauge = disp::RadialGauge("g", target * 100.0f, 0.0f, 100.0f);
        bar = disp::LoadingBar("b", target);
        card = disp::StatCard("c", target * 1000.0f);
    };
    g.Frame(body);
    CHECK(ring < 0.1f);
    CHECK(bar < 0.1f);
    CHECK(card < 100.0f);
    CHECK(gauge < 10.0f);
    g.Frames(150, body);
    CHECK(std::fabs(ring - 0.75f) < 1e-3f);
    CHECK(std::fabs(bar - 0.75f) < 1e-3f);
    CHECK(std::fabs(card - 750.0f) < 0.5f);
    CHECK(std::fabs(gauge - 75.0f) < 0.05f);
    target = 0.25f;
    g.Frame(body);
    CHECK(ring < 0.75f);
    CHECK(ring > 0.25f);
    g.Frames(150, body);
    CHECK(std::fabs(ring - 0.25f) < 1e-3f);
    CHECK(std::fabs(gauge - 25.0f) < 0.05f);
    CHECK(std::fabs(card - 250.0f) < 0.5f);

    disp::GetStyle().intro = false;
    float fresh = -1;
    g.Frame([&] { fresh = disp::RingProgress("new one", 0.6f); });
    disp::GetStyle().intro = true;
    CHECK(fresh == doctest::Approx(0.6f));
}

TEST_CASE("timeline scrubber drag changes time") {
    Gui g;
    float t = 0.0f;
    const float marks[] = {15.0f};
    bool changed = false;
    ImVec2 mn, mx;
    auto body = [&] {
        if (disp::TimelineScrubber("##tl", &t, 30.0f, marks, 1)) changed = true;
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
    };
    Mouse(ImVec2(-100, -100), false);
    g.Frames(2, body);
    CHECK_FALSE(changed);
    ImVec2 c = Mid(mn, mx);
    Mouse(ImVec2(c.x + 60.0f, c.y), false);
    g.Frame(body);
    Mouse(ImVec2(c.x + 60.0f, c.y), true);
    g.Frames(2, body);
    CHECK(changed);
    CHECK(t > 15.5f);
    CHECK(t < 30.0f);
    Mouse(ImVec2(c.x + 2.0f, c.y), true);
    g.Frames(2, body);
    CHECK(t == 15.0f);
    Mouse(ImVec2(mx.x + 50.0f, c.y), true);
    g.Frames(2, body);
    CHECK(t == 30.0f);
    Mouse(ImVec2(mn.x - 50.0f, c.y), true);
    g.Frames(2, body);
    CHECK(t == 0.0f);
    Mouse(ImVec2(mn.x - 50.0f, c.y), false);
    g.Frames(2, body);
    changed = false;
    Mouse(ImVec2(c.x + 80.0f, c.y), false);
    g.Frames(2, body);
    CHECK_FALSE(changed);
    CHECK(t == 0.0f);
}

TEST_CASE("breadcrumbs report the clicked item") {
    Gui g;
    const char* items[] = {"Home", "Configs", "Legit"};
    int got = -1;
    ImVec2 mn, mx;
    float w0 = 0, w1 = 0, sep = 0;
    auto body = [&] {
        int r = disp::Breadcrumbs("##bc", items, 3);
        if (r >= 0) got = r;
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
        float font = ImGui::GetFontSize(), pad = font * 0.45f;
        w0 = ImGui::CalcTextSize("Home").x + pad * 2.0f;
        w1 = ImGui::CalcTextSize("Configs").x + pad * 2.0f;
        sep = font * 1.1f;
    };
    Mouse(ImVec2(-100, -100), false);
    g.Frames(2, body);
    float cy = (mn.y + mx.y) * 0.5f;
    Click(g, ImVec2(mn.x + w0 * 0.5f, cy), body);
    CHECK(got == 0);
    got = -1;
    Click(g, ImVec2(mn.x + w0 + sep + w1 * 0.5f, cy), body);
    CHECK(got == 1);
    got = -1;
    Click(g, ImVec2(mn.x + w0 + sep + w1 + sep + 6.0f, cy), body);
    CHECK(got == -1);

    int clicked = -1;
    bool fired = false;
    auto zero = [&] {
        if (disp::Breadcrumbs("##z", "Root\0Child\0Leaf\0", &clicked)) fired = true;
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
    };
    g.Frames(2, zero);
    Click(g, ImVec2(mn.x + 8.0f, (mn.y + mx.y) * 0.5f), zero);
    CHECK(fired);
    CHECK(clicked == 0);
}

TEST_CASE("breadcrumbs collapse the middle when narrow") {
    Gui g;
    const char* items[] = {"Workspace", "Projects", "ImForge", "Packs", "Display", "Widgets"};
    float width = 0.0f, avail = 0.0f;
    g.Frames(2, [&] {
        ImGui::BeginChild("narrow", ImVec2(220, 80));
        avail = ImGui::GetContentRegionAvail().x;
        disp::Breadcrumbs("##bc", items, 6);
        width = ImGui::GetItemRectSize().x;
        ImGui::EndChild();
    });
    CHECK(width > 0.0f);
    CHECK(width <= avail + 1.0f);
}

TEST_CASE("progress steps go back to a finished step on click") {
    Gui g;
    int cur = 2;
    bool changed = false;
    const char* labels[] = {"Account", "Profile", "Done"};
    ImVec2 mn, mx;
    float pad_l = 0, pad_r = 0, cy = 0;
    auto body = [&] {
        if (disp::ProgressSteps("##steps", labels, 3, &cur)) changed = true;
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
        float r = ImGui::GetFrameHeight() * 0.55f, m = r * 0.45f, small = ImGui::GetFontSize() * 0.85f;
        pad_l = disp::detail::Max(r + m, disp::detail::TextSize(small, "Account").x * 0.5f);
        pad_r = disp::detail::Max(r + m, disp::detail::TextSize(small, "Done").x * 0.5f);
        cy = mn.y + m + r;
    };
    Mouse(ImVec2(-100, -100), false);
    g.Frames(2, body);
    Click(g, ImVec2(mn.x + pad_l, cy), body);
    CHECK(changed);
    CHECK(cur == 0);
    changed = false;
    Click(g, ImVec2(mx.x - pad_r, cy), body);
    CHECK_FALSE(changed);
    CHECK(cur == 0);
}

TEST_CASE("key overlay follows keyboard and mouse") {
    Gui g;
    int mask = -1;
    auto body = [&] { mask = disp::KeyOverlay("##keys"); };
    Mouse(ImVec2(-100, -100), false);
    g.Frames(2, body);
    CHECK(mask == 0);
    ImGuiIO& io = ImGui::GetIO();
#if IMGUI_VERSION_NUM >= 18700
    io.AddKeyEvent(ImGuiKey_W, true);
    io.AddKeyEvent(ImGuiKey_Space, true);
#else
    io.KeysDown['W'] = true;
    io.KeyMap[ImGuiKey_Space] = 32;
    io.KeysDown[32] = true;
#endif
    g.Frames(2, body);
    CHECK((mask & 1) != 0);
    CHECK((mask & (1 << 4)) != 0);
#if IMGUI_VERSION_NUM >= 18700
    io.AddKeyEvent(ImGuiKey_W, false);
    io.AddKeyEvent(ImGuiKey_Space, false);
#else
    io.KeysDown['W'] = false;
    io.KeysDown[32] = false;
#endif
    Mouse(ImVec2(-100, -100), true);
    g.Frames(2, body);
    CHECK(mask == (1 << 5));
    Mouse(ImVec2(-100, -100), false);
    g.Frames(2, body);
    CHECK(mask == 0);
    const bool state[7] = {false, false, false, true, true, false, true};
    disp::KeyOverlayStyle ko;
    ko.state = state;
    ko.keys = "ESDF";
    g.Frames(2, [&] { mask = disp::KeyOverlay("##replay", ko); });
    CHECK(mask == ((1 << 3) | (1 << 4) | (1 << 6)));
}

TEST_CASE("frame graph keeps its own history") {
    disp::FrameSamples s;
    for (int i = 0; i < 300; ++i) s.Push((float)i);
    CHECK(s.count == disp::FrameSamples::kCapacity);
    CHECK(s.At(0) == 60.0f);
    CHECK(s.At(s.count - 1) == 299.0f);
    s.Clear();
    CHECK(s.count == 0);

    Gui g;
    int live = 0, pushed = 0;
    g.Frames(10, [&] {
        disp::FrameGraph("history live");
        live = disp::FrameGraphSampleCount("history live");
    });
    CHECK(live == 10);
    g.Frame([&] {
        disp::FrameGraphPush("history pushed", 5.0f);
        disp::FrameGraphPush("history pushed", 6.0f);
        disp::FrameGraph("history pushed");
        pushed = disp::FrameGraphSampleCount("history pushed");
    });
    CHECK(pushed == 2);
}

TEST_CASE("toasts pause for progress, expire and dismiss") {
    Gui g;
    disp::PushToast("Saved", "Config written", disp::ToastKind::Success, 0.5f);
    int p = disp::PushToast("Uploading", nullptr, disp::ToastKind::Info, 0.5f);
    disp::SetToastProgress(p, 0.2f);
    auto body = [] { disp::ToastStack(); };
    g.Frames(70, body);
    CHECK(disp::ToastCount() == 1);
    disp::SetToastProgress(p, 1.0f);
    g.Frames(30, body);
    CHECK(disp::ToastCount() == 1);
    g.Frames(90, body);
    CHECK(disp::ToastCount() == 0);
    int last = 0;
    for (int i = 0; i < 20; ++i) last = disp::PushToast("many");
    CHECK(disp::ToastCount() == 16);
    g.Frames(2, body);
    disp::DismissToast(last);
    g.Frames(40, body);
    CHECK(disp::ToastCount() == 15);
    disp::ClearToasts();
    CHECK(disp::ToastCount() == 0);
}

TEST_CASE("tooltip card shows after the hover delay") {
    Gui g;
    bool shown = false;
    ImVec2 mn, mx;
    auto body = [&] {
        ImGui::Button("target", ImVec2(120, 30));
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
        shown = disp::TooltipCard("Tip", "Body text");
    };
    Mouse(ImVec2(-100, -100), false);
    g.Frames(2, body);
    CHECK_FALSE(shown);
    Mouse(Mid(mn, mx), false);
    g.Frames(3, body);
    CHECK_FALSE(shown);
    g.Frames(20, body);
    CHECK(shown);
    Mouse(ImVec2(-100, -100), false);
    g.Frames(2, body);
    CHECK_FALSE(shown);
}

TEST_CASE("empty state button reports clicks") {
    Gui g;
    bool clicked = false;
    ImVec2 mn, mx;
    float by = 0;
    auto body = [&] {
        if (disp::EmptyState("Nothing", "hint", "Create")) clicked = true;
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
        float font = ImGui::GetFontSize();
        float bh = ImGui::GetFrameHeight() + font * 0.2f;
        by = mx.y - font * 0.6f - ImGui::GetStyle().ItemSpacing.y - bh * 0.5f;
    };
    Mouse(ImVec2(-100, -100), false);
    g.Frames(2, body);
    CHECK_FALSE(clicked);
    Click(g, ImVec2((mn.x + mx.x) * 0.5f, by), body);
    CHECK(clicked);
}

TEST_CASE("badge count pops on change and hides at zero") {
    Gui g;
    int count = 0;
    float before = 0, after = 0, width = 0;
    auto body = [&] {
        ImVec2 c0 = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        int v0 = dl->VtxBuffer.Size;
        disp::BadgeCount("##n", count);
        before = c0.y;
        after = ImGui::GetCursorScreenPos().y;
        float lo = 1e9f, hi = -1e9f;
        for (int i = v0; i < dl->VtxBuffer.Size; ++i) {
            lo = disp::detail::Min(lo, dl->VtxBuffer[i].pos.x);
            hi = disp::detail::Max(hi, dl->VtxBuffer[i].pos.x);
        }
        width = hi > lo ? hi - lo : 0.0f;
    };
    g.Frames(2, body);
    CHECK(after == before);
    CHECK(width == 0.0f);
    count = 5;
    g.Frames(8, body);
    CHECK(after > before);
    float popped = width;
    g.Frames(60, body);
    float rest = width;
    CHECK(rest > 0.0f);
    CHECK(popped > rest + 0.5f);
}

TEST_CASE("initials, colors and key splitting") {
    Gui g;
    g.Frame([] {});
    char buf[12];
    disp::Initials("ada lovelace", buf, sizeof(buf));
    CHECK(std::string(buf) == "AL");
    disp::Initials("Prince", buf, sizeof(buf));
    CHECK(std::string(buf) == "P");
    disp::Initials("   ", buf, sizeof(buf));
    CHECK(std::string(buf).empty());
    disp::Initials("Jean-Luc Picard##7", buf, sizeof(buf));
    CHECK(std::string(buf) == "JP");
    disp::Initials("\xc3\x9cnal Kaya", buf, sizeof(buf));
    CHECK(std::string(buf) == "\xc3\x9cK");
    disp::Initials("Ada Lovelace", buf, 2);
    CHECK(std::string(buf) == "A");
    CHECK(disp::AvatarColor("Ada") == disp::AvatarColor("ada"));
    CHECK(disp::AvatarColor("Ada") != disp::AvatarColor("Bob"));
    CHECK((disp::AvatarColor("Ada") >> IM_COL32_A_SHIFT) == 255u);

    const char* b[8];
    const char* e[8];
    CHECK(disp::detail::SplitKeys("Ctrl+Shift+P", b, e, 8) == 3);
    CHECK(std::string(b[2], e[2]) == "P");
    CHECK(disp::detail::SplitKeys("Ctrl++", b, e, 8) == 2);
    CHECK(std::string(b[1], e[1]) == "+");
    CHECK(disp::detail::SplitKeys("+", b, e, 8) == 1);
    CHECK(std::strcmp(disp::StatusName(disp::Status::Away), "Away") == 0);
}

TEST_CASE("manifests load into the catalog and every type has an adapter") {
    imforge::Json m;
    REQUIRE(LoadJson("packs/display/manifest.json", m));
    CHECK(m["name"].AsString() == "display");
    CHECK(m["version"].AsString() == "0.2.0");
    CHECK(m["kind"].AsString() == "imblib");
    std::vector<std::string> types = ManifestTypes();
    CHECK(types.size() >= 16);
    imforge::WidgetCatalog cat;
    for (const std::string& t : types) {
        CAPTURE(t);
        imforge::Json j;
        REQUIRE(LoadJson("packs/display/widgets/" + t + ".imw.json", j));
        imforge::Error err;
        CHECK(cat.AddManifest(j, &err));
        CAPTURE(err.message);
        const imforge::WidgetDef* d = cat.Find(t.c_str());
        REQUIRE(d != nullptr);
        CHECK(std::string(d->category) == "Display");
        CHECK(j["include"].AsString() == "imb_display.h");
        CHECK(j["call"].AsString().find("imb::disp::") == 0);
    }
    imforge::Interpreter in;
    imforge::RegisterPackWidgets_display(in);
    for (const std::string& t : types) {
        CAPTURE(t);
        CHECK(in.FindWidgetFn(t) != nullptr);
    }
    CHECK(in.widgets.size() == types.size());
}

namespace {

imforge::Node MakeNode(const std::string& id, const std::string& type) {
    imforge::Node n;
    n.id = id;
    n.type = type;
    return n;
}

void Bind(imforge::Node& n, const char* slot, const char* path) {
    n.bind.emplace_back(std::string(slot), imforge::PropValue(imforge::Json(path)));
}

int g_steps_changed = 0;
void OnStepsChange(imforge::DrawCtx&) { ++g_steps_changed; }

struct InterpHarness {
    Gui gui;
    imforge::Interpreter interp;
    imforge::Panel panel;
    imforge::PanelInstance inst;
    std::vector<imforge::BindingRoot> roots;
    imforge::InterpOptions opts;
    DispState st;
    std::vector<std::pair<std::string, std::pair<ImVec2, ImVec2>>> rects;

    InterpHarness() {
        REQUIRE(LoadCatalog(interp.catalog));
        imforge::RegisterPackWidgets_display(interp);
        panel.name = "Main";
        panel.root = MakeNode("win", "Window");
        panel.root.SetProp("title", imforge::Json("Display"));
        roots.push_back(imforge::BindingRoot{"st", &st, imforge::StructInfoOf<DispState>()});
        interp.hooks.on_item = [this](const imforge::Node& n, const ImVec2& a, const ImVec2& b, bool) {
            for (auto& r : rects)
                if (r.first == n.id) {
                    r.second = std::make_pair(a, b);
                    return;
                }
            rects.emplace_back(n.id, std::make_pair(a, b));
        };
    }
    void Frame(bool design) {
        opts.design_mode = design;
        gui.h.BeginFrame();
        if (design) {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(1000, 700));
            ImGui::Begin("Host", nullptr, ImGuiWindowFlags_NoSavedSettings);
            ImGui::BeginChild("canvas", ImVec2(980, 660));
            interp.DrawPanel(panel, inst, roots, opts);
            ImGui::EndChild();
            ImGui::End();
        } else {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(1000, 2400));
            interp.DrawPanel(panel, inst, roots, opts);
        }
        gui.h.EndFrame();
    }
    bool Rect(const std::string& id, ImVec2& a, ImVec2& b) const {
        for (const auto& r : rects)
            if (r.first == id) {
                a = r.second.first;
                b = r.second.second;
                return true;
            }
        return false;
    }
    void CheckNoErrors() const {
        for (const imforge::Diagnostic& d : interp.last_diagnostics) {
            CAPTURE(d.node_id);
            CAPTURE(d.message);
            CHECK(d.level != imforge::Diagnostic::Level::Error);
        }
    }
};

std::vector<imforge::Node> AllNodes() {
    std::vector<imforge::Node> out;
    for (const std::string& t : ManifestTypes()) {
        imforge::Node n = MakeNode("n_" + t, t);
        if (t == "ProgressSteps") Bind(n, "current", "st.step");
        if (t == "TimelineScrubber") {
            Bind(n, "time", "st.time");
            Bind(n, "markers", "st.marks");
        }
        if (t == "Sparkline") Bind(n, "values", "st.series");
        if (t == "MiniBars") Bind(n, "values", "st.vals");
        if (t == "StatCard") Bind(n, "spark", "st.series");
        if (t == "FrameGraph") Bind(n, "samples", "st.vals");
        if (t == "Avatar") Bind(n, "texture", "st.tex");
        if (t == "Breadcrumbs") Bind(n, "clicked", "st.crumb");
        out.push_back(n);
    }
    out.push_back(MakeNode("n_live_graph", "FrameGraph"));
    imforge::Node ring = MakeNode("n_ring_bound", "RingProgress");
    ring.SetProp("fraction", imforge::Json("st.load"));
    out.push_back(ring);
    return out;
}

}  // namespace

TEST_CASE("adapters draw every widget through the interpreter") {
    InterpHarness h;
    h.st.tex = FakeTexture();
    h.panel.root.children = AllNodes();
    for (int f = 0; f < 3; ++f) h.Frame(true);
    h.CheckNoErrors();
    for (const imforge::Node& n : h.panel.root.children) {
        CAPTURE(n.id);
        ImVec2 a, b;
        CHECK(h.Rect(n.id, a, b));
    }
    for (int f = 0; f < 3; ++f) h.Frame(false);
    h.CheckNoErrors();
}

TEST_CASE("adapter bindings follow clicks and fire events") {
    InterpHarness h;
    imforge::Node steps = MakeNode("steps", "ProgressSteps");
    Bind(steps, "current", "st.step");
    imforge::EventSpec ev;
    ev.handler = true;
    steps.events.emplace_back(std::string("onChange"), ev);
    imforge::Node tl = MakeNode("tl", "TimelineScrubber");
    Bind(tl, "time", "st.time");
    h.panel.root.children.push_back(steps);
    h.panel.root.children.push_back(tl);
    h.interp.RegisterHandler("Main.steps.onChange", &OnStepsChange);
    g_steps_changed = 0;
    Mouse(ImVec2(-100, -100), false);
    h.Frame(false);
    h.Frame(false);
    ImVec2 a, b;
    REQUIRE(h.Rect("steps", a, b));
    float r = 0, m = 0, pad_l = 0;
    h.gui.h.BeginFrame();
    r = ImGui::GetFrameHeight() * 0.55f;
    m = r * 0.45f;
    pad_l = disp::detail::Max(r + m, disp::detail::TextSize(ImGui::GetFontSize() * 0.85f, "Account").x * 0.5f);
    h.gui.h.EndFrame();
    ImVec2 c0(a.x + pad_l, a.y + m + r);
    Mouse(c0, false);
    h.Frame(false);
    Mouse(c0, true);
    h.Frame(false);
    h.Frame(false);
    Mouse(c0, false);
    h.Frame(false);
    CHECK(h.st.step == 0);
    CHECK(g_steps_changed == 1);

    REQUIRE(h.Rect("tl", a, b));
    ImVec2 right(b.x + 40.0f, (a.y + b.y) * 0.5f);
    Mouse(ImVec2(b.x - 5.0f, right.y), false);
    h.Frame(false);
    Mouse(ImVec2(b.x - 5.0f, right.y), true);
    h.Frame(false);
    Mouse(right, true);
    h.Frame(false);
    h.Frame(false);
    Mouse(right, false);
    h.Frame(false);
    CHECK(h.st.time == doctest::Approx(30.0f));
}
