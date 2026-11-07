#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#ifndef IMFORGE_LIVE
#define IMFORGE_LIVE
#endif
#define IMFORGE_BACKEND_NAME "headless"
#include "imforge/imforge_client.h"
#include "imgui_headless.h"
#include "live_test_builder.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

struct Esp {
    bool enabled = false;
    float box = 1.5f;
};
struct Settings {
    Esp esp;
    int fov = 90;
};
struct Player {
    float health = 75.0f;
    int ammo = 12;
};
IMFORGE_REFLECT(Esp, enabled, box);
IMFORGE_REFLECT(Settings, esp, fov);
IMFORGE_REFLECT(Player, health, ammo);

namespace ui {
struct MainMenuState {
    int clicks = 0;
    bool open = true;
};
}  // namespace ui

using namespace imforge;
namespace live = imforge::live;

namespace {

const char* kProjectJson = R"({
  "schemaVersion": 1, "kind": "project", "name": "ClientTest",
  "imgui": {"version": "1.91.9", "branch": "master"},
  "platform": {"window": "sdl3", "renderer": "gl3"},
  "codegen": {"namespace": "ui", "dpiMode": "none", "baseFontSize": 13},
  "activeTheme": "Dark",
  "themes": [
    {"schemaVersion": 1, "kind": "theme", "name": "Dark",
     "colors": {"Button": "#ff000080"}, "vars": {"FrameRounding": 3}}
  ],
  "styleClasses": {"danger": {"colors": {"Text": "#ff0000ff"}}},
  "panels": ["panels/MainMenu.imb.json"]
})";

const char* kPanelJson = R"({
  "schemaVersion": 1, "kind": "panel", "name": "MainMenu",
  "params": [{"name": "player", "type": "Player"}, {"name": "cfg", "type": "Settings"}],
  "state": [{"name": "open", "type": "bool", "default": true}],
  "root": {"id": "window", "type": "Window",
           "props": {"title": "Main Menu", "initialSize": {"x": 400, "y": 300}, "flags": ["NoCollapse"]},
           "children": [
             {"id": "cb_esp", "type": "Checkbox", "props": {"label": "ESP"}, "bind": {"value": "cfg.esp.enabled"}},
             {"id": "txt_hp", "type": "Text", "props": {"text": "HP: {player.health:%.0f} / {player.ammo}"}},
             {"id": "btn_toggle", "type": "Button", "props": {"label": "Toggle"},
              "events": {"onClick": {"actions": [{"type": "toggle", "target": "cfg.esp.enabled"}], "handler": true}}},
             {"id": "custom_1", "type": "Code", "props": {"code": "ImGui::Text(\"hand written\");"}}
           ]}
})";

Json ParseOrFail(const char* text) {
    Json j;
    JsonError err;
    bool ok = ParseJson(text, std::strlen(text), j, &err);
    REQUIRE_MESSAGE(ok, err.message);
    return j;
}

uint64_t NowMs() { return detail::NowMs(); }

using Builder = test::LiveTestBuilder;

int g_block_calls = 0;
int g_handler_calls = 0;
Settings* g_handler_cfg = nullptr;
ui::MainMenuState* g_handler_state = nullptr;
std::vector<std::pair<int, std::string>> g_log;

void CustomBlock(DrawCtx&) {
    ++g_block_calls;
    ImGui::Text("hand written");
}

void OnToggleClick(DrawCtx& c) {
    ++g_handler_calls;
    g_handler_cfg = &live::Root<Settings>(c, "cfg");
    ui::MainMenuState& st = live::State<ui::MainMenuState>(c);
    ++st.clicks;
    g_handler_state = &st;
}

bool ToggleWidgetStub(DrawCtx& ctx) {
    bool* v = ctx.BindBool("value");
    return v ? ImGui::Checkbox(ctx.GetString("label").c_str(), v) : false;
}

void LogSink(int level, const char* msg) { g_log.emplace_back(level, msg ? msg : ""); }

struct App {
    test::HeadlessImGui ui;
    Player player;
    Settings cfg;
    ImVec2 mouse = ImVec2(1100.0f, 650.0f);
    bool down = false;

