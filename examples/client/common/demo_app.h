#pragma once
// Per-frame body shared by all example apps. Call DemoFrame() between ImGui::NewFrame() and
// ImGui::Render().
#include "imgui.h"
#include "imforge/imforge_client.h"
#include "demo_types.h"

#if __has_include("imb_essentials.h")
#include "imb_essentials.h"
#define DEMO_HAS_ESSENTIALS 1
#endif

#include <cstdio>
#include <cstring>

struct DemoState {
    char url[128] = "tcp://127.0.0.1:7777";
    bool registered = false;
    int play_clicks = 0;
    float health_history[64] = {};
    int history_pos = 0;
    Player* player = nullptr;
};

inline DemoState& DemoGet() {
    static DemoState s;
    return s;
}

inline void DemoSetUrl(const char* url) {
    DemoState& d = DemoGet();
    std::snprintf(d.url, sizeof(d.url), "%s", url);
}

inline void DemoOnPlayClicked(imforge::DrawCtx&) {
    DemoState& d = DemoGet();
    d.play_clicks++;
    if (d.player) d.player->health = d.player->max_hp;
}

inline void DemoCustomBlock(imforge::DrawCtx&) {
    DemoState& d = DemoGet();
    ImGui::Text("Hand-written block: health history (%d plays)", d.play_clicks);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 rmin = ImGui::GetCursorScreenPos();
    ImVec2 size(ImGui::GetContentRegionAvail().x, 48.0f);
    ImVec2 rmax(rmin.x + size.x, rmin.y + size.y);
    dl->AddRectFilled(rmin, rmax, IM_COL32(20, 20, 24, 255), 4.0f);
    const int n = IM_ARRAYSIZE(d.health_history);
    for (int i = 1; i < n; ++i) {
        float a = d.health_history[(d.history_pos + i - 1) % n];
        float b = d.health_history[(d.history_pos + i) % n];
        ImVec2 p0(rmin.x + size.x * (float)(i - 1) / (float)(n - 1), rmax.y - size.y * a);
        ImVec2 p1(rmin.x + size.x * (float)i / (float)(n - 1), rmax.y - size.y * b);
        dl->AddLine(p0, p1, IM_COL32(255, 90, 50, 255), 1.5f);
    }
    ImGui::Dummy(size);
}

inline void DemoLog(int level, const char* msg) {
    std::printf("[imforge:%d] %s\n", level, msg);
    std::fflush(stdout);
}

inline void DemoRegisterOnce() {
    DemoState& d = DemoGet();
    if (d.registered) return;
    d.registered = true;
    imforge::live::SetLogCallback(&DemoLog);
    imforge::live::RegisterHandler("MainMenu.btn_play.onClick", &DemoOnPlayClicked);
    imforge::live::RegisterBlock("MainMenu.custom_1", &DemoCustomBlock);
}

inline void DemoStatusWindow(Player& player, Settings& cfg) {
    DemoState& d = DemoGet();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("ImForge demo")) {
        const bool connected = imforge::live::IsConnected();
        ImGui::TextColored(connected ? ImVec4(0.3f, 0.9f, 0.4f, 1.0f) : ImVec4(0.9f, 0.6f, 0.2f, 1.0f),
                           "%s", connected ? "paired with ImForge" : "not connected");
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", imforge::live::Status());
        ImGui::InputText("URL", d.url, sizeof(d.url));
        if (ImGui::Button("Connect")) imforge::live::Connect(d.url);
        ImGui::SameLine();
        if (ImGui::Button("Disconnect")) imforge::live::Disconnect();
        ImGui::Text("%.1f fps (%.2f ms)", io.Framerate, 1000.0f / (io.Framerate > 0.0f ? io.Framerate : 1.0f));
        ImGui::Separator();
        ImGui::Text("player: %s  hp %.0f/%.0f  ammo %d  mode %d", player.name, player.health, player.max_hp,
                    player.ammo, (int)player.mode);
        ImGui::Text("pos (%.0f, %.0f)", player.pos.x, player.pos.y);
        ImGui::Text("esp %s  aim %s  bhop %s", cfg.esp.enabled ? "on" : "off", cfg.aim.enabled ? "on" : "off",
                    cfg.misc.bhop ? "on" : "off");
        ImGui::TextDisabled("Panels drawn live: MainMenu, Overlay (CheatMenu), Controls, Monitor, Arena (Showcase)");
    }
    ImGui::End();
}

inline void DemoFrame(Player& player, Settings& cfg) {
    DemoState& d = DemoGet();
    d.player = &player;
    DemoRegisterOnce();
    imforge::live::NewFrame();

    d.health_history[d.history_pos] = player.max_hp > 0.0f ? player.health / player.max_hp : 0.0f;
    d.history_pos = (d.history_pos + 1) % IM_ARRAYSIZE(d.health_history);

    DemoStatusWindow(player, cfg);

    imforge::live::Draw("MainMenu", {imforge::live::Bind("player", player), imforge::live::Bind("cfg", cfg)});
    imforge::live::Draw("Overlay", {imforge::live::Bind("player", player), imforge::live::Bind("cfg", cfg)});
    // the Showcase sample project's panels (unbound); Draw skips panels the project doesn't have
    imforge::live::Draw("Controls", {});
    imforge::live::Draw("Monitor", {});
    imforge::live::Draw("Arena", {});

#ifdef DEMO_HAS_ESSENTIALS
    imb::ess::DrawToasts();
#endif
}
