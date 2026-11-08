#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"

namespace hooks {
struct Rect {
    ImVec2 min, max;
};
inline std::map<std::string, std::vector<Rect>>& All() {
    static std::map<std::string, std::vector<Rect>> m;
    return m;
}
inline void Record(const char* name) { All()[name].push_back(Rect{ImGui::GetItemRectMin(), ImGui::GetItemRectMax()}); }
}  // namespace hooks
#define IMB_GAME_ITEM_HOOK(name) hooks::Record(name)

#include "imforge/imforge_interp.h"
#include "imb_ess_controls.h"
#include "../packs/gaming/imb_gaming.h"
#include "../packs/gaming/imforge_adapters_gaming.h"
#include "imgui_headless.h"

#ifndef IMFORGE_SOURCE_DIR
#define IMFORGE_SOURCE_DIR "."
#endif

using imforge::test::HeadlessImGui;
namespace game = imb::game;

struct GameCfg {
    bool enabled = false;
    int page = 0;
    float gap = 4.0f;
    bool dot = false;
    int keys[3] = {0, 0, 0};
    int selected = -1;
    int action = 0;
    bool open = true;
    float radius = 120.0f;
    ImVec4 team_a = ImVec4(0.2f, 0.6f, 1.0f, 1.0f);
    ImVec4 team_b = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
    std::vector<std::string> names = {"Main", "Alt", ""};
    std::vector<float> points = {0.0f, 0.5f, 1.0f, 0.4f, -0.2f, 0.0f};
    int changes = 0;
};
IMFORGE_REFLECT(GameCfg, enabled, page, gap, dot, keys, selected, action, open, radius, team_a, team_b, names, points,
                changes);

namespace {

const ImGuiWindowFlags kWinFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;

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

void NoTrickle() {
#if IMGUI_VERSION_NUM >= 18700
    ImGui::GetIO().ConfigInputTrickleEventQueue = false;
#endif
}

template <class F> void Frame(HeadlessImGui& h, F&& body, ImVec2 size = ImVec2(700, 700)) {
    hooks::All().clear();
    h.BeginFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(size);
    ImGui::Begin("g", nullptr, kWinFlags);
    body();
    ImGui::End();
    h.EndFrame();
}

template <class F> void Frames(HeadlessImGui& h, int n, F&& body) {
    for (int i = 0; i < n; ++i) Frame(h, body);
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

ImVec2 Center(const ImVec2& a, const ImVec2& b) { return ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f); }

hooks::Rect Hook(const char* name, int index = 0) {
    auto it = hooks::All().find(name);
    REQUIRE_MESSAGE(it != hooks::All().end(), "no hook rect for " << name);
    REQUIRE((int)it->second.size() > index);
    return it->second[(size_t)index];
}

ImVec2 HookCenter(const char* name, int index = 0) {
    hooks::Rect r = Hook(name, index);
    return Center(r.min, r.max);
}

std::string ReadFile(const std::string& path) {
    std::string out;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
}

}  // namespace