    void SetMouse(ImVec2 p, bool d) {
        mouse = p;
        down = d;
    }
    void Frame() {
        ImGuiIO& io = ImGui::GetIO();
#if IMGUI_VERSION_NUM >= 18700
        io.AddMousePosEvent(mouse.x, mouse.y);
        io.AddMouseButtonEvent(0, down);
#else
        io.MousePos = mouse;
        io.MouseDown[0] = down;
#endif
        ui.BeginFrame();
        live::NewFrame();
        if (live::DebugPanelCount() > 0) ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_Always);
        live::Draw("MainMenu", {live::Bind("player", player), live::Bind("cfg", cfg)});
        ui.EndFrame();
    }
};

template <class Pred> bool PumpUntil(Builder& b, App& app, Pred pred, uint64_t timeout_ms = 5000) {
    uint64_t deadline = NowMs() + timeout_ms;
    for (;;) {
        b.Pump();
        app.Frame();
        b.Pump();
        if (pred()) return true;
        if (NowMs() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void PumpFrames(Builder& b, App& app, int n) {
    for (int i = 0; i < n; ++i) {
        b.Pump();
        app.Frame();
        b.Pump();
    }
}

bool Sync(Builder& b, App& app) {
    size_t pongs = b.Count(msg::kPong);
    b.Send(MsgPing());
    return PumpUntil(b, app, [&] { return b.Count(msg::kPong) > pongs; }, 3000);
}

bool HasStr(const Json& arr, const char* s) {
    for (size_t i = 0; arr.IsArray() && i < arr.Size(); ++i)
        if (arr[i].IsString() && arr[i].AsString() == s) return true;
    return false;
}

ImVec2 Center(const ImVec2& a, const ImVec2& b) { return ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f); }

bool Click(Builder& b, App& app, const char* id) {
    ImVec2 mn, mx;
    if (!live::DebugItemRect("MainMenu", id, &mn, &mx)) return false;
    ImVec2 c = Center(mn, mx);
    app.SetMouse(c, false);
    PumpFrames(b, app, 3);
    app.SetMouse(c, true);
    PumpFrames(b, app, 2);
    app.SetMouse(c, false);
    PumpFrames(b, app, 3);
    return true;
}

}  // namespace

TEST_CASE("client API before any connection") {
    CHECK_FALSE(live::IsConnected());
    CHECK(std::string(live::Status()) == "disconnected");
    CHECK_FALSE(live::Connect("bogus://nowhere"));
    CHECK(std::string(live::Status()).compare(0, 6, "error:") == 0);
    CHECK_FALSE(live::Connect(""));
    CHECK_FALSE(live::IsConnected());
    live::Disconnect();
    CHECK(std::string(live::Status()) == "disconnected");
}

TEST_CASE("live attach end-to-end over tcp loopback") {
    App app;
    Builder builder;
    REQUIRE(builder.Listen());

    live::SetAppName("ClientTest");
    live::SetLogCallback(&LogSink);
    live::RegisterWidget("Toggle", &ToggleWidgetStub);
    live::RegisterTexture("logo", (ImTextureID)(intptr_t)7);
    live::RegisterBlock("MainMenu.custom_1", &CustomBlock, "1a2b3c4d");
    live::RegisterHandler("MainMenu.btn_toggle.onClick", &OnToggleClick);
    live::ShowMissingPanels(true);

    PumpFrames(builder, app, 2);
    CHECK(live::DebugItemCount() == 0);

    REQUIRE(live::Connect(builder.Url().c_str()));
    CHECK(std::string(live::Status()).compare(0, 10, "connecting") == 0);
    REQUIRE(PumpUntil(builder, app, [&] { return live::IsConnected() && builder.t->IsConnected(); }));
    REQUIRE(PumpUntil(builder, app, [&] { return builder.First(msg::kHello) != nullptr; }));

    {
        INFO("hello is a bare pairing request; app metadata and blocks follow sealed once paired");
        HelloMsg hello;
        const Json& raw_hello = *builder.First(msg::kHello);
        REQUIRE(ParseHello(raw_hello, hello));
        CHECK(hello.pairing);
        CHECK(hello.version == kProtocolVersion);
        CHECK(hello.nonce_a.size() == 32);
        CHECK(raw_hello.IsObject());
        CHECK(raw_hello.Size() == 4);
        CHECK_FALSE(raw_hello["app"].IsString());
        CHECK_FALSE(raw_hello["widgets"].IsArray());
        CHECK(builder.pair.Established());
        CHECK(live::IsPaired());
        REQUIRE(PumpUntil(builder, app, [&] { return builder.First(msg::kClientInfo) != nullptr; }));
        ClientInfoMsg info;
        REQUIRE(ParseClientInfo(*builder.First(msg::kClientInfo), info));
        CHECK(info.app == "ClientTest");
        CHECK(info.pid > 0);
        CHECK(info.imgui_version == IMGUI_VERSION_NUM);
        CHECK(info.backend == "headless");
        CHECK_FALSE(info.platform.empty());
        CHECK(HasStr((*builder.First(msg::kClientInfo))["widgets"], "Toggle"));
        CHECK(HasStr((*builder.First(msg::kClientInfo))["textures"], "logo"));
        CHECK(builder.pair.PeerApp() == "ClientTest");

        REQUIRE(PumpUntil(builder, app, [&] { return builder.First(msg::kBlocks) != nullptr; }));
        const Json& blocks = *builder.First(msg::kBlocks);
        CHECK(blocks["panel"].AsString() == "MainMenu");
        REQUIRE(blocks["blocks"].Size() == 1);
        CHECK(blocks["blocks"][0]["id"].AsString() == "custom_1");
        CHECK(blocks["blocks"][0]["h"].AsString() == "1a2b3c4d");
        CHECK(std::string(live::Status()).compare(0, 9, "connected") == 0);
    }

    builder.Send(MsgProject(ParseOrFail(kProjectJson)));
    builder.Send(MsgPanel(ParseOrFail(kPanelJson)));
    REQUIRE(PumpUntil(builder, app, [&] { return live::DebugPanelCount() == 1 && live::DebugItemCount() >= 4; }));
    live::ShowMissingPanels(false);

    {
        INFO("panel renders with bindings, theme and registered block");
        PumpFrames(builder, app, 2);
        CHECK(live::DebugItemCount() >= 4);
        ImVec2 mn, mx;
        CHECK(live::DebugItemRect("MainMenu", "cb_esp", &mn, &mx));
        CHECK(mx.x > mn.x);
        CHECK(mx.y > mn.y);
        CHECK(live::DebugItemRect("MainMenu", "txt_hp", &mn, &mx));
        CHECK(live::DebugItemRect("MainMenu", "btn_toggle", &mn, &mx));
        CHECK(g_block_calls > 0);
        const ImVec4& btn = ImGui::GetStyle().Colors[ImGuiCol_Button];
        CHECK(btn.x == doctest::Approx(1.0f).epsilon(0.01));
        CHECK(btn.y == doctest::Approx(0.0f).epsilon(0.01));
        CHECK(btn.w == doctest::Approx(128.0f / 255.0f).epsilon(0.01));
        CHECK(ImGui::GetStyle().FrameRounding == doctest::Approx(3.0f));
        for (const auto& e : g_log) {
            CHECK_MESSAGE(e.first < 2, e.second);
            CHECK(e.second.find("txt_hp") == std::string::npos);
        }
    }

    {
        INFO("theme message updates the style when enabled");
        Json theme = ParseOrFail(R"({"schemaVersion":1,"kind":"theme","name":"Dark","colors":{"Button":"#00ff00ff"},"vars":{}})");
        builder.Send(MsgTheme(theme));
        REQUIRE(PumpUntil(builder, app, [&] { return ImGui::GetStyle().Colors[ImGuiCol_Button].y > 0.9f; }));
        live::SetApplyTheme(false);
        builder.Send(MsgTheme(ParseOrFail(
            R"({"schemaVersion":1,"kind":"theme","name":"Dark","colors":{"Button":"#0000ffff"},"vars":{}})")));
        REQUIRE(Sync(builder, app));
        PumpFrames(builder, app, 3);
        CHECK(ImGui::GetStyle().Colors[ImGuiCol_Button].y > 0.9f);
        live::SetApplyTheme(true);
        CHECK(ImGui::GetStyle().Colors[ImGuiCol_Button].z > 0.9f);
    }

    {
        INFO("picker mode: hover and click report to the builder without firing events");
        builder.Send(MsgMode(true, "", "MainMenu"));
        REQUIRE(Sync(builder, app));
        PumpFrames(builder, app, 3);
        ImVec2 mn, mx;
        REQUIRE(live::DebugItemRect("MainMenu", "cb_esp", &mn, &mx));
        app.SetMouse(Center(mn, mx), false);
        REQUIRE(PumpUntil(builder, app, [&] {
            const Json* h = builder.Last(msg::kHover);
            return h && (*h)["id"].AsString() == "cb_esp";
        }));
        CHECK((*builder.Last(msg::kHover))["panel"].AsString() == "MainMenu");

        bool before = app.cfg.esp.enabled;
        REQUIRE(Click(builder, app, "cb_esp"));
        REQUIRE(PumpUntil(builder, app, [&] { return builder.Last(msg::kSelect) != nullptr; }, 2000));
        TargetMsg sel;
        REQUIRE(ParseSelect(*builder.Last(msg::kSelect), sel));
        CHECK(sel.panel == "MainMenu");
        CHECK(sel.id == "cb_esp");
#if IMGUI_VERSION_NUM >= 18600
        CHECK(app.cfg.esp.enabled == before);
#else
        app.cfg.esp.enabled = before;
#endif

        app.SetMouse(ImVec2(1100.0f, 650.0f), false);
        REQUIRE(PumpUntil(builder, app, [&] {
            const Json* h = builder.Last(msg::kHover);
            return h && (*h)["id"].AsString().empty();
        }));

        builder.Send(MsgMode(false, "cb_esp", "MainMenu"));
        REQUIRE(Sync(builder, app));
        PumpFrames(builder, app, 3);
        size_t selects = builder.Count(msg::kSelect);
        REQUIRE(Click(builder, app, "cb_esp"));
        PumpFrames(builder, app, 3);
        CHECK(app.cfg.esp.enabled != before);
        CHECK(builder.Count(msg::kSelect) == selects);
    }

    {
        INFO("button click runs actions and the registered handler with Root/State");
        builder.Send(MsgMode(false, "", ""));
        REQUIRE(Sync(builder, app));
        PumpFrames(builder, app, 3);
        bool before = app.cfg.esp.enabled;
        int calls = g_handler_calls;
        REQUIRE(Click(builder, app, "btn_toggle"));
        PumpFrames(builder, app, 2);
        CHECK(g_handler_calls == calls + 1);
        CHECK(app.cfg.esp.enabled != before);
        CHECK(g_handler_cfg == &app.cfg);
        REQUIRE(g_handler_state != nullptr);
        CHECK(g_handler_state->clicks == g_handler_calls);
        REQUIRE(Click(builder, app, "btn_toggle"));
        CHECK(g_handler_calls == calls + 2);
        CHECK(app.cfg.esp.enabled == before);
        CHECK(g_handler_state->clicks == g_handler_calls);
    }

    {
        INFO("stats, ping/pong, remove_panel and disconnect");
        REQUIRE(PumpUntil(builder, app, [&] { return builder.Count(msg::kStats) >= 1; }, 4000));
        StatsMsg stats;
        REQUIRE(ParseStats(*builder.Last(msg::kStats), stats));
        CHECK(stats.fps >= 0.0f);
        CHECK(HasStr((*builder.Last(msg::kStats))["panels"], "MainMenu"));

        REQUIRE(Sync(builder, app));

        builder.Send(MsgRemovePanel("MainMenu"));
        REQUIRE(PumpUntil(builder, app, [&] { return live::DebugPanelCount() == 0; }, 2000));
        PumpFrames(builder, app, 2);
        CHECK(live::DebugItemCount() == 0);

        builder.Send(MsgPanel(ParseOrFail(kPanelJson)));
        REQUIRE(PumpUntil(builder, app, [&] { return live::DebugItemCount() >= 4; }, 2000));

        live::Disconnect();
        CHECK_FALSE(live::IsConnected());
        CHECK(std::string(live::Status()) == "disconnected");
        PumpFrames(builder, app, 2);
        CHECK(live::DebugItemCount() >= 4);
    }

    live::Disconnect();
    builder.t->Close();
}
