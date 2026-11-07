#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#ifdef IMFORGE_LIVE
#undef IMFORGE_LIVE
#endif
#include "imforge/imforge_client.h"

#include <string>
#include <vector>

namespace {

struct Player {
    float health = 1.0f;
};
struct Settings {
    bool enabled = false;
};

void Block(imforge::DrawCtx&) {}
void Handler(imforge::DrawCtx&) {}
bool Widget(imforge::DrawCtx&) { return false; }
void LogSink(int, const char*) {}

}  // namespace

TEST_CASE("client header without IMFORGE_LIVE compiles to no-ops") {
    namespace live = imforge::live;
    Player player;
    Settings cfg;

    CHECK_FALSE(live::Connect("tcp://127.0.0.1:7777"));
    CHECK_FALSE(live::IsConnected());
    CHECK(live::Status() != nullptr);
    CHECK(std::string(live::Status()) == "disabled");

    live::SetAppName("noop");
    live::SetLogCallback(&LogSink);
    live::SetApplyTheme(false);
    live::SetDpiScale(2.0f);
    live::ShowMissingPanels(true);
    live::RegisterBlock("MainMenu.custom_1", &Block, "1a2b3c4d");
    live::RegisterBlock("MainMenu.custom_2", &Block);
    live::RegisterHandler("MainMenu.btn.onClick", &Handler);
    live::RegisterWidget("Toggle", &Widget);
    live::RegisterTexture("logo", (void*)nullptr);
    live::RegisterTexture("logo64", (unsigned long long)7);
    live::RegisterFont("Bold", nullptr);

    live::NewFrame();
    live::Draw("MainMenu", {live::Bind("player", player), live::Bind("cfg", cfg)});
    std::vector<live::BindingRoot> roots = {live::Bind("player", player)};
    live::Draw("MainMenu", roots);
    live::Draw("MainMenu", {});
    live::NewFrame();
    live::Disconnect();

    live::BindingRoot r = live::Bind("cfg", cfg);
    CHECK(std::string(r.name) == "cfg");
    CHECK(r.ptr == &cfg);
    CHECK(player.health == 1.0f);
    CHECK_FALSE(cfg.enabled);
}