TEST_CASE("every gaming widget draws for three frames") {
    HeadlessImGui h;
    game::EspPreview esp;
    game::EspPreview esp3d;
    esp3d.box_style = game::EspBox_3D;
    esp3d.box_fill = true;
    esp3d.head_dot = true;
    esp3d.health_side = game::EspBar_Top;
    game::Crosshair ch;
    ch.dot = true;
    ch.dynamic = true;
    game::Crosshair ch_t;
    ch_t.t_style = true;
    float radius = 150.0f;
    game::RadarPoint pts[3];
    pts[0].x = 0.2f;
    pts[0].y = 0.6f;
    pts[1].x = -2.0f;
    pts[1].team = 0;
    pts[2].has_yaw = true;
    pts[2].yaw = 45.0f;
    game::RadarStyle square;
    square.shape = game::RadarShape_Square;
    square.rotation = 30.0f;
    const float packed[] = {0.1f, 0.1f, 1.0f, -0.5f, 0.3f, 0.0f};
    const char* names[] = {"Main", nullptr, "Third"};
    int sel = 0;
    const char* actions[] = {"Menu", "Panic"};
    int keys[2] = {0, 0};
    ImVec4 ta(0.2f, 0.6f, 1.0f, 1.0f), tb(0.2f, 0.62f, 1.0f, 1.0f);
    int page = 1;
    bool on = true;
    bool open = true;
    game::PlayerInfo pinfo;
    pinfo.name = "Someone";
    pinfo.flags = game::PlayerFlag_Speaking | game::PlayerFlag_Friend | game::PlayerFlag_Bot | game::PlayerFlag_Local;
    pinfo.ping = 140;
    pinfo.score = 12;
    float gap = 3.0f;
    ImDrawData* dd = nullptr;
    int fg = 0;
    for (int i = 0; i < 3; ++i) {
        hooks::All().clear();
        h.BeginFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(1200, 700));
        ImGui::Begin("all", nullptr, kWinFlags);
        ImGui::BeginChild("col1", ImVec2(400, 0));
        game::EspBoxPreview("esp", &esp);
        ImGui::SameLine();
        game::EspBoxPreview("esp3d", &esp3d, ImVec2(120, 160));
        game::EspBoxPreview("espnull", nullptr, ImVec2(80, 100));
        game::EspBoxPreviewFields("espf", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                  nullptr, nullptr, "Fields", 12.0f, 2.0f, ImVec2(80, 100));
        game::CrosshairPreview("ch", &ch);
        ImGui::SameLine();
        game::CrosshairPreview("ch_t", &ch_t, ImVec2(60, 60), 2.0f);
        game::CrosshairPreviewFields("chf", &gap, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
        game::CrosshairEditor("Crosshair", &ch);
        game::CrosshairEditorFields("Fields##chf", &gap, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("col2", ImVec2(400, 0));
        game::FovCirclePreview("fov", &radius);
        game::FovCirclePreview("fov2", nullptr, 720.0f, IM_COL32(255, 0, 0, 255), false, false, ImVec2(160, 90));
        game::RadarMiniMap("radar", pts, 3);
        ImGui::SameLine();
        game::RadarMiniMap("radar2", pts, 3, square, ImVec2(90, 90));
        game::RadarMiniMapPacked("radar3", packed, 6, nullptr, game::RadarShape_Circle, 2.0f, 2, false, ImVec2(80, 80));
        game::RadarMiniMapPacked("radar4", nullptr, game::CountOf(nullptr), nullptr, 0, 1.0f, 3, true, ImVec2(60, 60));
        game::ProfileSlots("profiles", names, 3, &sel, 0);
        game::ProfileSlotsList("plist", nullptr, 0, "A\0B\0", nullptr, -1, nullptr);
        std::string slot_names[2] = {"One", " "};
        game::ProfileSlotsList("plist2", slot_names, 2, nullptr, nullptr, 0, nullptr);
        game::HotkeyList("hk", actions, keys, 2);
        game::HotkeyList("hk2", "One\0Two\0Three\0", nullptr, 0);
        game::TeamColorPicker("Teams", &ta, &tb);
        game::TeamColorPicker("Teams2##null", nullptr, nullptr, "A", "B", false);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("col3", ImVec2(380, 0));
        game::IconRailNav("rail", "A\0B\0C\0", "Alpha\0Beta\0Gamma\0", &page);
        ImGui::SameLine();
        game::IconRailNav("rail2", "X\0Y\0", "", nullptr);
        game::CategoryCard("Visuals", "Boxes, names and health bars, plus a long tail of text that wraps", "V", &on);
        game::CategoryCard("Button card##b", nullptr, nullptr, nullptr, ImVec2(200, 40), IM_COL32(255, 128, 0, 255));
        game::Watermark("Tool", "eu-west");
        game::Watermark("Tool##corner", nullptr, game::Corner_TopRight, true, true);
        game::FpsPingCounter("perf", 144.0f, 32.0f);
        game::FpsPingCounter("perf2", -1.0f, 150.0f, game::Corner_BottomLeft, true);
        game::PlayerListRow("p1", pinfo, true);
        game::PlayerListRow("p2", "Dead one", 1, 0.0f, game::PlayerFlag_Dead | game::PlayerFlag_Muted, 20);
        game::HealthArmorBar("hp", 80.0f, 40.0f);
        game::HealthArmorBar("hp2", 10.0f, 0.0f, 100.0f, 0.0f, true, false, 120.0f);
        game::KillFeedEntry("k1", "Alpha", "Bravo", "Rifle", 0.1f, 0, 1, game::KillFlag_Headshot | game::KillFlag_Wallbang);
        game::KillFeedEntry("k2", "Me", "Them", "", 5.8f, 2, 3, game::KillFlag_Local, 6.0f);
        game::NotificationBanner("ban", &open, "Saved", "Undo", game::Banner_Success);
        game::NotificationBanner("ban2", nullptr, "Heads up", nullptr, game::Banner_Warning, true, 5.0f);
        game::NotificationBanner("ban3", nullptr, "Error", "Retry", game::Banner_Error);
        ImGui::EndChild();
        fg = ImGui::GetForegroundDrawList()->VtxBuffer.Size;
        ImGui::End();
        dd = h.EndFrame();
    }
    REQUIRE(dd != nullptr);
    CHECK(dd->TotalVtxCount > 1000);
    CHECK(fg > 0);
    CHECK(game::CountOf(7) == 7);
}

TEST_CASE("crosshair editor changes the bound struct") {
    HeadlessImGui h;
    NoTrickle();
    game::Crosshair ch;
    int changes = 0;
    auto body = [&] {
        if (game::CrosshairEditor("Crosshair", &ch)) ++changes;
    };
    Frames(h, 2, body);
    CHECK_FALSE(ch.dot);
    Click(h, HookCenter("crosshair.dot"), body);
    CHECK(ch.dot);
    CHECK(changes == 1);
    Frames(h, 1, body);
    hooks::Rect gap = Hook("crosshair.gap");
    Click(h, ImVec2(gap.min.x + 2.0f, (gap.min.y + gap.max.y) * 0.5f), body);
    CHECK(ch.gap < 0.0f);
    CHECK(changes >= 2);
    Frames(h, 1, body);
    Click(h, HookCenter("Dot"), body);
    CHECK(ch.length == 0.0f);
    CHECK(ch.dot);
    CHECK(ch.dot_size == doctest::Approx(4.0f));

    float g = 4.0f;
    auto fields = [&] { game::CrosshairEditorFields("F", &g, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr); };
    Frames(h, 2, fields);
    hooks::Rect fg = Hook("crosshair.gap");
    float label_w = ImGui::CalcTextSize("Gap").x + ImGui::GetStyle().ItemInnerSpacing.x;
    Click(h, ImVec2(fg.max.x - label_w - 2.0f, (fg.min.y + fg.max.y) * 0.5f), fields);
    CHECK(g > 15.0f);
    Frames(h, 1, fields);
    Click(h, HookCenter("crosshair.t_style"), fields);
    bool t_on = false;
    Frame(h, [&] {
        fields();
        ImGui::PushID("F");
        ImGuiID k = ImGui::GetID("##fields");
        ImGui::PopID();
        t_on = ImGui::GetStateStorage()->GetBool(k + 5, false);
    });
    CHECK(t_on);
}

TEST_CASE("crosshair preview cycles its backdrop on click") {
    HeadlessImGui h;
    NoTrickle();
    game::Crosshair ch;
    int clicks = 0;
    ImVec2 mn, mx;
    auto body = [&] {
        if (game::CrosshairPreview("prev", &ch, ImVec2(100, 100))) ++clicks;
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
    };
    Frames(h, 2, body);
    CHECK(mx.x - mn.x == doctest::Approx(100.0f));
    Click(h, Center(mn, mx), body);
    Click(h, Center(mn, mx), body);
    CHECK(clicks == 2);
}

TEST_CASE("dragging the fov ring changes the radius") {
    HeadlessImGui h;
    NoTrickle();
    float r = 120.0f;
    int changes = 0;
    ImVec2 mn, mx;
    auto body = [&] {
        if (game::FovCirclePreview("fov", &r, game::FovPreviewStyle(), ImVec2(320, 180))) ++changes;
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
    };
    Frames(h, 2, body);
    CHECK(mx.y - mn.y == doctest::Approx(180.0f));
    ImVec2 c = Center(mn, mx);
    float scale = 180.0f / 1080.0f;
    ImVec2 ring(c.x + 120.0f * scale, c.y);
    SetMouse(ring, false);
    Frames(h, 2, body);
    SetMouse(ring, true);
    Frames(h, 2, body);
    SetMouse(ImVec2(c.x + 40.0f, c.y), true);
    Frames(h, 2, body);
    SetMouse(ImVec2(c.x + 40.0f, c.y), false);
    Frames(h, 2, body);
    CHECK(r == doctest::Approx(40.0f / scale).epsilon(0.02));
    CHECK(changes >= 1);
    float before = r;
    Click(h, c, body);
    CHECK(r == doctest::Approx(before));
}

TEST_CASE("team colors take presets and swap") {
    HeadlessImGui h;
    NoTrickle();
    ImVec4 a(1, 1, 1, 1), b(0, 0, 0, 1);
    int changes = 0;
    auto body = [&] {
        if (game::TeamColorPicker("Teams", &a, &b)) ++changes;
    };
    Frames(h, 2, body);
    Click(h, HookCenter("team.preset", 1), body);
    int n = 0;
    const game::TeamPreset* p = game::TeamPresets(&n);
    REQUIRE(n >= 2);
    CHECK(ImGui::ColorConvertFloat4ToU32(a) == p[1].a);
    CHECK(ImGui::ColorConvertFloat4ToU32(b) == p[1].b);
    Click(h, HookCenter("team.swap"), body);
    CHECK(ImGui::ColorConvertFloat4ToU32(a) == p[1].b);
    CHECK(ImGui::ColorConvertFloat4ToU32(b) == p[1].a);
    CHECK(changes == 2);
}

TEST_CASE("icon rail selects the clicked icon") {
    HeadlessImGui h;
    NoTrickle();
    int page = 0, changes = 0;
    ImVec2 mn, mx;
    const char* icons[] = {"A", "B", "C", "D"};
    const char* tips[] = {"Alpha", "Beta", "Gamma", "Delta"};
    auto body = [&] {
        if (game::IconRailNav("rail", icons, tips, 4, &page)) ++changes;
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
    };
    Frames(h, 2, body);
    float total = mx.y - mn.y;
    ImVec2 third(Center(mn, mx).x, mn.y + total * 2.5f / 4.0f);
    Click(h, third, body);
    CHECK(page == 2);
    CHECK(changes == 1);
    Click(h, third, body);
    CHECK(changes == 1);
    SetMouse(ImVec2(Center(mn, mx).x, mn.y + total * 0.5f / 4.0f), false);
    Frames(h, 40, body);
    CHECK(page == 2);

    bool changed = false;
    auto local = [&] {
        changed |= game::IconRailNav("rail2", "X\0Y\0", nullptr, nullptr);
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
    };
    Frames(h, 2, local);
    Click(h, ImVec2(Center(mn, mx).x, mn.y + (mx.y - mn.y) * 0.75f), local);
    CHECK(changed);
    int stored = -1;
    Frame(h, [&] {
        local();
        ImGui::PushID("rail2");
        ImGuiID k = ImGui::GetID("##index");
        ImGui::PopID();
        stored = ImGui::GetStateStorage()->GetInt(k, -1);
    });
    CHECK(stored == 1);
}

TEST_CASE("profile slots report select, load, save and a confirmed delete") {
    HeadlessImGui h;
    NoTrickle();
    const char* names[] = {"Default", "Ranked", "Casual", ""};
    int sel = 0, out = -1;
    std::vector<int> actions;
    auto body = [&] {
        int a = game::ProfileSlots("profiles", names, 4, &sel, 0, &out);
        if (a != game::ProfileAction_None) actions.push_back(a);
        CHECK(out == a);
    };
    Frames(h, 2, body);
    hooks::Rect list = Hook("profiles.list");
    float lh = list.max.y - list.min.y;
    auto row = [&](int i) { return ImVec2(Center(list.min, list.max).x, list.min.y + lh * ((float)i + 0.5f) / 4.0f); };
    Click(h, row(1), body);
    CHECK(sel == 1);
    REQUIRE(actions.size() == 1);
    CHECK(actions.back() == game::ProfileAction_Select);
    Click(h, HookCenter("profiles.load"), body);
    CHECK(actions.back() == game::ProfileAction_Load);
    Click(h, HookCenter("profiles.save"), body);
    CHECK(actions.back() == game::ProfileAction_Save);
    size_t before = actions.size();
    Click(h, HookCenter("profiles.delete"), body);
    CHECK(actions.size() == before);
    Click(h, HookCenter("profiles.delete"), body);
    CHECK(actions.back() == game::ProfileAction_Delete);
    CHECK(sel == 1);

    Click(h, row(3), body);
    CHECK(sel == 3);
    before = actions.size();
    Click(h, HookCenter("profiles.load"), body);
    Click(h, HookCenter("profiles.delete"), body);
    CHECK(actions.size() == before);
    Click(h, HookCenter("profiles.save"), body);
    CHECK(actions.back() == game::ProfileAction_Save);
}

TEST_CASE("hotkey list rebinds a row through the keybind badge") {
    HeadlessImGui h;
    NoTrickle();
    const char* actions[] = {"Menu", "Panic", "Shot"};
    int keys[3] = {0, 0, 0};
    int changed_row = -1, changes = 0;
    auto body = [&] {
        if (game::HotkeyList("hk", actions, keys, 3, &changed_row)) ++changes;
    };
    Frames(h, 2, body);
    REQUIRE(hooks::All()["hotkeys.badge"].size() == 3);
    Click(h, HookCenter("hotkeys.badge", 1), body);
    CHECK(imb::ess::IsCapturingKeybind());
#if IMGUI_VERSION_NUM >= 18700
    const int kKey = ImGuiKey_F5;
#else
    const int kKey = 'A';
#endif
    PressKey(h, kKey, body);
    CHECK(keys[1] == kKey);
    CHECK(changed_row == 1);
    CHECK(changes == 1);
    CHECK_FALSE(imb::ess::IsCapturingKeybind());
    keys[0] = kKey;
    Frames(h, 2, body);
    CHECK(changes == 1);
}

TEST_CASE("category card flips its bool and lifts on hover") {
    HeadlessImGui h;
    NoTrickle();
    bool on = false;
    int clicks = 0, button_clicks = 0;
    ImVec2 mn, mx, bmn, bmx;
    auto body = [&] {
        if (game::CategoryCard("Visuals", "desc", "V", &on, ImVec2(260, 0))) ++clicks;
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
        if (game::CategoryCard("Open##btn", nullptr, nullptr, nullptr)) ++button_clicks;
        bmn = ImGui::GetItemRectMin();
        bmx = ImGui::GetItemRectMax();
    };
    Frames(h, 2, body);
    CHECK(mx.x - mn.x == doctest::Approx(260.0f));
    Click(h, Center(mn, mx), body);
    CHECK(on);
    CHECK(clicks == 1);
    Click(h, Center(mn, mx), body);
    CHECK_FALSE(on);
    Click(h, Center(bmn, bmx), body);
    CHECK(button_clicks == 1);
}

TEST_CASE("player row returns row and hover button actions") {
    HeadlessImGui h;
    NoTrickle();
    std::vector<int> got;
    ImVec2 mn, mx;
    auto body = [&] {
        int a = game::PlayerListRow("row", "Someone", 1, 0.5f, 0, 40, false, 400.0f);
        if (a) got.push_back(a);
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
    };
    Frames(h, 2, body);
    ImVec2 left(mn.x + 30.0f, Center(mn, mx).y);
    Click(h, left, body);
    REQUIRE(got.size() == 1);
    CHECK(got.back() == game::PlayerRow_Click);
    float rh = mx.y - mn.y;
    float bs = floorf(rh * 0.7f), bgap = floorf(ImGui::GetFontSize() * 0.2f);
    ImVec2 mute(mx.x - bgap - bs * 0.5f, Center(mn, mx).y);
    ImVec2 profile(mx.x - bgap * 3.0f - bs * 2.5f, Center(mn, mx).y);
    SetMouse(mute, false);
    Frames(h, 30, body);
    Click(h, mute, body);
    CHECK(got.back() == game::PlayerRow_Mute);
    Frames(h, 30, body);
    Click(h, profile, body);
    CHECK(got.back() == game::PlayerRow_Profile);
    size_t n = got.size();
    Click(h, left, body, 1);
    CHECK(got.size() == n + 1);
    CHECK(got.back() == game::PlayerRow_Profile);
}

TEST_CASE("banner reports its action and closes") {
    HeadlessImGui h;
    NoTrickle();
    bool open = true;
    int actions = 0;
    bool drawn = false;
    auto body = [&] {
        ImVec2 before = ImGui::GetCursorScreenPos();
        if (game::NotificationBanner("ban", &open, "Saved", "Undo", game::Banner_Info)) ++actions;
        drawn = ImGui::GetCursorScreenPos().y > before.y;
    };
    Frames(h, 3, body);
    CHECK(drawn);
    Click(h, HookCenter("banner.action"), body);
    CHECK(actions == 1);
    CHECK(open);
    Click(h, HookCenter("banner.close"), body);
    CHECK_FALSE(open);
    Frames(h, 40, body);
    CHECK_FALSE(drawn);
    open = true;
    Frames(h, 40, body);
    CHECK(drawn);

    bool timed = true;
    game::BannerStyle st;
    st.timeout = 0.2f;
    auto timed_body = [&] { game::NotificationBanner("timed", &timed, "Bye", nullptr, game::Banner_Warning, st); };
    SetMouse(ImVec2(-100, -100), false);
    Frames(h, 30, timed_body);
    CHECK_FALSE(timed);
}

TEST_CASE("kill feed entries slide, fade and expire") {
    HeadlessImGui h;
    bool alive = false, dead = true;
    float early_h = 0.0f, late_h = 0.0f;
    Frame(h, [&] {
        alive = game::KillFeedEntry("a", "A", "B", "Rifle", 1.0f);
        early_h = ImGui::GetItemRectSize().y;
        game::KillFeedEntry("b", "A", "B", "Rifle", 5.95f);
        late_h = ImGui::GetItemRectSize().y;
        dead = game::KillFeedEntry("c", "A", "B", "Rifle", 6.5f);
    });
    CHECK(alive);
    CHECK_FALSE(dead);
    CHECK(early_h == doctest::Approx(ImGui::GetFrameHeight()));
    CHECK(late_h < early_h);
}

TEST_CASE("health bar damage trail lags behind a drop") {
    HeadlessImGui h;
    float hp = 100.0f;
    float trail = -1.0f, shown = -1.0f;
    auto body = [&] {
        game::HealthArmorBar("hp", hp, 50.0f);
        ImGui::PushID("hp");
        ImGuiID k = ImGui::GetID("##h");
        ImGui::PopID();
        shown = ImGui::GetStateStorage()->GetFloat(k, -1.0f);
        trail = ImGui::GetStateStorage()->GetFloat(k + 1, -1.0f);
    };
    Frames(h, 3, body);
    CHECK(trail == doctest::Approx(1.0f));
    hp = 40.0f;
    Frames(h, 10, body);
    CHECK(shown < 0.6f);
    CHECK(trail == doctest::Approx(1.0f));
    Frames(h, 60, body);
    CHECK(trail < 0.9f);
    Frames(h, 60, body);
    CHECK(trail == doctest::Approx(0.4f));
}

TEST_CASE("radar reports the hovered point") {
    HeadlessImGui h;
    NoTrickle();
    game::RadarPoint pts[2];
    pts[0].x = 0.0f;
    pts[0].y = 0.5f;
    pts[1].x = 0.5f;
    pts[1].y = 0.0f;
    game::RadarStyle st;
    st.sweep = false;
    int hovered = -2;
    ImVec2 mn, mx;
    auto body = [&] {
        hovered = game::RadarMiniMap("radar", pts, 2, st, ImVec2(200, 200));
        mn = ImGui::GetItemRectMin();
        mx = ImGui::GetItemRectMax();
    };
    Frames(h, 2, body);
    ImVec2 c(floorf(Center(mn, mx).x), floorf(Center(mn, mx).y));
    float inset = 99.0f - std::fmax(2.5f, 99.0f * 0.045f) - 1.0f;
    SetMouse(ImVec2(c.x, c.y - inset * 0.5f), false);
    Frames(h, 2, body);
    CHECK(hovered == 0);
    SetMouse(ImVec2(c.x + inset * 0.5f, c.y), false);
    Frames(h, 2, body);
    CHECK(hovered == 1);
    SetMouse(ImVec2(c.x - 60.0f, c.y + 60.0f), false);
    Frames(h, 2, body);
    CHECK(hovered == -1);
    st.rotation = 90.0f;
    SetMouse(ImVec2(c.x + inset * 0.5f, c.y), false);
    Frames(h, 2, body);
    CHECK(hovered == 0);
}

namespace {

const char* kPanel = R"json({
  "schemaVersion": 1, "kind": "panel", "name": "Game",
  "params": [ { "name": "cfg", "type": "GameCfg" } ],
  "root": { "id": "window", "type": "Window",
    "props": { "title": "Gaming", "initialSize": {"x": 900, "y": 2000}, "initialPos": {"x": 0, "y": 0}, "sizeCond": "Always" },
    "children": [
      { "id": "esp", "type": "EspBoxPreview", "bind": { "box": "cfg.enabled", "health": "cfg.radius" } },
      { "id": "chp", "type": "CrosshairPreview", "bind": { "gap": "cfg.gap", "dot": "cfg.dot" } },
      { "id": "che", "type": "CrosshairEditor", "bind": { "gap": "cfg.gap", "dot": "cfg.dot" } },
      { "id": "fov", "type": "FovCirclePreview", "bind": { "radius": "cfg.radius" } },
      { "id": "radar", "type": "RadarMiniMap", "props": { "shape": "Square" }, "bind": { "points": "cfg.points" } },
      { "id": "prof", "type": "ProfileSlots", "bind": { "names": "cfg.names", "selected": "cfg.selected", "action": "cfg.action" } },
      { "id": "hk", "type": "HotkeyList", "props": { "actions": ["A", "B", "C", "D"] }, "bind": { "keys": "cfg.keys" } },
      { "id": "wm", "type": "Watermark", "props": { "corner": "TopRight" } },
      { "id": "perf", "type": "FpsPingCounter", "props": { "fps": "144", "ping": "cfg.radius" } },
      { "id": "row", "type": "PlayerListRow", "props": { "name": "Player {cfg.page}", "team": "cfg.page", "flags": "8" } },
      { "id": "team", "type": "TeamColorPicker", "bind": { "teamA": "cfg.team_a", "teamB": "cfg.team_b" } },
      { "id": "rail", "type": "IconRailNav", "bind": { "index": "cfg.page" },
        "events": { "onChange": { "actions": [ { "type": "set", "target": "cfg.changes", "value": { "expr": "cfg.changes + 1" } } ] } } },
      { "id": "card", "type": "CategoryCard", "props": { "size": { "w": 300, "h": 56 } }, "bind": { "enabled": "cfg.enabled" },
        "events": { "onChange": { "actions": [ { "type": "set", "target": "cfg.changes", "value": { "expr": "cfg.changes + 1" } } ] } } },
      { "id": "hp", "type": "HealthArmorBar", "props": { "health": "cfg.radius / 2" } },
      { "id": "kill", "type": "KillFeedEntry" },
      { "id": "ban", "type": "NotificationBanner", "bind": { "open": "cfg.open" } }
    ] } })json";

struct Item {
    std::string id;
    ImVec2 min, max;
};

struct Harness {
    HeadlessImGui gui{1400.0f, 2100.0f};
    imforge::Interpreter interp;
    imforge::Panel panel;
    imforge::PanelInstance inst;
    std::vector<imforge::BindingRoot> roots;
    imforge::InterpOptions opts;
    GameCfg cfg;
    std::vector<Item> items;
    std::vector<std::string> types;

    Harness() {
        using namespace imforge;
        Json j;
        JsonError je;
        REQUIRE_MESSAGE(ParseJson(kPanel, j, &je), je.message);
        Error e;
        REQUIRE_MESSAGE(PanelFromJson(j, panel, &e), e.message);
        std::string dir = std::string(IMFORGE_SOURCE_DIR) + "/packs/gaming/";
        Json m;
        REQUIRE_MESSAGE(ParseJson(ReadFile(dir + "manifest.json"), m, &je), je.message);
        const Json& widgets = m["widgets"];
        for (size_t i = 0; i < widgets.Size(); ++i) {
            Json w;
            std::string path = dir + widgets[i].AsString();
            REQUIRE_MESSAGE(ParseJson(ReadFile(path), w, &je), path << ": " << je.message);
            Error we;
            CHECK_MESSAGE(interp.catalog.AddManifest(w, &we), we.message);
            types.push_back(w["name"].AsString());
        }
        RegisterPackWidgets_gaming(interp);
        roots.push_back(BindingRoot{"cfg", &cfg, StructInfoOf<GameCfg>()});
        interp.hooks.on_item = [this](const Node& n, const ImVec2& mn, const ImVec2& mx, bool) {
            items.push_back(Item{n.id, mn, mx});
        };
        NoTrickle();
    }

    void Frame() {
        items.clear();
        hooks::All().clear();
        gui.BeginFrame();
        if (opts.design_mode) {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(1000, 2000));
            ImGui::Begin("Builder", nullptr, ImGuiWindowFlags_NoSavedSettings);
            interp.DrawPanel(panel, inst, roots, opts);
            ImGui::End();
        } else {
            interp.DrawPanel(panel, inst, roots, opts);
        }
        gui.EndFrame();
    }

    const Item* Find(const char* id) const {
        for (const Item& it : items)
            if (it.id == id) return &it;
        return nullptr;
    }

    void ClickAt(ImVec2 c) {
        SetMouse(c, false);
        Frame();
        SetMouse(c, true);
        Frame();
        SetMouse(c, false);
        Frame();
    }

    int Errors() const {
        int n = 0;
        for (const imforge::Diagnostic& d : interp.last_diagnostics) {
            if (d.level != imforge::Diagnostic::Level::Error) continue;
            MESSAGE(d.node_id << ": " << d.message);
            ++n;
        }
        return n;
    }
};

}  // namespace

TEST_CASE("gaming manifests load and every type draws through its adapter") {
    Harness h;
    CHECK(h.types.size() == 16);
    for (const std::string& t : h.types) {
        CHECK_MESSAGE(h.interp.catalog.FindCustom(t.c_str()) != nullptr, t);
        CHECK_MESSAGE(h.interp.FindWidgetFn(t) != nullptr, t);
    }
    for (int mode = 0; mode < 2; ++mode) {
        h.opts.design_mode = mode == 1;
        for (int f = 0; f < 3; ++f) h.Frame();
        CHECK(h.Errors() == 0);
        for (const char* id : {"esp", "chp", "che", "fov", "radar", "prof", "hk", "wm", "perf", "row", "team", "rail", "card",
                               "hp", "kill", "ban"})
            CHECK_MESSAGE(h.Find(id) != nullptr, id);
    }

    h.opts.design_mode = false;
    h.Frame();
    const Item* card = h.Find("card");
    REQUIRE(card != nullptr);
    h.ClickAt(Center(card->min, card->max));
    CHECK(h.cfg.enabled);
    CHECK(h.cfg.changes == 1);

    const Item* rail = h.Find("rail");
    REQUIRE(rail != nullptr);
    h.ClickAt(ImVec2(Center(rail->min, rail->max).x, rail->min.y + (rail->max.y - rail->min.y) * 2.5f / 4.0f));
    CHECK(h.cfg.page == 2);
    CHECK(h.cfg.changes == 2);

    const Item* prof = h.Find("prof");
    REQUIRE(prof != nullptr);
    h.Frame();
    hooks::Rect list = Hook("profiles.list");
    h.ClickAt(ImVec2(Center(list.min, list.max).x, list.min.y + (list.max.y - list.min.y) * 1.5f / 3.0f));
    CHECK(h.cfg.selected == 1);
    h.Frame();
    h.ClickAt(HookCenter("profiles.load"));
    CHECK(h.cfg.action == game::ProfileAction_Load);
    h.Frame();
    CHECK(h.cfg.action == game::ProfileAction_None);
}

TEST_CASE("hotkey list with an unresolved int array binding draws in design mode") {
    Harness h;
    h.roots.clear();
    imforge::Json j;
    imforge::JsonError je;
    const char* text = R"json({
      "schemaVersion": 1, "kind": "panel", "name": "Mock",
      "root": { "id": "window", "type": "Window", "props": { "title": "Mock" },
        "children": [
          { "id": "hk", "type": "HotkeyList", "bind": { "keys": "nothing.keys" } },
          { "id": "hk2", "type": "HotkeyList" }
        ] } })json";
    REQUIRE(imforge::ParseJson(text, j, &je));
    imforge::Error e;
    REQUIRE(imforge::PanelFromJson(j, h.panel, &e));
    h.opts.design_mode = true;
    for (int f = 0; f < 3; ++f) h.Frame();
    CHECK(h.Errors() == 0);
    CHECK(h.Find("hk") != nullptr);
    CHECK(h.Find("hk2") != nullptr);
}
